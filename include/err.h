#ifndef ERR_H
#define ERR_H

#include <setjmp.h>
#include <stdlib.h>
#include "env.h"  /* for Env, VM, Value */
#include "interpreter.h"
#include "table.h"


typedef struct ErrFrame {
  jmp_buf jb;
  struct ErrFrame *prev;
  struct Env *env_at_push;
} ErrFrame;

void vm_err_push(struct VM *vm, ErrFrame *f);
void vm_err_pop(struct VM *vm);
void vm_raise(struct VM *vm, Value err);
#endif
