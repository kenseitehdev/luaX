#include "../include/err.h"
void vm_err_push(struct VM *vm, ErrFrame *f){
  f->prev = (ErrFrame*)vm->err_frame;
  f->env_at_push = vm->env;  // ← ADD THIS LINE
  vm->err_frame = f;
}
void vm_err_pop(struct VM *vm){
  if (vm->err_frame) vm->err_frame = ((ErrFrame*)vm->err_frame)->prev;
}
void vm_raise(struct VM *vm, Value err){
    vm->err_obj = (err.tag == VAL_NIL) ? V_str_from_c("error") : err;
    if (!vm->err_frame){
for (Env *e = vm->env; e; e = e->parent) {
          env_close_all(vm, e, vm->err_obj);
        }
        if (vm->err_obj.tag == VAL_STR)
            fprintf(stderr, "[LuaX]: %s\n", vm->err_obj.as.s->data);
        else
            fprintf(stderr, "[LuaX]: error\n");
        exit(1);
    }
    ErrFrame *top = (ErrFrame*)vm->err_frame;
    while (vm->env && vm->env != top->env_at_push) {
      env_close_all(vm, vm->env, vm->err_obj);
      vm->env = vm->env->parent;
    }
    longjmp(top->jb, 1);
}

Value call_debug_traceback(struct VM *vm, Value msg, int level) {
  Value dbg;
  if (!env_get(vm->env, "debug", &dbg) || dbg.tag != VAL_TABLE) {
    return msg;
  }
  Value tb;
  if (!tbl_get(dbg.as.t, V_str_from_c("traceback"), &tb) || !is_callable(tb)) {
    return msg;
  }
  Value args[2];
  int argc = 1;
  args[0] = msg;
  if (level >= 0) { args[1] = V_int((long long)level); argc = 2; }
  Value out = call_any(vm, tb, argc, args);
  return (out.tag == VAL_STR) ? out : msg;
}
Value builtin_error(struct VM *vm, int argc, Value *argv){
  Value obj = (argc >= 1) ? argv[0] : V_str_from_c("error");
  int level = 1;
  if (argc >= 2) {
    if (argv[1].tag == VAL_INT) level = (int)argv[1].as.i;
    else if (argv[1].tag == VAL_NUM) level = (int)argv[1].as.n;
  }
  if (obj.tag == VAL_STR) {
    Value traced = call_debug_traceback(vm, obj, level);
    vm_raise(vm, traced);
    return V_nil();
  }
  vm_raise(vm, obj);
  return V_nil();
}

