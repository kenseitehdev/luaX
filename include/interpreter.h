// interpreter.h
#ifndef INTERPRETER_H
#define INTERPRETER_H
#include <math.h>
#include <stdbool.h>
#include "parser.h"  /* for AST, ASTKind, ASTVec, etc. */
typedef struct Coroutine Coroutine; /* fwd so VM can refer to it */

typedef enum {
  VAL_NIL, VAL_BOOL, VAL_INT, VAL_NUM, VAL_STR, VAL_TABLE,
  VAL_COROUTINE,
  VAL_CFUNC,          /* native/builtin C function */
  VAL_FUNC,           /* user-defined Lua function (closure) */
  VAL_MULTI           /* multiple-return bundle */
} ValTag;

struct VM; /* fwd */

/* Forward declarations */
typedef struct Str Str;
typedef struct Table Table;
typedef struct TableEntry TableEntry;
typedef struct Func Func;
typedef struct Multi Multi;

typedef struct Value Value;
typedef Value (*CFunc)(struct VM *vm, int argc, Value *argv);
struct VM *vm_create_repl(void);
void exec_stmt_repl(struct VM *vm, AST *n);
/* Strings */
struct Str {
  int   len;
  char *data;
};

/* Multi-return bundle */
struct Multi {
  int count;
  Value *items;
};

/* Value */
struct Value {
  ValTag tag;
  union {
    long long i;
    double    n;
    int       b;
    Str      *s;
    Table    *t;
    CFunc     cfunc;
    Func     *fn;   /* VAL_FUNC */
    Multi    *m;    /* VAL_MULTI */
  } as;
};

/* Tables */
struct TableEntry {
  Value key;
  Value val;
  TableEntry *next;
};

struct Table {
  int cap;
  TableEntry **buckets;
};

/* Closure */
struct Func {
  ASTVec params;     /* identifiers */
  bool   vararg;
  AST   *body;       /* a block AST */
  struct Env *env;   /* captured lexical env */
};

/* ===== to-be-closed locals support =====
   Each Env keeps a LIFO of registrations; on scope exit or unwind,
   the interpreter will call __close(var, err) in reverse order. */
typedef struct CloseReg {
  int  slot;  /* index into Env->vals for the <close> variable */
  bool open;  /* still needs closing? */
} CloseReg;

typedef struct Env {
  struct Env *parent;
  int count, cap;
  char **names;
  Value *vals;
  bool  *is_local;

  /* <close> tracking */
  CloseReg *closers;
  int ccount, ccap;
} Env;

/* ====== Yield plumbing for statement-boundary coroutines ====== */
typedef struct {
  AST   *blk;      /* block we were executing */
  size_t pc;       /* next statement index to run */
} CoResumePoint;

typedef struct VM {
  Env *env;        /* current lexical env */
  bool break_flag; /* used by loops */
  bool has_ret;
  Value ret_val;
  void *err_frame;
  Value err_obj;
  int top;
  Value stack[256];
  /* goto plumbing across nested blocks */
  bool        pending_goto;
  const char *goto_label;
  int has_exception;
  Value last_exception;
  /* ---- coroutine yield plumbing ---- */
  bool  co_yielding;       /* set by coroutine.yield(...) */
  Value co_yield_vals;     /* a table [1..n] of yielded values */
  CoResumePoint co_point;  /* where to resume inside a block */
  Env  *co_call_env;       /* the coroutine's active call env (saved across yields) */
  Coroutine *active_co;    /* which coroutine (if any) is running on this VM */
} VM;

/* API */
int interpret(AST *root);

/* Convenience constructors */
Value V_nil(void);
Value V_bool(bool b);
Value V_int(long long x);
Value V_num(double x);
Value V_str_from_c(const char *s);
Value V_table(void);
/* === cross-TU helpers so libs like coroutine.c can interact with the VM === */
void  tbl_set_public(struct Table *t, Value key, Value val);
int   tbl_get_public(struct Table *t, Value key, Value *out);
void  env_add_public(struct Env *e, const char *name, Value v, bool is_local);
Value call_any_public(struct VM *vm, Value cal, int argc, Value *argv);

/* optional: register standard libs implemented in other files */
/* VM error raise (longjmp). Usable from other modules (coroutines, libs). */
void vm_raise(struct VM *vm, Value err);

#ifndef LUA_PLUS_MAX_LOOP_ITERS
#define LUA_PLUS_MAX_LOOP_ITERS 10000000  
#endif
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void vm_gc_collect(struct VM *vm) { (void)vm; }

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void vm_gc_stop(struct VM *vm) { (void)vm; }

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void vm_gc_restart(struct VM *vm) { (void)vm; }

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
int vm_gc_isrunning(struct VM *vm) { (void)vm; return 0; }

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
int vm_gc_step(struct VM *vm, int kb) { (void)vm; (void)kb; return 0; } /* return 1 if cycle finished */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
int vm_gc_setpause(struct VM *vm, int pause) { (void)vm; (void)pause; return 0; } /* return old */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
int vm_gc_setstepmul(struct VM *vm, int mul) { (void)vm; (void)mul; return 0; }   /* return old */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void vm_gc_set_incremental(struct VM *vm, int pause, int stepmul, int stepsize_kb) {
  (void)vm; (void)pause; (void)stepmul; (void)stepsize_kb;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void vm_gc_set_generational(struct VM *vm, int minormul, int majormul) {
  (void)vm; (void)minormul; (void)majormul;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
size_t vm_gc_total_bytes(struct VM *vm) { (void)vm; return 0; }
/* fwd decls we actually use before definition */
struct Func; 
struct Table; 
struct Env; 
struct VM; 
typedef struct TableEntry TableEntry;

typedef enum { GC_MODE_INCREMENTAL = 0, GC_MODE_GENERATIONAL = 1 } GCMode;
typedef struct {
  int running;
  GCMode mode;
  int pause;          /* % */
  int stepmul;       /* % */
  int stepsize_kb;   /* for incremental */
  int minormul;      /* for generational */
  int majormul;      /* for generational */
  unsigned tick;     /* fake progress so step() sometimes returns true */
} GCShim;

static GCShim g_gc = {
  1, GC_MODE_INCREMENTAL, 200, 200, 64, 200, 200, 0
};
/* fwd helpers that are defined later in this file */
Value mm_of(Value v, const char *name);
static int   try_bin_mm(struct VM *vm, const char *mm, Value a, Value b, Value *out);
static int   try_un_mm (struct VM *vm, const char *mm, Value a, Value *out);
static Value eval_index(VM *vm, Value table, Value key);
static void  assign_index(VM *vm, Value table, Value key, Value val);

/* from other compilation units */
static Value call_function(VM *vm, Func *fn, int argc, Value *argv);
void register_coroutine_lib(VM *vm);

void  tbl_set(struct Table *t, Value key, Value val);
int   tbl_get(struct Table *t, Value key, Value *out);
void  env_add(struct Env *e, const char *name, Value v, bool is_local);
Value call_any(struct VM *vm, Value cal, int argc, Value *argv);

void register_math_lib(struct VM *vm);
void register_string_lib(struct VM *vm);
void register_table_lib(struct VM *vm);
void register_utf8_lib(struct VM *vm);
void register_os_lib(struct VM *vm);
void register_io_lib(struct VM *vm);
void register_debug_lib(struct VM *vm);
void register_random_lib(struct VM *vm);
void register_date_lib(struct VM *vm);
void register_request_lib(struct VM *vm);
void register_package_lib(VM *vm);
void register_exception_lib(VM *vm);

int as_truthy(Value v);
/* public adapters for libraries implemented in other files */
void  tbl_set_public(struct Table *t, Value key, Value val);
int   tbl_get_public(struct Table *t, Value key, Value *out);
void  env_add_public(struct Env *e, const char *name, Value v, bool is_local);
Value call_any_public(struct VM *vm, Value cal, int argc, Value *argv);
void shim_collect(struct VM *vm);
void shim_stop(struct VM *vm);
void shim_restart(struct VM *vm);
int  shim_isrunning(struct VM *vm);
int  shim_step(struct VM *vm, int kb);
int  shim_setpause(struct VM *vm, int pause);
int  shim_setstepmul(struct VM *vm, int mul);
void shim_set_generational(struct VM *vm, int minormul, int majormul);
int to_int_val(Value v, int dflt);
void shim_set_incremental(struct VM *vm, int pause, int stepmul, int stepsize_kb);
void register_coroutine_lib(struct VM *vm);
void register_async_lib(struct VM *vm);
void register_class_lib(struct VM *vm);
void register_libs(struct VM *vm);  
/* Iterate through table entries - callback gets called for each key/value pair */
typedef void (*TableIterCallback)(Value key, Value val, void *userdata);
void tbl_foreach_public(struct Table *t, TableIterCallback callback, void *userdata);
void load_packages();
    char path_buf[2048];

#endif /* INTERPRETER_H */
