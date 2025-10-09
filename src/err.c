#include "../include/err.h"
void vm_err_push(struct VM *vm, ErrFrame *f){
  f->prev = (ErrFrame*)vm->err_frame;
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
