// coroutine.c

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "../include/interpreter.h"

/* ---------------------------
 * Enhanced coroutine record with full Lua compatibility
 * --------------------------- */

typedef enum {
    CO_DEAD = 0,
    CO_SUSPENDED = 1,
    CO_RUNNING = 2,
    CO_NORMAL = 3     /* When coroutine calls another coroutine */
} CoStatus;

typedef struct CoStackFrame {
    char **names;            /* Variable names snapshot */
    Value *vals;             /* Variable values snapshot */
    bool *is_local;          /* Local flags snapshot */
    int var_count;           /* Number of variables */
    CoResumePoint point;     /* Resume point (blk, pc) */
    struct CoStackFrame *next;
} CoStackFrame;

typedef struct Coroutine {
    Value fn;                /* the coroutine function (VAL_FUNC/VAL_CFUNC) */
    CoStatus status;

    /* Enhanced resume point with full stack context */
    CoResumePoint point;
    Env *env_on_yield;       /* Environment chain snapshot */
    CoStackFrame *call_stack; /* Full call stack preservation */

    /* Execution state */
    int started;             /* 0=new, 1=started */
    Value *yield_values;     /* Values passed to yield (to the resumer) */
    int yield_count;
    Value *resume_values;    /* Values passed to resume (to yield’s caller) */
    int resume_count;
    bool pending_yield_return; /* next yield() should return resume_values */

    /* Error handling */
    Value error_value;       /* If coroutine died with error */
    bool has_error;

    /* Nesting support */
    struct Coroutine *caller; /* Which coroutine resumed this one */
    struct Coroutine *callee;  /* Which coroutine this one is running */

    /* Memory management */
    int ref_count;           /* Reference counting for GC */
    bool marked;             /* GC mark bit */
} Coroutine;

/* Global coroutine management */
static Coroutine *g_main_coroutine = NULL;  /* The main thread */
static Coroutine *g_current_coroutine = NULL;

/* We represent coroutine values as tables with a hidden pointer field. */
static const char *CO_PTR = "_co_ptr";
static const char *CO_TYPE = "_co_type";

/* Forwarded helpers from your VM */
extern void  tbl_set_public(struct Table *t, Value key, Value val);
extern int   tbl_get_public(struct Table *t, Value key, Value *out);
extern void  env_add_public(struct Env *e, const char *name, Value v, bool is_local);
extern Value call_any_public(struct VM *vm, Value cal, int argc, Value *argv);

/* Helper function for error handling - simple stderr print */
static Value vm_error_simple(struct VM *vm, const char *msg) {
    (void)vm; /* unused in simple implementation */
    fprintf(stderr, "Coroutine error: %s\n", msg);
    return V_nil();
}

/* ---------------------------
 * Stack frame management
 * --------------------------- */

static CoStackFrame* co_frame_create(void) {
    CoStackFrame *frame = (CoStackFrame*)calloc(1, sizeof(CoStackFrame));
    if (!frame) {
        fprintf(stderr, "Coroutine: Out of memory creating stack frame\n");
        exit(1);
    }
    return frame;
}

static void co_frame_destroy(CoStackFrame *frame) {
    if (!frame) return;

    if (frame->names) {
        for (int i = 0; i < frame->var_count; i++) {
            if (frame->names[i]) free(frame->names[i]);
        }
        free(frame->names);
    }
    if (frame->vals)     free(frame->vals);
    if (frame->is_local) free(frame->is_local);

    CoStackFrame *next = frame->next;
    free(frame);
    if (next) co_frame_destroy(next);
}

static void co_save_stack_frame(Coroutine *co, struct VM *vm) {
    if (!co || !vm) return;

    CoStackFrame *frame = co_frame_create();

    /* Save current resume point */
    frame->point = vm->co_point;

    /* Save environment variables */
    if (vm->env && vm->env->count > 0) {
        frame->var_count = vm->env->count;

        /* Deep copy names */
        frame->names = (char**)calloc((size_t)frame->var_count, sizeof(char*));
        for (int i = 0; i < frame->var_count; i++) {
            if (vm->env->names[i]) {
                size_t len = strlen(vm->env->names[i]) + 1;
                frame->names[i] = (char*)malloc(len);
                if (frame->names[i]) memcpy(frame->names[i], vm->env->names[i], len);
            }
        }

        /* Deep copy values */
        frame->vals = (Value*)malloc(sizeof(Value) * (size_t)frame->var_count);
        if (frame->vals) memcpy(frame->vals, vm->env->vals, sizeof(Value) * (size_t)frame->var_count);

        /* Deep copy local flags */
        frame->is_local = (bool*)malloc(sizeof(bool) * (size_t)frame->var_count);
        if (frame->is_local) memcpy(frame->is_local, vm->env->is_local, sizeof(bool) * (size_t)frame->var_count);
    }

    /* Link into coroutine's stack chain */
    frame->next = co->call_stack;
    co->call_stack = frame;
}

static void co_restore_stack_frame(Coroutine *co, struct VM *vm) {
    if (!co || !vm || !co->call_stack) return;

    CoStackFrame *frame = co->call_stack;

    /* Restore resume point */
    vm->co_point = frame->point;

    /* Restore environment variables */
    if (frame->var_count > 0 && vm->env) {
        /* Ensure environment has enough capacity */
        if (vm->env->cap < frame->var_count) {
            int new_cap = frame->var_count * 2;

            vm->env->names    = (char**)realloc(vm->env->names,    sizeof(char*) * (size_t)new_cap);
            vm->env->vals     = (Value*)realloc(vm->env->vals,     sizeof(Value) * (size_t)new_cap);
            vm->env->is_local = (bool*)realloc(vm->env->is_local,  sizeof(bool) * (size_t)new_cap);

            vm->env->cap = new_cap;
        }

        /* Clear existing names to avoid leaks */
        for (int i = 0; i < vm->env->count; i++) {
            if (vm->env->names[i]) {
                free(vm->env->names[i]);
                vm->env->names[i] = NULL;
            }
        }

        /* Copy names */
        for (int i = 0; i < frame->var_count; i++) {
            if (frame->names[i]) {
                size_t len = strlen(frame->names[i]) + 1;
                vm->env->names[i] = (char*)malloc(len);
                if (vm->env->names[i]) memcpy(vm->env->names[i], frame->names[i], len);
            } else {
                vm->env->names[i] = NULL;
            }
        }

        /* Copy values and flags */
        memcpy(vm->env->vals,     frame->vals,     sizeof(Value) * (size_t)frame->var_count);
        memcpy(vm->env->is_local, frame->is_local, sizeof(bool)  * (size_t)frame->var_count);

        vm->env->count = frame->var_count;
    }

    /* Pop the frame */
    co->call_stack = frame->next;
    frame->next = NULL;
    co_frame_destroy(frame);
}

/* ---------------------------
 * Coroutine boxing/unboxing with type safety
 * --------------------------- */

static Value V_coroutine_box(Coroutine *co) {
    Value t = V_table();

    /* Box pointer via CFunc slot (reusing existing mechanism) */
    Value ptr;
    ptr.tag = VAL_CFUNC;
    ptr.as.cfunc = (CFunc)co;
    tbl_set_public(t.as.t, V_str_from_c(CO_PTR), ptr);

    /* Add type marker for safety */
    tbl_set_public(t.as.t, V_str_from_c(CO_TYPE), V_str_from_c("coroutine"));

    /* Add reference */
    co->ref_count++;

    return t;
}

static Coroutine* co_from_value(Value v) {
    if (v.tag != VAL_TABLE) return NULL;

    /* Check type marker */
    Value type_val;
    if (!tbl_get_public(v.as.t, V_str_from_c(CO_TYPE), &type_val)) return NULL;
    if (type_val.tag != VAL_STR || strcmp(type_val.as.s->data, "coroutine") != 0) return NULL;

    Value ptr;
    if (!tbl_get_public(v.as.t, V_str_from_c(CO_PTR), &ptr)) return NULL;
    if (ptr.tag != VAL_CFUNC) return NULL;

    return (Coroutine*)(void*)ptr.as.cfunc;
}

static int co_is_callable(Value v) {
    return (v.tag == VAL_FUNC) || (v.tag == VAL_CFUNC);
}

/* ---------------------------
 * Memory management
 * --------------------------- */

static void co_unref(Coroutine *co) {
    if (!co) return;
    co->ref_count--;
    if (co->ref_count <= 0) {
        co_frame_destroy(co->call_stack);
        if (co->yield_values)  free(co->yield_values);
        if (co->resume_values) free(co->resume_values);
        free(co);
    }
}

/* ---------------------------
 * Enhanced result handling
 * --------------------------- */

static Value make_result_tuple(bool success, int value_count, Value *values) {
    Value tup = V_table();
    tbl_set_public(tup.as.t, V_int(1), V_bool(success ? 1 : 0));

    for (int i = 0; i < value_count; i++) {
        tbl_set_public(tup.as.t, V_int(i + 2), values[i]);
    }

    return tup;
}

static Value make_ok_result(int value_count, Value *values) {
    return make_result_tuple(true, value_count, values);
}

static Value make_error_result(const char *msg) {
    Value err = V_str_from_c(msg);
    return make_result_tuple(false, 1, &err);
}

/* ---------------------------
 * Main coroutine initialization
 * --------------------------- */

static void ensure_main_coroutine(struct VM *vm) {
    if (g_main_coroutine) return;

    g_main_coroutine = (Coroutine*)calloc(1, sizeof(Coroutine));
    if (!g_main_coroutine) {
        fprintf(stderr, "Coroutine: Failed to create main coroutine\n");
        exit(1);
    }

    g_main_coroutine->status    = CO_RUNNING;
    g_main_coroutine->started   = 1;
    g_main_coroutine->ref_count = 1;
    g_main_coroutine->fn        = V_nil(); /* Main thread has no function */

    g_current_coroutine = g_main_coroutine;
}

/* ---------------------------
 * Builtins
 * --------------------------- */

static Value co_create(struct VM *vm, int argc, Value *argv) {
    ensure_main_coroutine(vm);

    if (argc < 1 || !co_is_callable(argv[0])) {
        return vm_error_simple(vm, "coroutine.create: function expected");
    }

    Coroutine *co = (Coroutine*)calloc(1, sizeof(Coroutine));
    if (!co) {
        return vm_error_simple(vm, "coroutine.create: out of memory");
    }

    co->fn        = argv[0];
    co->status    = CO_SUSPENDED;
    co->started   = 0;
    co->ref_count = 0;
    co->caller    = g_current_coroutine;
    co->pending_yield_return = false;

    return V_coroutine_box(co);
}

static Value co_yield(struct VM *vm, int argc, Value *argv) {
    ensure_main_coroutine(vm);

    if (g_current_coroutine == g_main_coroutine) {
        return vm_error_simple(vm, "attempt to yield from outside a coroutine");
    }

    Coroutine *co = g_current_coroutine;

    /* If we were resumed into this callsite, yield() should RETURN the resume values */
    if (co->pending_yield_return) {
        co->pending_yield_return = false;
        if (co->resume_count > 0) return co->resume_values[0];
        return V_nil();
    }

    if (co->status != CO_RUNNING) {
        return vm_error_simple(vm, "attempt to yield from non-running coroutine");
    }

    /* Store yield values for the resumer */
    if (co->yield_values) { free(co->yield_values); co->yield_values = NULL; }
    co->yield_count = argc;
    if (argc > 0) {
        co->yield_values = (Value*)malloc(sizeof(Value) * (size_t)argc);
        if (co->yield_values) memcpy(co->yield_values, argv, sizeof(Value) * (size_t)argc);
    }

    /* Save complete execution context */
    co_save_stack_frame(co, vm);
    co->point        = vm->co_point;
    co->env_on_yield = vm->env;

    /* Mark for suspension */
    vm->co_yielding = true;
    co->status      = CO_SUSPENDED;

    return V_nil();
}

static Value co_resume(struct VM *vm, int argc, Value *argv) {
    ensure_main_coroutine(vm);

    if (argc < 1) {
        return make_error_result("coroutine expected");
    }

    Coroutine *co = co_from_value(argv[0]);
    if (!co) {
        return make_error_result("bad coroutine");
    }

    if (co->status == CO_RUNNING) {
        return make_error_result("cannot resume running coroutine");
    }

    if (co->status == CO_DEAD) {
        return make_error_result("cannot resume dead coroutine");
    }

    /* Handle resume arguments */
    int resume_argc = (argc > 1) ? (argc - 1) : 0;
    Value *resume_argv = NULL;
    if (resume_argc > 0) {
        resume_argv = (Value*)malloc(sizeof(Value) * (size_t)resume_argc);
        if (resume_argv) memcpy(resume_argv, argv + 1, sizeof(Value) * (size_t)resume_argc);
    }

    /* Set up coroutine nesting */
    Coroutine *caller = g_current_coroutine;
    if (caller != g_main_coroutine) {
        caller->status = CO_NORMAL;
        caller->callee = co;
    }
    co->caller = caller;

    /* Store resume values for the coroutine to access (as yield() return) */
    if (co->resume_values) { free(co->resume_values); co->resume_values = NULL; }
    co->resume_values = resume_argv;
    co->resume_count  = resume_argc;

    /* Switch to target coroutine */
    g_current_coroutine = co;
    co->status     = CO_RUNNING;
    vm->active_co  = co;
    vm->co_yielding = false;

    Value ret = V_nil();

    if (!co->started) {
        /* First resume: call the function with resume args */
        co->started = 1;
        ret = call_any_public(vm, co->fn, resume_argc, resume_argv);

        if (vm->co_yielding) {
            /* Coroutine yielded */
            Value result = make_ok_result(co->yield_count, co->yield_values);

            /* Restore caller */
            g_current_coroutine = caller;
            if (caller != g_main_coroutine) {
                caller->status = CO_RUNNING;
                caller->callee = NULL;
            }
            co->caller = NULL;
            vm->co_yielding = false;

            return result;
        } else {
            /* Coroutine completed normally */
            co->status = CO_DEAD;

            /* Restore caller */
            g_current_coroutine = caller;
            if (caller != g_main_coroutine) {
                caller->status = CO_RUNNING;
                caller->callee = NULL;
            }
            co->caller = NULL;

            return make_ok_result(1, &ret);
        }
    } else {
        /* Resume from yield point:
           - restore frame and env
           - arm yield() to return resume values
           - re-enter function (your exec path consults vm->co_point) */
        co_restore_stack_frame(co, vm);
        vm->co_point = co->point;
        vm->env      = co->env_on_yield;

        co->pending_yield_return = true;

        ret = call_any_public(vm, co->fn, 0, NULL);

        if (vm->co_yielding) {
            /* Yielded again */
            Value result = make_ok_result(co->yield_count, co->yield_values);

            /* Restore caller */
            g_current_coroutine = caller;
            if (caller != g_main_coroutine) {
                caller->status = CO_RUNNING;
                caller->callee = NULL;
            }
            co->caller = NULL;
            vm->co_yielding = false;

            return result;
        } else {
            /* Completed */
            co->status = CO_DEAD;

            /* Restore caller */
            g_current_coroutine = caller;
            if (caller != g_main_coroutine) {
                caller->status = CO_RUNNING;
                caller->callee = NULL;
            }
            co->caller = NULL;

            return make_ok_result(1, &ret);
        }
    }
}

static Value co_running(struct VM *vm, int argc, Value *argv) {
    (void)argc; (void)argv;
    ensure_main_coroutine(vm);

    if (g_current_coroutine == g_main_coroutine) {
        return V_nil(); /* Main thread returns nil */
    }

    return V_coroutine_box(g_current_coroutine);
}

static Value co_status(struct VM *vm, int argc, Value *argv) {
    (void)vm;
    if (argc < 1) {
        return V_str_from_c("dead");
    }

    Coroutine *co = co_from_value(argv[0]);
    if (!co) {
        return V_str_from_c("dead");
    }

    switch (co->status) {
        case CO_SUSPENDED: return V_str_from_c("suspended");
        case CO_RUNNING:
            return (co == g_current_coroutine)
                       ? V_str_from_c("running")
                       : V_str_from_c("normal");
        case CO_NORMAL:    return V_str_from_c("normal");
        case CO_DEAD:      return V_str_from_c("dead");
        default:           return V_str_from_c("dead");
    }
}

static Value co_isyieldable(struct VM *vm, int argc, Value *argv) {
    (void)argc; (void)argv;
    ensure_main_coroutine(vm);

    /* A coroutine is yieldable if it's running and not the main thread */
    bool yieldable = (g_current_coroutine != g_main_coroutine) &&
                     (g_current_coroutine->status == CO_RUNNING);

    return V_bool(yieldable ? 1 : 0);
}

/* ------------- wrap: closure that resumes and rethrows ------------- */

static Value co_wrap_call(struct VM *vm, int argc, Value *argv) {
    /* For __call, argv[0] is the wrapper table (self). */
    if (argc < 1 || argv[0].tag != VAL_TABLE) return V_nil();

    Value wrapper = argv[0];
    Value co_val;
    if (!tbl_get_public(wrapper.as.t, V_str_from_c("co"), &co_val)) return V_nil();

    /* Build resume args: (co_val, user_args...) */
    int user_n = (argc >= 1) ? (argc - 1) : 0;
    int total  = 1 + user_n;
    Value *res_argv = total ? (Value*)malloc(sizeof(Value) * (size_t)total) : NULL;
    if (res_argv) {
        res_argv[0] = co_val;
        for (int i = 0; i < user_n; i++) res_argv[1 + i] = argv[1 + i];
    }

    Value rr = co_resume(vm, total, res_argv);
    if (res_argv) free(res_argv);

    /* rr is tuple: [1]=bool ok, [2..]=values or error msg */
    Value ok;
    if (rr.tag != VAL_TABLE || !tbl_get_public(rr.as.t, V_int(1), &ok) || ok.tag != VAL_BOOL)
        return V_nil();

    if (!ok.as.b) {
        Value err;
        if (tbl_get_public(rr.as.t, V_int(2), &err)) {
            vm_raise(vm, (err.tag == VAL_STR) ? err : V_str_from_c("coroutine error"));
        }
        vm_raise(vm, V_str_from_c("coroutine error"));
    }

    /* return first value to match your single-return expression behavior */
    Value v;
    if (tbl_get_public(rr.as.t, V_int(2), &v)) return v;
    return V_nil();
}

static Value co_wrap(struct VM *vm, int argc, Value *argv) {
    if (argc < 1 || !co_is_callable(argv[0])) {
        return vm_error_simple(vm, "coroutine.wrap: function expected");
    }

    /* Create coroutine */
    Value co_tbl = co_create(vm, 1, argv);
    if (co_tbl.tag != VAL_TABLE) return V_nil();

    /* wrapper is a table with metatable { __call = co_wrap_call }, and holds 'co' */
    Value wrapper = V_table();
    tbl_set_public(wrapper.as.t, V_str_from_c("co"), co_tbl);

    Value mt = V_table();
    Value c; c.tag = VAL_CFUNC; c.as.cfunc = co_wrap_call;
    tbl_set_public(mt.as.t, V_str_from_c("__call"), c);
    /* your VM uses a hidden metatable slot "_mt" */
    tbl_set_public(wrapper.as.t, V_str_from_c("_mt"), mt);

    return wrapper;
}

/* ---------------------------
 * Library registration
 * --------------------------- */

void register_coroutine_lib(VM *vm) {
    ensure_main_coroutine(vm);

    Value coro = V_table();

    /* Core functions */
    tbl_set_public(coro.as.t, V_str_from_c("create"),
                   (Value){.tag=VAL_CFUNC, .as.cfunc=co_create});
    tbl_set_public(coro.as.t, V_str_from_c("resume"),
                   (Value){.tag=VAL_CFUNC, .as.cfunc=co_resume});
    tbl_set_public(coro.as.t, V_str_from_c("yield"),
                   (Value){.tag=VAL_CFUNC, .as.cfunc=co_yield});
    tbl_set_public(coro.as.t, V_str_from_c("status"),
                   (Value){.tag=VAL_CFUNC, .as.cfunc=co_status});
    tbl_set_public(coro.as.t, V_str_from_c("wrap"),
                   (Value){.tag=VAL_CFUNC, .as.cfunc=co_wrap});

    /* Lua 5.2+ functions */
    tbl_set_public(coro.as.t, V_str_from_c("running"),
                   (Value){.tag=VAL_CFUNC, .as.cfunc=co_running});

    /* Lua 5.3+ functions */
    tbl_set_public(coro.as.t, V_str_from_c("isyieldable"),
                   (Value){.tag=VAL_CFUNC, .as.cfunc=co_isyieldable});

    env_add_public(vm->env, "coroutine", coro, false);
}

/* ---------------------------
 * Cleanup function (call on VM shutdown)
 * --------------------------- */

void cleanup_coroutine_lib(void) {
    if (g_main_coroutine) {
        co_unref(g_main_coroutine);
        g_main_coroutine = NULL;
    }
    g_current_coroutine = NULL;
}
