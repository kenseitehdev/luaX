#include "../include/err.h"
void vm_err_push(struct VM *vm, ErrFrame *f){
  f->prev = (ErrFrame*)vm->err_frame;
  f->env_at_push = vm->env;  // ← ADD THIS LINE
  vm->err_frame = f;
}
void vm_err_pop(struct VM *vm){
  if (vm->err_frame) vm->err_frame = ((ErrFrame*)vm->err_frame)->prev;
}
void vm_raise(struct VM *vm, Value err) {
    vm->err_obj = (err.tag == VAL_NIL) ? V_str_from_c("error") : err;
    const char *msg = (vm->err_obj.tag == VAL_STR) ? vm->err_obj.as.s->data : "error";

    if (!vm->err_frame) {
        for (Env *e = vm->env; e; e = e->parent) {
            env_close_all(vm, e, vm->err_obj);
        }
        if (vm->current_line > 0)
            fprintf(stderr, "[LuaX]: line %d: %s\n", vm->current_line, msg);
        else
            fprintf(stderr, "[LuaX]: %s\n", msg);
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
    // Default fallback message
    const char *fallback = "[LuaX]: (traceback unavailable)";
    Value dbg, tb;

    // 1️⃣ Safely check for 'debug' table
    if (!env_get(vm->env, "debug", &dbg) || dbg.tag != VAL_TABLE) {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "[LuaX]: line %d: %s\n  %s",
                 vm->current_line,
                 (msg.tag == VAL_STR) ? msg.as.s->data : "error",
                 fallback);
        return V_str_from_c(buf);
    }

    // 2️⃣ Ensure 'traceback' exists and is callable
    if (!tbl_get(dbg.as.t, V_str_from_c("traceback"), &tb) || !is_callable(tb)) {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "[LuaX]: line %d: %s\n  (no debug.traceback available)",
                 vm->current_line,
                 (msg.tag == VAL_STR) ? msg.as.s->data : "error");
        return V_str_from_c(buf);
    }

    // 3️⃣ Prepare arguments safely
    Value args[2];
    int argc = 1;
    args[0] = msg;
    if (level >= 0) {
        args[1] = V_int((long long)level);
        argc = 2;
    }

    // 4️⃣ Protect call_any() against recursive error
    ErrFrame frame;
    if (setjmp(frame.jb) != 0) {
        // Exception occurred inside debug.traceback
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "[LuaX]: line %d: %s\n  (error while running debug.traceback)",
                 vm->current_line,
                 (msg.tag == VAL_STR) ? msg.as.s->data : "error");
        vm_err_pop(vm);
        return V_str_from_c(buf);
    }

    vm_err_push(vm, &frame);
    Value out = call_any(vm, tb, argc, args);
    vm_err_pop(vm);

    // 5️⃣ Validate and return result
    if (out.tag == VAL_STR) {
        // Optionally append line info if not included
        char buf[1024];
        snprintf(buf, sizeof(buf),
                 "[LuaX]: line %d: %s\n%s",
                 vm->current_line,
                 (msg.tag == VAL_STR) ? msg.as.s->data : "error",
                 out.as.s->data);
        return V_str_from_c(buf);
    }

    // Fallback again if traceback returns non-string
    char buf[512];
    snprintf(buf, sizeof(buf),
             "[LuaX]: line %d: %s\n  (invalid traceback result)",
             vm->current_line,
             (msg.tag == VAL_STR) ? msg.as.s->data : "error");
    return V_str_from_c(buf);
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

