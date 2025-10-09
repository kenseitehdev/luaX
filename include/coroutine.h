
#ifndef COROUTINE_H
#define COROUTINE_H

#include "interpreter.h"

typedef enum { CO_DEAD, CO_SUSPENDED, CO_RUNNING } CoStatus;
void register_coroutine_lib(struct VM *vm);
typedef struct Frame {
  AST *node;
  size_t pc;
  Env *env;
  /* optional: label map if you want goto inside coroutines */
  void *labels;
  size_t lab_count, lab_cap;
} Frame;

struct Coroutine {
  VM vm;           /* coroutine-local VM state */
  Frame *stack;
  int top, cap;
  CoStatus status;
  bool yield_flag;
};

Value V_coroutine(Coroutine *co);

/* public entry points used by builtins */
Coroutine *co_new_from_func(VM *parent, Func *fn, int argc, Value *argv);
int        co_run_trampoline(Coroutine *co);  /* run until yield/return */

/* builtin glue (to register into _G.coroutine) */
Value builtin_coroutine_create(VM*, int, Value*);
Value builtin_coroutine_resume(VM*, int, Value*);
Value builtin_coroutine_yield (VM*, int, Value*);
Value builtin_coroutine_status(VM*, int, Value*);
Value builtin_coroutine_wrap  (VM*, int, Value*);

#endif
