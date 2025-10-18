// lib/exception.c - Lua-compatible exception library
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "../include/interpreter.h"
#include "../include/err.h"  /* For ErrFrame, vm_err_push, vm_err_pop */

/* ---- Helpers ---- */

/* Duplicate a string */
static char *sdup(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s) + 1;
    char *p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* Convert a Value to string */
static char *value_to_string(Value v) {
    char buf[128];
    switch (v.tag) {
        case VAL_STR: {
            char *s = (char*)malloc((size_t)v.as.s->len + 1);
            if (s) {
                memcpy(s, v.as.s->data, (size_t)v.as.s->len);
                s[v.as.s->len] = '\0';
            }
            return s;
        }
        case VAL_NIL:   return sdup("nil");
        case VAL_BOOL:  return sdup(v.as.b ? "true" : "false");
        case VAL_INT:   snprintf(buf, sizeof(buf), "%lld", v.as.i); return sdup(buf);
        case VAL_NUM:   snprintf(buf, sizeof(buf), "%.14g", v.as.n); return sdup(buf);
        case VAL_TABLE: return sdup("table");
        case VAL_FUNC:  return sdup("function");
        case VAL_CFUNC: return sdup("function");
        case VAL_COROUTINE: return sdup("thread");
        default:        return sdup("<unknown>");
    }
}

/* Error helper */
static Value exception_error(const char *msg) {
    fprintf(stderr, "exception: %s\n", msg);
    return V_nil();
}
static int protected_call(struct VM *vm, Value func, int argc, Value *argv, Value *ret) {
    if (!is_callable(func)) {
        *ret = V_nil();
        return 0;
    }

    ErrFrame frame;
    frame.env_at_push = vm->env;
    
    Value old_err_obj = vm->err_obj;
    
    vm_err_push(vm, &frame);

    if (setjmp(frame.jb) == 0) {
        *ret = call_any_public(vm, func, argc, argv);
        vm_err_pop(vm);
        vm->err_obj = old_err_obj;
        return 1;
    } else {
        *ret = vm->err_obj;
        vm_err_pop(vm);
        vm->err_obj = old_err_obj;
        return 0;
    }
}

/* ---- Core functions ---- */

/* throw(exception) */
static Value exc_throw(struct VM *vm, int argc, Value *argv) {
    if (argc < 1) return exception_error("bad argument #1 to 'throw' (value expected)");
    char *msg = value_to_string(argv[0]);
    if (msg) {
        vm_raise(vm, V_str_from_c(msg));
        free(msg);
    }
    return V_nil(); // never reached
}

/* pcall(func, ...) -> { success, result } */
static Value exc_pcall(struct VM *vm, int argc, Value *argv) {
    if (argc < 1 || !is_callable(argv[0]))
        return exception_error("bad argument #1 to 'pcall' (function expected)");

    Value func = argv[0], ret;
    int success = protected_call(vm, func, argc - 1, argv + 1, &ret);

    Value result = V_table();
    tbl_set_public(result.as.t, V_int(1), V_bool(success));
    tbl_set_public(result.as.t, V_int(2), ret);
    return result;
}

/* xpcall(func, err_handler, ...) -> { success, result } */
static Value exc_xpcall(struct VM *vm, int argc, Value *argv) {
    if (argc < 2 || !is_callable(argv[0]) || !is_callable(argv[1]))
        return exception_error("bad arguments to 'xpcall' (function, function expected)");

    Value func = argv[0], handler = argv[1], ret;
    int success = protected_call(vm, func, argc - 2, argv + 2, &ret);

    if (!success) {
        Value handler_ret;
        Value args[1] = { ret };
        protected_call(vm, handler, 1, args, &handler_ret);
        ret = handler_ret;
    }

    Value result = V_table();
    tbl_set_public(result.as.t, V_int(1), V_bool(success));
    tbl_set_public(result.as.t, V_int(2), ret);
    return result;
}

/* exception.type(value) -> string */
static Value exc_type(struct VM *vm, int argc, Value *argv) {
    (void)vm;
    if (argc < 1) return exception_error("bad argument #1 to 'type' (value expected)");
    const char *typename;
    switch (argv[0].tag) {
        case VAL_NIL:      typename = "nil"; break;
        case VAL_BOOL:     typename = "boolean"; break;
        case VAL_INT:
        case VAL_NUM:      typename = "number"; break;
        case VAL_STR:      typename = "string"; break;
        case VAL_TABLE:    typename = "table"; break;
        case VAL_FUNC:
        case VAL_CFUNC:    typename = "function"; break;
        case VAL_COROUTINE: typename = "thread"; break;
        default:           typename = "value"; break;
    }
    return V_str_from_c(typename);
}

/* exception.tostring(value) -> string */
static Value exc_tostring(struct VM *vm, int argc, Value *argv) {
    (void)vm;
    if (argc < 1) return exception_error("bad argument #1 to 'tostring' (value expected)");
    char *s = value_to_string(argv[0]);
    Value result = V_str_from_c(s);
    free(s);
    return result;
}

/* ---- try/catch/finally using VM state ---- */

/* try(func) - executes function and stores exception in VM if it fails */
static Value exc_try(struct VM *vm, int argc, Value *argv) {
    if (argc < 1 || !is_callable(argv[0]))
        return exception_error("bad argument #1 to 'try' (function expected)");

    Value ret;
    int success = protected_call(vm, argv[0], 0, NULL, &ret);

    // Store exception state in VM for catch/finally to use
    vm->has_exception = !success;
    if (!success) {
        vm->last_exception = ret;
    } else {
        vm->last_exception = V_nil();
    }

    return V_nil();
}

/* catch(handler) - calls handler with exception if one occurred */
static Value exc_catch(struct VM *vm, int argc, Value *argv) {
    if (!vm->has_exception) {
        // No exception to catch, just return nil
        return V_nil();
    }
    
    if (argc < 1 || !is_callable(argv[0]))
        return exception_error("bad argument #1 to 'catch' (function expected)");

    Value exc = vm->last_exception;
    Value ret;
    Value args[1] = { exc };
    protected_call(vm, argv[0], 1, args, &ret);

    // Mark exception as handled
    vm->has_exception = 0;
    vm->last_exception = V_nil();
    
    return ret;
}

/* finally(handler) - always executes handler, clears exception state */
static Value exc_finally(struct VM *vm, int argc, Value *argv) {
    if (argc < 1 || !is_callable(argv[0]))
        return exception_error("bad argument #1 to 'finally' (function expected)");

    Value ret;
    protected_call(vm, argv[0], 0, NULL, &ret);
    
    // Clear exception state after finally block
    // (in real Lua, finally would re-throw if exception wasn't caught)
    vm->has_exception = 0;
    vm->last_exception = V_nil();
    
    return ret;
}

/* ---- Registration ---- */
void register_exception_lib(struct VM *vm) {
    Value E = V_table();

    tbl_set_public(E.as.t, V_str_from_c("throw"),     (Value){.tag=VAL_CFUNC, .as.cfunc=exc_throw});
    tbl_set_public(E.as.t, V_str_from_c("pcall"),     (Value){.tag=VAL_CFUNC, .as.cfunc=exc_pcall});
    tbl_set_public(E.as.t, V_str_from_c("xpcall"),    (Value){.tag=VAL_CFUNC, .as.cfunc=exc_xpcall});
    tbl_set_public(E.as.t, V_str_from_c("try"),       (Value){.tag=VAL_CFUNC, .as.cfunc=exc_try});
    tbl_set_public(E.as.t, V_str_from_c("catch"),     (Value){.tag=VAL_CFUNC, .as.cfunc=exc_catch});
    tbl_set_public(E.as.t, V_str_from_c("finally"),   (Value){.tag=VAL_CFUNC, .as.cfunc=exc_finally});
    tbl_set_public(E.as.t, V_str_from_c("type"),      (Value){.tag=VAL_CFUNC, .as.cfunc=exc_type});
    tbl_set_public(E.as.t, V_str_from_c("tostring"),  (Value){.tag=VAL_CFUNC, .as.cfunc=exc_tostring});

    env_add_public(vm->env, "exception", E, false);
    
    // Initialize exception state
    vm->has_exception = 0;
    vm->last_exception = V_nil();
}
