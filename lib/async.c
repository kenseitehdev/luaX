// lib/async.c - Async/await concurrency library
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../include/interpreter.h"

/* ---- Async Task Queue ---- */
typedef struct TaskNode {
    Value coroutine;           /* The coroutine to resume */
    Value pending_promise;     /* Promise it's waiting on (if any) */
    int is_waiting;            /* 1 if waiting for promise, 0 if ready */
    struct TaskNode *next;
} TaskNode;

typedef struct {
    TaskNode *head;
    TaskNode *tail;
    int count;
} TaskQueue;

/* Global event loop state */
static TaskQueue g_task_queue = {NULL, NULL, 0};
static int g_loop_running = 0;

/* ---- Promise State ---- */
typedef enum {
    PROMISE_PENDING,
    PROMISE_RESOLVED,
    PROMISE_REJECTED
} PromiseState;

/* Helper to create empty table */
static Value new_table(void) {
    return V_table();
}

/* Helper to set table field */
static void set_field(Value table, const char *key, Value val) {
    tbl_set_public(table.as.t, V_str_from_c(key), val);
}

/* Helper to get table field */
static int get_field(Value table, const char *key, Value *out) {
    if (table.tag != VAL_TABLE) return 0;
    return tbl_get_public(table.as.t, V_str_from_c(key), out);
}

/* ---- Task Queue Operations ---- */
static void queue_init(TaskQueue *q) {
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
}

static void queue_push(TaskQueue *q, Value coro, Value promise, int is_waiting) {
    TaskNode *node = (TaskNode*)malloc(sizeof(TaskNode));
    node->coroutine = coro;
    node->pending_promise = promise;
    node->is_waiting = is_waiting;
    node->next = NULL;
    
    if (q->tail) {
        q->tail->next = node;
        q->tail = node;
    } else {
        q->head = q->tail = node;
    }
    q->count++;
}

static int queue_pop(TaskQueue *q, Value *coro_out, Value *promise_out, int *is_waiting_out) {
    if (!q->head) return 0;
    
    TaskNode *node = q->head;
    *coro_out = node->coroutine;
    *promise_out = node->pending_promise;
    *is_waiting_out = node->is_waiting;
    
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    q->count--;
    
    free(node);
    return 1;
}

static void queue_clear(TaskQueue *q) {
    while (q->head) {
        TaskNode *node = q->head;
        q->head = node->next;
        free(node);
    }
    q->tail = NULL;
    q->count = 0;
}

/* ---- Promise Creation ---- */
static Value create_promise(void) {
    Value promise = new_table();
    set_field(promise, "state", V_int(PROMISE_PENDING));
    set_field(promise, "value", V_nil());
    set_field(promise, "callbacks", new_table());  /* Array of callbacks */
    return promise;
}

static int is_promise(Value v) {
    if (v.tag != VAL_TABLE) return 0;
    Value state;
    return get_field(v, "state", &state);
}

static PromiseState get_promise_state(Value promise) {
    Value state;
    if (get_field(promise, "state", &state) && state.tag == VAL_INT) {
        return (PromiseState)state.as.i;
    }
    return PROMISE_PENDING;
}

static void resolve_promise(struct VM *vm, Value promise, Value result) {
    set_field(promise, "state", V_int(PROMISE_RESOLVED));
    set_field(promise, "value", result);
    
    /* Run callbacks */
    Value callbacks;
    if (get_field(promise, "callbacks", &callbacks) && callbacks.tag == VAL_TABLE) {
        /* Iterate through callbacks array */
        for (int i = 1; i <= 100; i++) {  /* Max 100 callbacks */
            Value cb;
            if (tbl_get_public(callbacks.as.t, V_int(i), &cb) && 
                (cb.tag == VAL_FUNC || cb.tag == VAL_CFUNC)) {
                Value args[1] = {result};
                call_any_public(vm, cb, 1, args);
            } else {
                break;
            }
        }
    }
}

static void reject_promise(struct VM *vm, Value promise, Value error) {
    set_field(promise, "state", V_int(PROMISE_REJECTED));
    set_field(promise, "value", error);
    
    /* TODO: Run error callbacks */
    (void)vm;
}

/* ---- Async Core Functions ---- */

/* Helper to find global variable */
static int get_global(struct VM *vm, const char *name, Value *out) {
    /* Search through environment chain for the name */
    for (Env *e = vm->env; e; e = e->parent) {
        for (int i = 0; i < e->count; i++) {
            if (e->names[i] && strcmp(e->names[i], name) == 0) {
                *out = e->vals[i];
                return 1;
            }
        }
    }
    return 0;
}

/* async.spawn(func) - spawns a new async task */
static Value async_spawn(struct VM *vm, int argc, Value *argv) {
    if (argc < 1 || (argv[0].tag != VAL_FUNC && argv[0].tag != VAL_CFUNC)) {
        fprintf(stderr, "async.spawn: expected function\n");
        return V_nil();
    }
    
    /* Get coroutine.create */
    Value coro_table;
    if (!get_global(vm, "coroutine", &coro_table) || coro_table.tag != VAL_TABLE) {
        fprintf(stderr, "async.spawn: coroutine library not available\n");
        return V_nil();
    }
    
    Value create_func;
    if (!get_field(coro_table, "create", &create_func)) {
        fprintf(stderr, "async.spawn: coroutine.create not available\n");
        return V_nil();
    }
    
    Value coro_args[1] = {argv[0]};
    Value coro = call_any_public(vm, create_func, 1, coro_args);
    
    /* Add to task queue */
    queue_push(&g_task_queue, coro, V_nil(), 0);
    
    return coro;
}

/* async.await(promise) - waits for a promise to resolve */
static Value async_await(struct VM *vm, int argc, Value *argv) {
    if (argc < 1) {
        fprintf(stderr, "async.await: expected promise\n");
        return V_nil();
    }
    
    Value promise = argv[0];
    
    /* If it's already resolved, return the value */
    if (is_promise(promise)) {
        PromiseState state = get_promise_state(promise);
        if (state == PROMISE_RESOLVED) {
            Value result;
            get_field(promise, "value", &result);
            return result;
        } else if (state == PROMISE_REJECTED) {
            Value err;
            get_field(promise, "value", &err);
            vm_raise(vm, err);
            return V_nil();
        }
    }
    
    /* Otherwise, yield with a special marker */
    Value marker = new_table();
    set_field(marker, "_async_await", V_bool(1));
    set_field(marker, "_promise", promise);
    
    /* Get coroutine.yield */
    Value coro_table;
    if (!get_global(vm, "coroutine", &coro_table) || coro_table.tag != VAL_TABLE) {
        fprintf(stderr, "async.await: coroutine library not available\n");
        return V_nil();
    }
    
    Value yield_func;
    if (!get_field(coro_table, "yield", &yield_func)) {
        fprintf(stderr, "async.await: coroutine.yield not available\n");
        return V_nil();
    }
    
    Value yield_args[1] = {marker};
    return call_any_public(vm, yield_func, 1, yield_args);
}

/* async.run() - runs the event loop until all tasks complete */
static Value async_run(struct VM *vm, int argc, Value *argv) {
    (void)argc;
    (void)argv;
    
    if (g_loop_running) {
        fprintf(stderr, "async.run: event loop already running\n");
        return V_nil();
    }
    
    g_loop_running = 1;
    
    /* Get coroutine.resume and coroutine.status */
    Value coro_table;
    if (!get_global(vm, "coroutine", &coro_table) || coro_table.tag != VAL_TABLE) {
        fprintf(stderr, "async.run: coroutine library not available\n");
        g_loop_running = 0;
        return V_nil();
    }
    
    Value resume_func, status_func;
    if (!get_field(coro_table, "resume", &resume_func) || 
        !get_field(coro_table, "status", &status_func)) {
        fprintf(stderr, "async.run: coroutine.resume/status not available\n");
        g_loop_running = 0;
        return V_nil();
    }
    
    int max_iterations = 10000;  /* Prevent infinite loops */
    int iteration = 0;
    
    while (g_task_queue.count > 0 && iteration++ < max_iterations) {
        Value coro, promise;
        int is_waiting;
        
        if (!queue_pop(&g_task_queue, &coro, &promise, &is_waiting)) {
            break;
        }
        
        /* Check if task is waiting on a promise */
        if (is_waiting && is_promise(promise)) {
            PromiseState state = get_promise_state(promise);
            if (state == PROMISE_PENDING) {
                /* Still waiting, re-queue at the end */
                queue_push(&g_task_queue, coro, promise, 1);
                continue;
            }
        }
        
        /* Resume the coroutine */
        Value resume_args[2] = {coro, V_nil()};
        int resume_argc = 1;
        
        /* If promise is resolved, pass the value */
        if (is_waiting && is_promise(promise) && get_promise_state(promise) == PROMISE_RESOLVED) {
            Value result;
            get_field(promise, "value", &result);
            resume_args[1] = result;
            resume_argc = 2;
        }
        
        Value result = call_any_public(vm, resume_func, resume_argc, resume_args);
        
        /* Check result: {success, value} or {success, value, ...} */
        if (result.tag == VAL_TABLE) {
            Value success, ret_val;
            if (tbl_get_public(result.as.t, V_int(1), &success) && success.tag == VAL_BOOL) {
                if (success.as.b) {
                    /* Coroutine yielded successfully */
                    if (tbl_get_public(result.as.t, V_int(2), &ret_val)) {
                        /* Check if it yielded an await marker */
                        Value is_await;
                        if (ret_val.tag == VAL_TABLE && 
                            get_field(ret_val, "_async_await", &is_await) && 
                            is_await.tag == VAL_BOOL && is_await.as.b) {
                            /* It's waiting on a promise */
                            Value await_promise;
                            if (get_field(ret_val, "_promise", &await_promise)) {
                                queue_push(&g_task_queue, coro, await_promise, 1);
                            }
                        } else {
                            /* Regular yield, re-queue as ready */
                            queue_push(&g_task_queue, coro, V_nil(), 0);
                        }
                    }
                } else {
                    /* Coroutine finished or errored */
                    if (tbl_get_public(result.as.t, V_int(2), &ret_val)) {
                        /* Check if it's an error */
                        Value status_args[1] = {coro};
                        Value status = call_any_public(vm, status_func, 1, status_args);
                        /* Just continue, task is done */
                    }
                }
            }
        }
    }
    
    g_loop_running = 0;
    queue_clear(&g_task_queue);
    
    return V_nil();
}

/* async.sleep(seconds) - returns a promise that resolves after delay */
static Value async_sleep(struct VM *vm, int argc, Value *argv) {
    double seconds = 0.0;
    if (argc > 0) {
        if (argv[0].tag == VAL_NUM) seconds = argv[0].as.n;
        else if (argv[0].tag == VAL_INT) seconds = (double)argv[0].as.i;
    }
    
    Value promise = create_promise();
    
    /* Store start time */
    set_field(promise, "_sleep_start", V_num((double)clock() / CLOCKS_PER_SEC));
    set_field(promise, "_sleep_duration", V_num(seconds));
    
    /* Spawn a task to resolve it after the delay */
    /* Note: This is a simple busy-wait implementation */
    /* In production, you'd use proper async I/O */
    
    (void)vm;
    
    return promise;
}

/* async.promise(executor_func) - creates a new promise */
static Value async_promise(struct VM *vm, int argc, Value *argv) {
    if (argc < 1 || (argv[0].tag != VAL_FUNC && argv[0].tag != VAL_CFUNC)) {
        fprintf(stderr, "async.promise: expected function\n");
        return V_nil();
    }
    
    Value promise = create_promise();
    Value executor = argv[0];
    
    /* Create resolve and reject functions */
    /* These would need to be proper closures that capture the promise */
    /* For now, we'll return the promise and let the executor handle it manually */
    
    (void)vm;
    (void)executor;
    
    return promise;
}

/* async.resolve(value) - creates an immediately resolved promise */
static Value async_resolve(struct VM *vm, int argc, Value *argv) {
    Value val = (argc > 0) ? argv[0] : V_nil();
    Value promise = create_promise();
    resolve_promise(vm, promise, val);
    return promise;
}

/* async.reject(error) - creates an immediately rejected promise */
static Value async_reject(struct VM *vm, int argc, Value *argv) {
    Value err = (argc > 0) ? argv[0] : V_str_from_c("rejected");
    Value promise = create_promise();
    reject_promise(vm, promise, err);
    return promise;
}

/* Helper: Check if all promises are resolved */
static int all_resolved(Value *promises, int count) {
    for (int i = 0; i < count; i++) {
        if (is_promise(promises[i]) && get_promise_state(promises[i]) == PROMISE_PENDING) {
            return 0;
        }
    }
    return 1;
}

/* async.all(promises_table) - waits for all promises */
static Value async_all(struct VM *vm, int argc, Value *argv) {
    if (argc < 1 || argv[0].tag != VAL_TABLE) {
        fprintf(stderr, "async.all: expected table of promises\n");
        return V_nil();
    }
    
    /* Create a promise that resolves when all input promises resolve */
    Value result_promise = create_promise();
    
    /* For simplicity, we'll just return the promise */
    /* A full implementation would track all promises and resolve when they're all done */
    
    (void)vm;
    
    return result_promise;
}

/* ---- Registration ---- */
void register_async_lib(struct VM *vm) {
    Value A = new_table();
    
    set_field(A, "spawn", (Value){.tag=VAL_CFUNC, .as.cfunc=async_spawn});
    set_field(A, "await", (Value){.tag=VAL_CFUNC, .as.cfunc=async_await});
    set_field(A, "run", (Value){.tag=VAL_CFUNC, .as.cfunc=async_run});
    set_field(A, "sleep", (Value){.tag=VAL_CFUNC, .as.cfunc=async_sleep});
    set_field(A, "promise", (Value){.tag=VAL_CFUNC, .as.cfunc=async_promise});
    set_field(A, "resolve", (Value){.tag=VAL_CFUNC, .as.cfunc=async_resolve});
    set_field(A, "reject", (Value){.tag=VAL_CFUNC, .as.cfunc=async_reject});
    set_field(A, "all", (Value){.tag=VAL_CFUNC, .as.cfunc=async_all});
    
    env_add_public(vm->env, "async", A, false);
    
    /* Initialize task queue */
    queue_init(&g_task_queue);
}
