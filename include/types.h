// types.h
#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include "parser.h"  // Get AST and ASTVec from parser.h

/* Value tags */
typedef enum {
  VAL_NIL, VAL_BOOL, VAL_INT, VAL_NUM, VAL_STR, VAL_TABLE,
  VAL_COROUTINE,
  VAL_CFUNC,
  VAL_FUNC,
  VAL_MULTI
} ValTag;

/* Forward declare VM for CFunc */
typedef struct VM VM;
typedef struct Value Value;

/* String */
typedef struct Str {
  int len;
  char *data;
} Str;

/* Multi-return bundle */
typedef struct Multi {
  int count;
  Value *items;
} Multi;

/* C function pointer type */
typedef Value (*CFunc)(VM *vm, int argc, Value *argv);

/* Value - define BEFORE TableEntry since TableEntry needs it */
struct Value {
  ValTag tag;
  union {
    long long i;
    double n;
    int b;
    Str *s;
    struct Table *t;
    CFunc cfunc;
    struct Func *fn;
    Multi *m;
  } as;
};

/* Now TableEntry can use Value by value */
typedef struct TableEntry {
  Value key;
  Value val;
  struct TableEntry *next;
} TableEntry;

/* Table */
typedef struct Table {
  int cap;
  TableEntry **buckets;
} Table;

/* Environment */
typedef struct CloseReg {
  int slot;
  bool open;
} CloseReg;

typedef struct Env {
  struct Env *parent;
  int count, cap;
  char **names;
  Value *vals;
  bool *is_local;
  CloseReg *closers;
  int ccount, ccap;
} Env;

/* Function closure */
typedef struct Func {
  ASTVec params;  // ASTVec comes from parser.h
  bool vararg;
  AST *body;      // AST comes from parser.h
  Env *env;
} Func;

/* Coroutine resume point */
typedef struct CoResumePoint {
  AST *blk;
  size_t pc;
} CoResumePoint;

/* Coroutine (forward declared) */
typedef struct Coroutine Coroutine;

/* VM */
struct VM {
  Env *env;
  bool break_flag;
  bool has_ret;
  Value ret_val;
  void *err_frame;
  Value err_obj;
  int top;
  Value stack[256];
  bool pending_goto;
  const char *goto_label;
  int has_exception;
  Value last_exception;
  bool co_yielding;
  Value co_yield_vals;
  CoResumePoint co_point;
  Env *co_call_env;
  Coroutine *active_co;
};

/* GC types */
typedef enum {
  GC_MODE_INCREMENTAL = 0,
  GC_MODE_GENERATIONAL = 1
} GCMode;

typedef struct GCShim {
  int running;
  GCMode mode;
  int pause;
  int stepmul;
  int stepsize_kb;
  int minormul;
  int majormul;
  unsigned tick;
} GCShim;

/* Global GC shim instance - declare extern here, define in one .c file */
extern GCShim g_gc;

#endif /* TYPES_H */
