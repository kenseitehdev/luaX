#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h> 
#include <ctype.h>
#include <setjmp.h> 
#include "../include/parser.h"
#include "../include/builtins.h"
#include "../include/lexer.h"
#include "../include/err.h"
#define STACK_MAX 256

void tbl_foreach_public(struct Table *t, TableIterCallback callback, void *userdata) {
    if (!t || !callback) return;
    for (int i = 0; i < t->cap; i++) {
        for (TableEntry *e = t->buckets[i]; e; e = e->next) {
            callback(e->key, e->val, userdata);
        }
    }
}
static Value vm_pop(VM *vm) {
    if (vm->top < 0) {
        vm_raise(vm, V_str_from_c("stack underflow"));
    }
    return vm->stack[vm->top--];
}

static void vm_push(VM *vm, Value v) {
    if (vm->top >= STACK_MAX - 1) {
        vm_raise(vm, V_str_from_c("stack overflow"));
    }
    vm->stack[++vm->top] = v;
}
static void *xmalloc(size_t n){ void *p=malloc(n); if(!p){fprintf(stderr,"OOM\n"); exit(1);} return p; }
static char *xstrdup(const char*s){ if(!s) s=""; size_t n=strlen(s)+1; char *p=xmalloc(n); memcpy(p,s,n); return p; }
static Value op_len(Value v){
  if (v.tag == VAL_STR)  return V_int(v.as.s->len);
  if (v.tag == VAL_TABLE){
    long long n = 0, i = 1; Value out;
    while (tbl_get(v.as.t, V_int(i), &out)) { n++; i++; }
    return V_int(n);
  }
  return V_int(0);
}
static FILE* open_string_as_FILE(const char *code) {
    if (!code) code = "";
#if defined(_GNU_SOURCE) || defined(__GLIBC__)
    FILE *f = fmemopen((void*)code, strlen(code), "r");
    if (f) return f;
#endif
    FILE *f = tmpfile();
    if (!f) return NULL;
    size_t len = strlen(code);
    if (len && fwrite(code, 1, len, f) != len) { fclose(f); return NULL; }
    rewind(f);
    return f;
}
Value V_nil(void){ Value v={.tag=VAL_NIL}; return v; }
Value V_bool(bool b){ Value v={.tag=VAL_BOOL}; v.as.b=b?1:0; return v; }
Value V_int(long long x){ Value v={.tag=VAL_INT}; v.as.i=x; return v; }
Value V_num(double x){ Value v={.tag=VAL_NUM}; v.as.n=x; return v; }
static Str *Str_new_len(const char *s,int len){ Str *st=xmalloc(sizeof(*st)); st->len=len; st->data=xmalloc(len+1); if(s&&len) memcpy(st->data,s,len); st->data[len]='\0'; return st; }
Value V_str_from_c(const char *s){ if(!s) s=""; return (Value){.tag=VAL_STR,.as.s=Str_new_len(s,(int)strlen(s))}; }
static unsigned long long hash_mix(unsigned long long x){ x ^= x>>33; x*=0xff51afd7ed558ccdULL; x ^= x>>33; x*=0xc4ceb9fe1a85ec53ULL; x ^= x>>33; return x; }
static unsigned long long hash_value(Value v){
  switch(v.tag){
    case VAL_NIL:  return 1469598103934665603ULL;
    case VAL_BOOL: return v.as.b?0x9e3779b97f4a7c15ULL:0x51d7348a2f0f3ad9ULL;
    case VAL_INT: { 
      union { double d; unsigned long long u; } u = { .d = (double)v.as.i };
      return hash_mix(u.u);
    }
    case VAL_NUM: {
      union{ double d; unsigned long long u; }u={.d=v.as.n};
      return hash_mix(u.u);
    }
    case VAL_STR: {
      unsigned long long h=1469598103934665603ULL;
      for(int i=0;i<v.as.s->len;i++){ h^=(unsigned char)v.as.s->data[i]; h*=1099511628211ULL; }
      return h;
    }
    case VAL_TABLE: return (unsigned long long)(uintptr_t)v.as.t;
    case VAL_FUNC:  return (unsigned long long)(uintptr_t)v.as.fn;
    case VAL_CFUNC: return (unsigned long long)(uintptr_t)v.as.cfunc;
    default: return 0x12345678ULL;
  }
}
static Table *tbl_new(void){
  Table *t=xmalloc(sizeof(*t));
  t->cap=32; t->buckets=xmalloc(sizeof(TableEntry*)*t->cap);
  for(int i=0;i<t->cap;i++) t->buckets[i]=NULL;
  return t;
}
Value V_table(void){ return (Value){.tag=VAL_TABLE,.as.t=tbl_new()}; }
int value_equal(Value a, Value b){
  if(a.tag!=b.tag){
    if((a.tag==VAL_INT&&b.tag==VAL_NUM)) return (double)a.as.i==b.as.n;
    if((a.tag==VAL_NUM&&b.tag==VAL_INT)) return a.as.n==(double)b.as.i;
    return 0;
  }
  switch(a.tag){
    case VAL_NIL: return 1;
    case VAL_BOOL: return a.as.b==b.as.b;
    case VAL_INT: return a.as.i==b.as.i;
    case VAL_NUM: return a.as.n==b.as.n;
    case VAL_STR: return a.as.s->len==b.as.s->len && memcmp(a.as.s->data,b.as.s->data,a.as.s->len)==0;
    default: return a.as.t==b.as.t;
  }
}
void tbl_set(Table *t, Value key, Value val){
  unsigned long long h=hash_value(key);
  int idx = (int)(h % t->cap);
  for(TableEntry *e=t->buckets[idx]; e; e=e->next){
    if(value_equal(e->key,key)){ e->val=val; return; }
  }
  TableEntry *ne=xmalloc(sizeof(*ne));
  ne->key=key; ne->val=val; ne->next=t->buckets[idx];
  t->buckets[idx]=ne;
}
int tbl_get(Table *t, Value key, Value *out){
  unsigned long long h=hash_value(key);
  int idx = (int)(h % t->cap);
  for(TableEntry *e=t->buckets[idx]; e; e=e->next){
    if(value_equal(e->key,key)){ *out=e->val; return 1; }
  }
  return 0;
}
static Env *env_push(Env *parent){
  Env *e=xmalloc(sizeof(*e));
  e->parent=parent; e->count=0; e->cap=8;
  e->names=xmalloc(sizeof(char*)*e->cap);
  e->vals =xmalloc(sizeof(Value)*e->cap);
  e->is_local=xmalloc(sizeof(bool)*e->cap);
  e->closers = NULL;
  e->ccount  = 0;
  e->ccap    = 0;
  return e;
}
void env_add(Env *e, const char *name, Value v, bool is_local){
  if(e->count==e->cap){
    e->cap*=2;
    e->names=realloc(e->names,sizeof(char*)*e->cap);
    e->vals =realloc(e->vals ,sizeof(Value)*e->cap);
    e->is_local=realloc(e->is_local,sizeof(bool)*e->cap);
  }
  e->names[e->count]=xstrdup(name);
  e->vals [e->count]=v;
  e->is_local[e->count]=is_local;
  e->count++;
}
static int env_set(Env *e, const char *name, Value v){
  for(Env *cur=e; cur; cur=cur->parent){
    for(int i=0;i<cur->count;i++){
      if(strcmp(cur->names[i],name)==0){ cur->vals[i]=v; return 1; }
    }
  }
  return 0;
}
int env_get(Env *e, const char *name, Value *out){
  for(Env *cur=e; cur; cur=cur->parent){
    for(int i=0;i<cur->count;i++){
      if(strcmp(cur->names[i],name)==0){ *out=cur->vals[i]; return 1; }
    }
  }
  return 0;
}
static int env_find(Env *e, const char *name, Env **owner, int *slot){
  for(Env *cur=e; cur; cur=cur->parent){
    for(int i=0;i<cur->count;i++){
      if(strcmp(cur->names[i],name)==0){ if(owner)*owner=cur; if(slot)*slot=i; return 1; }
    }
  }
  return 0;
}
Env* env_root(Env *e){
  if(!e) return NULL;
  while(e->parent) e = e->parent;
  return e;
}
static void print_value(Value v){
  switch(v.tag){
    case VAL_NIL:   printf("nil"); break;
    case VAL_BOOL:  printf(v.as.b?"true":"false"); break;
    case VAL_INT:   printf("%lld", v.as.i); break;
    case VAL_NUM:   printf("%g", v.as.n); break;
    case VAL_STR:   fwrite(v.as.s->data,1,v.as.s->len,stdout); break;
    case VAL_TABLE: printf("table:%p", (void*)v.as.t); break;
    case VAL_CFUNC: printf("function:%p", (void*)v.as.cfunc); break;
    case VAL_FUNC:  printf("function:%p",  (void*)v.as.fn); break;
  }
}
static Str *to_string_buf(Value v){
  char tmp[64];
  switch(v.tag){
    case VAL_NIL:   return Str_new_len("nil", 3);
    case VAL_BOOL:  return v.as.b? Str_new_len("true",4):Str_new_len("false",5);
    case VAL_INT:   snprintf(tmp,sizeof(tmp),"%lld", v.as.i); return Str_new_len(tmp,(int)strlen(tmp));
    case VAL_NUM:   snprintf(tmp,sizeof(tmp),"%.17g", v.as.n); return Str_new_len(tmp,(int)strlen(tmp));
    case VAL_STR:   return Str_new_len(v.as.s->data, v.as.s->len);
    case VAL_TABLE: snprintf(tmp,sizeof(tmp),"table:%p",(void*)v.as.t); return Str_new_len(tmp,(int)strlen(tmp));
    case VAL_CFUNC: snprintf(tmp,sizeof(tmp),"function:%p",(void*)v.as.cfunc); return Str_new_len(tmp,(int)strlen(tmp));
    case VAL_FUNC:  snprintf(tmp,sizeof(tmp),"function:%p",(void*)v.as.fn); return Str_new_len(tmp,(int)strlen(tmp));
  }
  return Str_new_len("<unknown>",9);
}
int is_callable(Value v){
  return (v.tag == VAL_CFUNC) || (v.tag == VAL_FUNC);
}
int as_truthy(Value v){
  if(v.tag==VAL_NIL) return 0;
  if(v.tag==VAL_BOOL) return v.as.b!=0;
  return 1;
}
static double as_num(Value v){
  if(v.tag==VAL_INT) return (double)v.as.i;
  if(v.tag==VAL_NUM) return v.as.n;
  if(v.tag==VAL_BOOL) return v.as.b?1.0:0.0;
  return 0.0;
}
static long long as_int(Value v){
  if(v.tag==VAL_INT) return v.as.i;
  if(v.tag==VAL_NUM) return (long long)v.as.n;
  if(v.tag==VAL_BOOL) return v.as.b?1:0;
  return 0;
}
static inline Value V_cstr(const char *s){ return V_str_from_c(s); }
static Value mt_of(Value v){
  if (v.tag != VAL_TABLE) return V_nil();
  Value mt;
  if (tbl_get(v.as.t, V_cstr(MT_STORE), &mt) && mt.tag == VAL_TABLE) return mt;
  return V_nil();
}
Value mm_of(Value v, const char *name){
  Value mt = mt_of(v);
  if (mt.tag != VAL_TABLE) return V_nil();
  Value f;
  if (tbl_get(mt.as.t, V_cstr(name), &f)) return f;
  return V_nil();
}
static int try_bin_mm(struct VM *vm, const char *mm, Value a, Value b, Value *out){
  Value f = mm_of(a, mm);
  if (f.tag == VAL_NIL) f = mm_of(b, mm);
  if (f.tag != VAL_NIL){
    Value argv[2] = { a, b };
    *out = call_any(vm, f, 2, argv);
    return 1;
  }
  return 0;
}
static int try_un_mm(struct VM *vm, const char *mm, Value a, Value *out){
  Value f = mm_of(a, mm);
  if (f.tag != VAL_NIL){
    Value argv[1] = { a };
    *out = call_any(vm, f, 1, argv);
    return 1;
  }
  return 0;
}
Value call_any(VM *vm, Value cal, int argc, Value *argv) {
    if (cal.tag == VAL_CFUNC) return cal.as.cfunc(vm, argc, argv);
    if (cal.tag == VAL_FUNC)  return call_function(vm, cal.as.fn, argc, argv);

    Value f = mm_of(cal, "__call");
    if (f.tag != VAL_NIL) {
        Value *args = NULL;
        int n = argc + 1;
        if (n > 0) args = (Value*)malloc(sizeof(Value) * (size_t)n);
        args[0] = cal;
        for (int i = 0; i < argc; i++) args[i+1] = argv[i];
        Value r = call_any(vm, f, n, args);
        if (args) free(args);
        return r;
    }

    const char *type_name = "unknown";
    switch(cal.tag) {
        case VAL_NIL:   type_name = "nil"; break;
        case VAL_BOOL:  type_name = "boolean"; break;
        case VAL_INT:   type_name = "number"; break;
        case VAL_NUM:   type_name = "number"; break;
        case VAL_STR:   type_name = "string"; break;
        case VAL_TABLE: type_name = "table"; break;
        case VAL_COROUTINE: type_name = "coroutine"; break;
        case VAL_CFUNC: type_name = "cfunction"; break;
        case VAL_FUNC:  type_name = "function"; break;
        case VAL_MULTI: type_name = "multi"; break;
    }

    // Build the error message
    char err_msg[512];
    snprintf(err_msg, sizeof(err_msg),
             "attempted to call a non-function: called a %s value", type_name);

    // Print a stack traceback using ErrFrame chain
    if (vm->err_frame) {
        ErrFrame *frame = (ErrFrame*)vm->err_frame;
        int depth = 0;
        snprintf(err_msg + strlen(err_msg), sizeof(err_msg) - strlen(err_msg),
                 "\nStack traceback:");
        while (frame) {
            snprintf(err_msg + strlen(err_msg), sizeof(err_msg) - strlen(err_msg),
                     "\n  [%d] env=%p", depth, frame->env_at_push);
            frame = frame->prev;
            depth++;
        }
    }

    vm_raise(vm, V_str_from_c(err_msg));
    return V_nil();
}
static Value op_concat(Value a, Value b){
  char tmpa[64], tmpb[64];
  const char *sa=NULL, *sb=NULL; int la=0, lb=0;
  if(a.tag==VAL_STR){ sa=a.as.s->data; la=a.as.s->len; }
  else { snprintf(tmpa,sizeof(tmpa),"%g", as_num(a)); sa=tmpa; la=(int)strlen(sa); }
  if(b.tag==VAL_STR){ sb=b.as.s->data; lb=b.as.s->len; }
  else { snprintf(tmpb,sizeof(tmpb),"%g", as_num(b)); sb=tmpb; lb=(int)strlen(sb); }
  Str *s=Str_new_len(NULL, la+lb);
  memcpy(s->data, sa, la); memcpy(s->data+la, sb, lb); s->data[la+lb]='\0';
  return (Value){.tag=VAL_STR,.as.s=s};
}
int to_int_val(Value v, int dflt){
  if (v.tag == VAL_INT) return (int)v.as.i;
  if (v.tag == VAL_NUM) return (int)v.as.n;
  return dflt;
}
void shim_collect(struct VM *vm){ vm_gc_collect(vm); g_gc.tick = 0; }
void shim_stop(struct VM *vm){ vm_gc_stop(vm); g_gc.running = 0; }
void shim_restart(struct VM *vm){ vm_gc_restart(vm); g_gc.running = 1; }
int  shim_isrunning(struct VM *vm){ int r = vm_gc_isrunning(vm); return r ? r : g_gc.running; }
int  shim_step(struct VM *vm, int kb){
  int done = vm_gc_step(vm, kb);
  if (done) return done;
  g_gc.tick++;
  return (g_gc.tick % 8 == 0) ? 1 : 0; 
}
int  shim_setpause(struct VM *vm, int pause){
  int old = vm_gc_setpause(vm, pause);
  if (old == 0) { old = g_gc.pause; if (pause > 0) g_gc.pause = pause; }
  return old;
}
int  shim_setstepmul(struct VM *vm, int mul){
  int old = vm_gc_setstepmul(vm, mul);
  if (old == 0) { old = g_gc.stepmul; if (mul > 0) g_gc.stepmul = mul; }
  return old;
}
void shim_set_incremental(struct VM *vm, int pause, int stepmul, int stepsize_kb){
  vm_gc_set_incremental(vm, pause, stepmul, stepsize_kb);
  g_gc.mode = GC_MODE_INCREMENTAL;
  if (pause > 0) g_gc.pause = pause;
  if (stepmul > 0) g_gc.stepmul = stepmul;
  if (stepsize_kb > 0) g_gc.stepsize_kb = stepsize_kb;
}
void shim_set_generational(struct VM *vm, int minormul, int majormul){
  vm_gc_set_generational(vm, minormul, majormul);
  g_gc.mode = GC_MODE_GENERATIONAL;
  if (minormul > 0) g_gc.minormul = minormul;
  if (majormul > 0) g_gc.majormul = majormul;
}
static Value call_debug_traceback(struct VM *vm, Value msg, int level) {
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
Value builtin__G(struct VM *vm, int argc, Value *argv){
  (void)argc;(void)argv;
  Env *root = env_root(vm->env);
  Value t = V_table();
  for (int i=0;i<root->count;i++){
    tbl_set(t.as.t, V_str_from_c(root->names[i]), root->vals[i]);
  }
  return t;
}
Value builtin_rawequal(struct VM *vm, int argc, Value *argv){
  (void)vm;
  if (argc<2) return V_bool(0);
  return V_bool(value_equal(argv[0], argv[1]));
}
Value builtin_rawget(struct VM *vm, int argc, Value *argv){
  (void)vm;
  if (argc<2 || argv[0].tag!=VAL_TABLE) return V_nil();
  Value out;
  if (tbl_get(argv[0].as.t, argv[1], &out)) return out;
  return V_nil();
}
Value builtin_rawset(struct VM *vm, int argc, Value *argv){
  (void)vm;
  if (argc<3 || argv[0].tag!=VAL_TABLE) return V_nil();
  tbl_set(argv[0].as.t, argv[1], argv[2]);
  return argv[0];
}
Value builtin_next(struct VM *vm, int argc, Value *argv){
  (void)vm;
  if (argc<1 || argv[0].tag!=VAL_TABLE) return V_nil();
  Table *t = argv[0].as.t;
  int has_key = (argc>=2) && (argv[1].tag!=VAL_NIL);
  int found = !has_key;
  for (int bi=0; bi<t->cap; ++bi){
    for (TableEntry *e = t->buckets[bi]; e; e=e->next){
      if (!found){
        if (value_equal(e->key, argv[1])) { found = 1; }
        continue;
      } else {
        Value tup = V_table();
        tbl_set(tup.as.t, V_int(1), e->key);
        tbl_set(tup.as.t, V_int(2), e->val);
        return tup;
      }
    }
  }
  return V_nil();
}
Value builtin_pairs(struct VM *vm, int argc, Value *argv){
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_TABLE) return V_nil();
  Value mm = mm_of(argv[0], "__pairs");
  if (mm.tag != VAL_NIL) {
    Value res = call_any(vm, mm, 1, &argv[0]);
    if (res.tag == VAL_TABLE) return res;
  }
  Value triple = V_table();
  Value iter; iter.tag = VAL_CFUNC; iter.as.cfunc = builtin_next;
  tbl_set(triple.as.t, V_int(1), iter);
  tbl_set(triple.as.t, V_int(2), argv[0]);
  tbl_set(triple.as.t, V_int(3), V_nil());
  return triple;
}
Value ipairs_iter(struct VM *vm, int argc, Value *argv){
  (void)vm;
  if (argc < 2 || argv[0].tag != VAL_TABLE) return V_nil();
  long long i = 0;
  if (argv[1].tag == VAL_INT) i = argv[1].as.i;
  else if (argv[1].tag == VAL_NUM) i = (long long)argv[1].as.n;
  i += 1;
  Value val;
  if (tbl_get(argv[0].as.t, V_int(i), &val)) {
    Value pair = V_table();
    tbl_set(pair.as.t, V_int(1), V_int(i));
    tbl_set(pair.as.t, V_int(2), val);
    return pair;
  }
  return V_nil();
}
Value builtin_ipairs(struct VM *vm, int argc, Value *argv){
  (void)argc;
  if (argc < 1 || argv[0].tag != VAL_TABLE) return V_nil();
  Value triple = V_table();
  Value iter; iter.tag = VAL_CFUNC; iter.as.cfunc = ipairs_iter;
  tbl_set(triple.as.t, V_int(1), iter);
  tbl_set(triple.as.t, V_int(2), argv[0]);
  tbl_set(triple.as.t, V_int(3), V_int(0));
  return triple;
}
Value builtin_tonumber(struct VM* vm, int argc, Value* argv) {
  (void)vm;
  if (argc < 1) return V_nil();
  if (argv[0].tag == VAL_INT || argv[0].tag == VAL_NUM) return argv[0];
  if (argv[0].tag != VAL_STR) return V_nil();
  const char* s = argv[0].as.s->data;
  int len = argv[0].as.s->len;
  while (*s && isspace((unsigned char)*s)) { s++; len--; }
  while (len > 0 && isspace((unsigned char)s[len-1])) { len--; }
  if (len <= 0) return V_nil();
  if (argc >= 2 && argv[1].tag == VAL_INT) {
    int base = (int)argv[1].as.i;
    if (base < 2 || base > 36) return V_nil();
    int sign = 1, i = 0, saw_digit = 0;
    if (len > 0 && (s[0] == '+' || s[0] == '-')) { sign = (s[0] == '-') ? -1 : 1; s++; len--; }
    long long value = 0;
    for (i = 0; i < len; i++) {
      unsigned char c = (unsigned char)s[i];
      int d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (c >= 'A' && c <= 'Z') d = 10 + (c - 'A');
      else if (c >= 'a' && c <= 'z') d = 10 + (c - 'a');
      else return V_nil();
      if (d >= base) return V_nil();
      saw_digit = 1;
      value = value * base + d;
    }
    if (!saw_digit) return V_nil();
    if (sign < 0) value = -value;
    return V_int(value);
  } else {
    char *end = NULL;
    double d = strtod(s, &end);
    if (!end || end != s + len) return V_nil();
    return V_num(d);
  }
}
static Value tostring_default(Value v) {
  char buf[64];
  switch (v.tag) {
    case VAL_NIL:
      return V_str_from_c("nil");
    case VAL_BOOL:
      return V_str_from_c(v.as.b ? "true" : "false");
    case VAL_INT:
      snprintf(buf, sizeof(buf), "%lld", (long long)v.as.i);
      return V_str_from_c(buf);
    case VAL_NUM:
      snprintf(buf, sizeof(buf), "%.17g", v.as.n);
      return V_str_from_c(buf);
    case VAL_STR:
      return v;
    case VAL_TABLE:
      return V_str_from_c("table");
    case VAL_FUNC:
      return V_str_from_c("function");
    case VAL_CFUNC:
      return V_str_from_c("function");
#ifdef VAL_COROUTINE
    case VAL_COROUTINE:
      return V_str_from_c("thread");
#endif
    default:
      return V_str_from_c("<value>");
  }
}
static Value builtin_tostring(struct VM *vm, int argc, Value *argv){
  if (argc < 1) return V_str_from_c("");
  Value mm = mm_of(argv[0], "__tostring");
  if (mm.tag != VAL_NIL){
    Value s = call_any(vm, mm, 1, argv);
    if (s.tag == VAL_STR) return s;
  }
  return tostring_default(argv[0]);
}
static Value builtin_print(struct VM *vm, int argc, Value *argv){
  for (int i = 0; i < argc; i++) {
    if (i) printf("\t");
    Value s = builtin_tostring(vm, 1, &argv[i]);
    if (s.tag == VAL_STR) fwrite(s.as.s->data, 1, s.as.s->len, stdout);
    else print_value(argv[i]); 
  }
  printf("\n");
  return V_nil();
}
static Value builtin_type(struct VM *vm, int argc, Value *argv){
  (void)vm; if (argc<1) return V_str_from_c("nil");
  switch(argv[0].tag){
    case VAL_NIL: return V_str_from_c("nil");
    case VAL_BOOL: return V_str_from_c("boolean");
    case VAL_INT: case VAL_NUM: return V_str_from_c("number");
    case VAL_STR: return V_str_from_c("string");
    case VAL_TABLE: return V_str_from_c("table");
    case VAL_FUNC: case VAL_CFUNC: return V_str_from_c("function");
    default: return V_str_from_c("unknown");
  }
}
static Value builtin__VERSION(struct VM *vm, int argc, Value *argv){
  (void)vm;(void)argc;(void)argv;
  return V_str_from_c("LuaX 1.0");
}
static char* read_entire_file(const char *path, size_t *out_len){
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_len) *out_len = rd;
    return buf;
}
static AST* compile_chunk_from_FILE(FILE *fp){
  Token *toks = NULL; int count = 0, cap = 0;
  for (;;) {
    Token t = next(fp);
    if (count >= cap){ cap = cap? cap*2 : 64; toks = (Token*)realloc(toks, sizeof(Token)*cap); }
    toks[count++] = t;
    if (t.type == TOK_EOF) break;
  }
  Parser *p = parser_create(toks, count);
  ASTVec stmts = (ASTVec){0};
  while (parser_curr(p).type != TOK_EOF) {
    AST *s = statement(p);
    if (!s) break;
    if (parser_curr(p).type == TOK_EOF && p->had_error) break;
    astvec_push(&stmts, s);
  }
  AST *program = ast_make_block(stmts, count ? toks[count-1].line : 1);
  for (int i=0;i<count;i++) free(toks[i].lexeme);
  free(toks);
  parser_destroy(p);
  return program;
}
static Value builtin_load(struct VM *vm, int argc, Value *argv){
  if (argc<1 || argv[0].tag!=VAL_STR) return V_nil();
  FILE *fp = open_string_as_FILE(argv[0].as.s->data);
  if (!fp) return V_nil();
  AST *program = compile_chunk_from_FILE(fp);
  fclose(fp);
  Func *fn = xmalloc(sizeof(*fn)); memset(fn,0,sizeof(*fn));
  fn->params=(ASTVec){0}; fn->vararg=false; fn->body=program; fn->env=vm->env;
  Value v; v.tag=VAL_FUNC; v.as.fn=fn; return v;
}
static Value builtin_loadfile(struct VM *vm, int argc, Value *argv){
  if (argc<1 || argv[0].tag!=VAL_STR) return V_nil();
  size_t n=0; char *src = read_entire_file(argv[0].as.s->data, &n);
  if (!src) { fprintf(stderr,"[LuaX]: loadfile: cannot open '%s'\n", argv[0].as.s->data); return V_nil(); }
  FILE *fp = open_string_as_FILE(src);
  free(src);
  if (!fp) return V_nil();
  AST *program = compile_chunk_from_FILE(fp);
  fclose(fp);
  Func *fn = xmalloc(sizeof(*fn)); memset(fn,0,sizeof(*fn));
  fn->params=(ASTVec){0}; fn->vararg=false; fn->body=program; fn->env=vm->env;
  Value v; v.tag=VAL_FUNC; v.as.fn=fn; return v;
}
static Value builtin_pcall(struct VM *vm, int argc, Value *argv){
  if (argc < 1 || !is_callable(argv[0])) {
    Value tup = V_table();
    tbl_set(tup.as.t, V_int(1), V_bool(0));
    tbl_set(tup.as.t, V_int(2), V_str_from_c("attempt to call a non-function"));
    return tup;
  }
  ErrFrame frame;
  vm_err_push(vm, &frame);
  if (setjmp(frame.jb) == 0) {
    Value ret = call_any(vm, argv[0], argc - 1, argv + 1);
    vm_err_pop(vm);
    Value tup = V_table();
    tbl_set(tup.as.t, V_int(1), V_bool(1));
    tbl_set(tup.as.t, V_int(2), ret);
    return tup;
  } else {
    Value err = vm->err_obj.tag ? vm->err_obj : V_str_from_c("error");
    vm_err_pop(vm);
    Value tup = V_table();
    tbl_set(tup.as.t, V_int(1), V_bool(0));
    tbl_set(tup.as.t, V_int(2), err);
    return tup;
  }
}
static Value builtin_xpcall(struct VM *vm, int argc, Value *argv){
  if (argc < 2 || !is_callable(argv[0]) || !is_callable(argv[1])) {
    Value tup = V_table();
    tbl_set(tup.as.t, V_int(1), V_bool(0));
    tbl_set(tup.as.t, V_int(2), V_str_from_c("bad arguments to xpcall"));
    return tup;
  }
  Value f = argv[0];
  Value msgh = argv[1];
  ErrFrame frame;
  vm_err_push(vm, &frame);
  if (setjmp(frame.jb) == 0) {
    Value ret = call_any(vm, f, argc - 2, argv + 2);
    vm_err_pop(vm);
    Value tup = V_table();
    tbl_set(tup.as.t, V_int(1), V_bool(1));
    tbl_set(tup.as.t, V_int(2), ret);
    return tup;
  } else {
    Value err_in = vm->err_obj.tag ? vm->err_obj : V_str_from_c("error");
    vm_err_pop(vm);
    ErrFrame mh;
    vm_err_push(vm, &mh);
    if (setjmp(mh.jb) == 0) {
      Value msgret = call_any(vm, msgh, 1, &err_in);
      vm_err_pop(vm);
      Value tup = V_table();
      tbl_set(tup.as.t, V_int(1), V_bool(0));
      tbl_set(tup.as.t, V_int(2), msgret);
      return tup;
    } else {
      Value handler_err = vm->err_obj.tag ? vm->err_obj : V_str_from_c("error in error handler");
      vm_err_pop(vm);
      Value tup = V_table();
      tbl_set(tup.as.t, V_int(1), V_bool(0));
      tbl_set(tup.as.t, V_int(2), handler_err);
      return tup;
    }
  }
}
static Value eval_expr(VM *vm, AST *n);
static void  exec_stmt(VM *vm, AST *n);
static Func *func_new(ASTVec params, bool vararg, AST *body, Env *capt){
  Func *fn = xmalloc(sizeof(*fn));
  fn->params = params; 
  fn->vararg = vararg;
  fn->body   = body;
  fn->env    = capt;
  return fn;
}
static Value call_function(VM *vm, Func *fn, int argc, Value *argv){
  Env *saved_env = vm->env;
  bool saved_has_ret = vm->has_ret;
  Value saved_ret = vm->ret_val;
  bool saved_break = vm->break_flag;
  bool saved_pg = vm->pending_goto;
  const char *saved_gl = vm->goto_label;
  vm->env = env_push(fn->env);
  if (vm->active_co && !vm->co_call_env) {
    vm->co_call_env = vm->env;
  }
  int pcount = (int)fn->params.count;
  for(int i=0;i<pcount;i++){
    AST *id = fn->params.items[i];
    const char *nm = (id && id->kind==AST_IDENT) ? id->as.ident.name : "";
    Value val = (i<argc)? argv[i] : V_nil();
    env_add(vm->env, nm, val, true);
  }
  if(fn->vararg){
    Value vargs = V_table();
    int k=1;
    for(int i=pcount;i<argc;i++){
      tbl_set(vargs.as.t, V_int(k++), argv[i]);
    }
    env_add(vm->env, "...", vargs, true);
  }
  vm->has_ret = false;
  vm->break_flag = false;
  vm->pending_goto = false;
  exec_stmt(vm, fn->body);
  Value ret = vm->has_ret ? vm->ret_val : V_nil();
  vm->env = saved_env;
  vm->has_ret = saved_has_ret;
  vm->ret_val = saved_ret;
  vm->break_flag = saved_break;
  vm->pending_goto = saved_pg;
  vm->goto_label = saved_gl;
  return ret;
}
static Value eval_index(VM *vm, Value table, Value key){
  if(table.tag!=VAL_TABLE) return V_nil();
  Value out;
  if(tbl_get(table.as.t, key, &out)) return out;
  Value mm = mm_of(table, "__index");
  if (mm.tag == VAL_NIL) return V_nil();
  if (mm.tag == VAL_TABLE){
    Value v;
    if (tbl_get(mm.as.t, key, &v)) return v;
    return V_nil();
  }
  Value argv2[2] = { table, key };
  return call_any(vm, mm, 2, argv2);
}
static void assign_index(VM *vm, Value table, Value key, Value val){
  if(table.tag!=VAL_TABLE) return;
  Value existing;
  if (tbl_get(table.as.t, key, &existing)){
    tbl_set(table.as.t, key, val);
    return;
  }
  Value mm = mm_of(table, "__newindex");
  if (mm.tag == VAL_NIL){
    tbl_set(table.as.t, key, val);
    return;
  }
  if (mm.tag == VAL_TABLE){
    tbl_set(mm.as.t, key, val);
    return;
  }
  Value argv3[3] = { table, key, val };
  (void)call_any(vm, mm, 3, argv3);
}
static Value eval_expr(VM *vm, AST *n){
  switch(n->kind){
    case AST_NIL:   return V_nil();
    case AST_BOOL:  return V_bool(n->as.bval.v);
    case AST_NUMBER:return V_num(n->as.nval.v);
    case AST_STRING:return V_str_from_c(n->as.sval.s);
    case AST_IDENT: {
      Value v; if(env_get(vm->env, n->as.ident.name, &v)) return v;
      return V_nil();
    }
    case AST_UNARY: {
      Value r = eval_expr(vm, n->as.unary.expr);
      switch(n->as.unary.op){
        case OP_NEG: return (r.tag==VAL_INT)?V_int(-r.as.i):V_num(-as_num(r));
        case OP_NOT: return V_bool(!as_truthy(r));
        case OP_LEN: {
          {
            Value out;
            if (try_un_mm(vm, "__len", r, &out)) return out;
          }
          if (r.tag == VAL_STR) return V_int(r.as.s->len);
          if (r.tag == VAL_TABLE) return op_len(r);
          const char *tname =
            (r.tag == VAL_NIL)                     ? "nil" :
            (r.tag == VAL_BOOL)                    ? "boolean" :
            (r.tag == VAL_INT || r.tag == VAL_NUM) ? "number" :
            (r.tag == VAL_STR)                     ? "string" :
            (r.tag == VAL_TABLE)                   ? "table" :
            (r.tag == VAL_FUNC || r.tag == VAL_CFUNC) ? "function" :
                                                       "value";
          char buf[96];
          snprintf(buf, sizeof(buf), "attempt to get length of a %s value", tname);
          vm_raise(vm, V_str_from_c(buf));
          return V_nil(); 
        }
        default: return V_nil();
      }
    }
    case AST_BINARY: {
      if(n->as.binary.op==OP_AND){
        Value L = eval_expr(vm, n->as.binary.lhs);
        if(!as_truthy(L)) return L;
        return eval_expr(vm, n->as.binary.rhs);
      }
      if(n->as.binary.op==OP_OR){
        Value L = eval_expr(vm, n->as.binary.lhs);
        if(as_truthy(L)) return L;
        return eval_expr(vm, n->as.binary.rhs);
      }
      Value L = eval_expr(vm, n->as.binary.lhs);
      Value R = eval_expr(vm, n->as.binary.rhs);
      switch(n->as.binary.op){
        case OP_ADD: {
          if (L.tag==VAL_INT && R.tag==VAL_INT) return V_int(L.as.i + R.as.i);
          if ((L.tag==VAL_INT||L.tag==VAL_NUM) && (R.tag==VAL_INT||R.tag==VAL_NUM))
            return V_num(as_num(L) + as_num(R));
          Value out; if (try_bin_mm(vm, "__add", L, R, &out)) return out;
          vm_raise(vm, V_str_from_c("attempt to perform arithmetic on a non-number"));
          return V_nil();
        }
        case OP_SUB: {
          if (L.tag==VAL_INT && R.tag==VAL_INT) return V_int(L.as.i - R.as.i);
          if ((L.tag==VAL_INT||L.tag==VAL_NUM) && (R.tag==VAL_INT||R.tag==VAL_NUM))
            return V_num(as_num(L) - as_num(R));
          Value out; if (try_bin_mm(vm, "__sub", L, R, &out)) return out;
          vm_raise(vm, V_str_from_c("attempt to perform arithmetic on a non-number"));
          return V_nil();
        }
        case OP_MUL: {
          if (L.tag==VAL_INT && R.tag==VAL_INT) return V_int(L.as.i * R.as.i);
          if ((L.tag==VAL_INT||L.tag==VAL_NUM) && (R.tag==VAL_INT||R.tag==VAL_NUM))
            return V_num(as_num(L) * as_num(R));
          Value out; if (try_bin_mm(vm, "__mul", L, R, &out)) return out;
          vm_raise(vm, V_str_from_c("attempt to perform arithmetic on a non-number"));
          return V_nil();
        }
case OP_IDIV: {
    Value right = vm_pop(vm);   // replace vm_pop with however you get the top Value
    Value left  = vm_pop(vm);

    // Convert to numbers
    double l = (left.tag == VAL_INT) ? (double)left.as.i :
               (left.tag == VAL_NUM) ? left.as.n : 0.0;
    double r = (right.tag == VAL_INT) ? (double)right.as.i :
               (right.tag == VAL_NUM) ? right.as.n : 0.0;

    if (r == 0.0) {
        vm_raise(vm, V_str_from_c("integer division by zero"));
    }

    // Lua truncates toward -inf
    long long res = (long long)floor(l / r);
    vm_push(vm,V_int(res));  // wrap back into a Value and push
    break;
}
        case OP_DIV: {
          if ((L.tag==VAL_INT||L.tag==VAL_NUM) && (R.tag==VAL_INT||R.tag==VAL_NUM))
            return V_num(as_num(L) / as_num(R));
          Value out; if (try_bin_mm(vm, "__div", L, R, &out)) return out;
          vm_raise(vm, V_str_from_c("attempt to perform arithmetic on a non-number"));
          return V_nil();
        }
        case OP_MOD: {
          if (L.tag==VAL_INT && R.tag==VAL_INT) return V_int(L.as.i % R.as.i);
          if ((L.tag==VAL_INT||L.tag==VAL_NUM) && (R.tag==VAL_INT||R.tag==VAL_NUM))
            return V_num(fmod(as_num(L), as_num(R)));
          Value out; if (try_bin_mm(vm, "__mod", L, R, &out)) return out;
          vm_raise(vm, V_str_from_c("attempt to perform arithmetic on a non-number"));
          return V_nil();
        }
        case OP_POW: {
          if ((L.tag==VAL_INT||L.tag==VAL_NUM) && (R.tag==VAL_INT||R.tag==VAL_NUM))
            return V_num(pow(as_num(L), as_num(R)));
          Value out; if (try_bin_mm(vm, "__pow", L, R, &out)) return out;
          vm_raise(vm, V_str_from_c("attempt to perform arithmetic on a non-number"));
          return V_nil();
        }
        case OP_CONCAT: {
          if ((L.tag==VAL_STR || L.tag==VAL_INT || L.tag==VAL_NUM) &&
              (R.tag==VAL_STR || R.tag==VAL_INT || R.tag==VAL_NUM))
            return op_concat(L, R);
          Value out; if (try_bin_mm(vm, "__concat", L, R, &out)) return out;
          vm_raise(vm, V_str_from_c("attempt to concatenate a non-string value"));
          return V_nil();
        }
        case OP_EQ: {
          int eq = value_equal(L, R);
          Value fL = mm_of(L, "__eq"); Value fR = mm_of(R, "__eq");
          if (fL.tag != VAL_NIL || fR.tag != VAL_NIL){
            Value out;
            if (try_bin_mm(vm, "__eq", L, R, &out)) return V_bool(as_truthy(out));
          }
          return V_bool(eq);
        }
        case OP_NE: {
          Value fL = mm_of(L, "__eq"); Value fR = mm_of(R, "__eq");
          if (fL.tag != VAL_NIL || fR.tag != VAL_NIL){
            Value out;
            if (try_bin_mm(vm, "__eq", L, R, &out)) return V_bool(!as_truthy(out));
          }
          return V_bool(!value_equal(L, R));
        }
        case OP_LT: {
          if ((L.tag==VAL_INT||L.tag==VAL_NUM) && (R.tag==VAL_INT||R.tag==VAL_NUM))
            return V_bool(as_num(L) < as_num(R));
          if (L.tag==VAL_STR && R.tag==VAL_STR){
            int min = (L.as.s->len < R.as.s->len) ? L.as.s->len : R.as.s->len;
            int cmp = memcmp(L.as.s->data, R.as.s->data, (size_t)min);
            if (cmp < 0) return V_bool(1);
            if (cmp > 0) return V_bool(0);
            return V_bool(L.as.s->len < R.as.s->len);
          }
          Value out; if (try_bin_mm(vm, "__lt", L, R, &out)) return V_bool(as_truthy(out));
          const char *tL =
            (L.tag==VAL_NIL)?"nil":(L.tag==VAL_BOOL)?"boolean":
            (L.tag==VAL_INT||L.tag==VAL_NUM)?"number":
            (L.tag==VAL_STR)?"string":
            (L.tag==VAL_TABLE)?"table":
            (L.tag==VAL_FUNC||L.tag==VAL_CFUNC)?"function":"value";
          const char *tR =
            (R.tag==VAL_NIL)?"nil":(R.tag==VAL_BOOL)?"boolean":
            (R.tag==VAL_INT||R.tag==VAL_NUM)?"number":
            (R.tag==VAL_STR)?"string":
            (R.tag==VAL_TABLE)?"table":
            (R.tag==VAL_FUNC||R.tag==VAL_CFUNC)?"function":"value";
          char buf[128];
          snprintf(buf,sizeof(buf),"attempt to compare %s with %s", tL, tR);
          vm_raise(vm, V_str_from_c(buf));
          return V_bool(0);
        }
        case OP_LE: {
          if ((L.tag==VAL_INT||L.tag==VAL_NUM) && (R.tag==VAL_INT||R.tag==VAL_NUM))
            return V_bool(as_num(L) <= as_num(R));
          if (L.tag==VAL_STR && R.tag==VAL_STR){
            int min = (L.as.s->len < R.as.s->len) ? L.as.s->len : R.as.s->len;
            int cmp = memcmp(L.as.s->data, R.as.s->data, (size_t)min);
            if (cmp < 0) return V_bool(1);
            if (cmp > 0) return V_bool(0);
            return V_bool(L.as.s->len <= R.as.s->len);
          }
          Value out;
          if (try_bin_mm(vm, "__le", L, R, &out)) return V_bool(as_truthy(out));
          Value out2;
          if (try_bin_mm(vm, "__lt", R, L, &out2)) return V_bool(!as_truthy(out2));
          const char *tL =
            (L.tag==VAL_NIL)?"nil":(L.tag==VAL_BOOL)?"boolean":
            (L.tag==VAL_INT||L.tag==VAL_NUM)?"number":
            (L.tag==VAL_STR)?"string":
            (L.tag==VAL_TABLE)?"table":
            (L.tag==VAL_FUNC||L.tag==VAL_CFUNC)?"function":"value";
          const char *tR =
            (R.tag==VAL_NIL)?"nil":(R.tag==VAL_BOOL)?"boolean":
            (R.tag==VAL_INT||R.tag==VAL_NUM)?"number":
            (R.tag==VAL_STR)?"string":
            (R.tag==VAL_TABLE)?"table":
            (R.tag==VAL_FUNC||R.tag==VAL_CFUNC)?"function":"value";
          char buf[128];
          snprintf(buf,sizeof(buf),"attempt to compare %s with %s", tL, tR);
          vm_raise(vm, V_str_from_c(buf));
          return V_bool(0);
        }
        case OP_GT: {
          if ((L.tag==VAL_INT||L.tag==VAL_NUM) && (R.tag==VAL_INT||R.tag==VAL_NUM))
            return V_bool(as_num(L) > as_num(R));
          if (L.tag==VAL_STR && R.tag==VAL_STR){
            int min = (L.as.s->len < R.as.s->len) ? L.as.s->len : R.as.s->len;
            int cmp = memcmp(L.as.s->data, R.as.s->data, (size_t)min);
            if (cmp > 0) return V_bool(1);
            if (cmp < 0) return V_bool(0);
            return V_bool(L.as.s->len > R.as.s->len);
          }
          Value out; if (try_bin_mm(vm, "__lt", R, L, &out)) return V_bool(as_truthy(out));
          const char *tL =
            (L.tag==VAL_NIL)?"nil":(L.tag==VAL_BOOL)?"boolean":
            (L.tag==VAL_INT||L.tag==VAL_NUM)?"number":
            (L.tag==VAL_STR)?"string":
            (L.tag==VAL_TABLE)?"table":
            (L.tag==VAL_FUNC||L.tag==VAL_CFUNC)?"function":"value";
          const char *tR =
            (R.tag==VAL_NIL)?"nil":(R.tag==VAL_BOOL)?"boolean":
            (R.tag==VAL_INT||R.tag==VAL_NUM)?"number":
            (R.tag==VAL_STR)?"string":
            (R.tag==VAL_TABLE)?"table":
            (R.tag==VAL_FUNC||R.tag==VAL_CFUNC)?"function":"value";
          char buf[128];
          snprintf(buf,sizeof(buf),"attempt to compare %s with %s", tL, tR);
          vm_raise(vm, V_str_from_c(buf));
          return V_bool(0);
        }
        case OP_GE: {
          if ((L.tag==VAL_INT||L.tag==VAL_NUM) && (R.tag==VAL_INT||R.tag==VAL_NUM))
            return V_bool(as_num(L) >= as_num(R));
          if (L.tag==VAL_STR && R.tag==VAL_STR){
            int min = (L.as.s->len < R.as.s->len) ? L.as.s->len : R.as.s->len;
            int cmp = memcmp(L.as.s->data, R.as.s->data, (size_t)min);
            if (cmp > 0) return V_bool(1);
            if (cmp < 0) return V_bool(0);
            return V_bool(L.as.s->len >= R.as.s->len);
          }
          Value out; if (try_bin_mm(vm, "__lt", L, R, &out)) return V_bool(!as_truthy(out));
          const char *tL =
            (L.tag==VAL_NIL)?"nil":(L.tag==VAL_BOOL)?"boolean":
            (L.tag==VAL_INT||L.tag==VAL_NUM)?"number":
            (L.tag==VAL_STR)?"string":
            (L.tag==VAL_TABLE)?"table":
            (L.tag==VAL_FUNC||L.tag==VAL_CFUNC)?"function":"value";
          const char *tR =
            (R.tag==VAL_NIL)?"nil":(R.tag==VAL_BOOL)?"boolean":
            (R.tag==VAL_INT||R.tag==VAL_NUM)?"number":
            (R.tag==VAL_STR)?"string":
            (R.tag==VAL_TABLE)?"table":
            (R.tag==VAL_FUNC||R.tag==VAL_CFUNC)?"function":"value";
          char buf[128];
          snprintf(buf,sizeof(buf),"attempt to compare %s with %s", tL, tR);
          vm_raise(vm, V_str_from_c(buf));
          return V_bool(0);
        }
        default: return V_nil();
      }
    }
    case AST_TABLE: {
      Value t = V_table();
      int nexti = 1;
      for(size_t i=0;i<n->as.table.values.count;i++){
        AST *k = n->as.table.keys.items[i];
        AST *v = n->as.table.values.items[i];
        if(!k && v && v->kind==AST_IDENT && v->as.ident.name && strcmp(v->as.ident.name,"...")==0){
          Value dots;
          if(env_get(vm->env, "...", &dots) && dots.tag==VAL_TABLE){
            Value elem; int j=1;
            while(tbl_get(dots.as.t, V_int((long long)j), &elem)){ tbl_set(t.as.t, V_int(nexti++), elem); j++; }
            continue;
          }
        }
        Value key = k? eval_expr(vm,k) : V_int((long long)nexti++);
        Value val = eval_expr(vm,v);
        tbl_set(t.as.t, key, val);
      }
      return t;
    }
    case AST_INDEX: {
      Value t = eval_expr(vm, n->as.index.target);
      Value k = eval_expr(vm, n->as.index.index);
      return eval_index(vm, t, k);
    }
    case AST_FIELD: {
      Value t = eval_expr(vm, n->as.field.target);
      Value k = V_str_from_c(n->as.field.field);
      return eval_index(vm, t, k);
    }
    case AST_FUNCTION: {
      Func *fn = func_new(n->as.fn.params, n->as.fn.vararg, n->as.fn.body, vm->env);
      Value v; v.tag=VAL_FUNC; v.as.fn=fn; return v;
    }
    case AST_CALL: {
      Value cal = eval_expr(vm, n->as.call.callee);
      int final_argc = 0;
      for (int i = 0; i < (int)n->as.call.args.count; i++) {
        AST *arg = n->as.call.args.items[i];
        if (arg && arg->kind == AST_IDENT && arg->as.ident.name &&
            strcmp(arg->as.ident.name, "...") == 0) {
          Value dots;
          if (env_get(vm->env, "...", &dots) && dots.tag == VAL_TABLE) {
            int j = 1; Value tmp;
            while (tbl_get(dots.as.t, V_int(j), &tmp)) { final_argc++; j++; }
          }
        } else {
          final_argc++;
        }
      }
      Value *argv = final_argc ? xmalloc(sizeof(Value) * final_argc) : NULL;
      int ai = 0;
      for (int i = 0; i < (int)n->as.call.args.count; i++) {
        AST *arg = n->as.call.args.items[i];
        if (arg && arg->kind == AST_IDENT && arg->as.ident.name &&
            strcmp(arg->as.ident.name, "...") == 0) {
          Value dots;
          if (env_get(vm->env, "...", &dots) && dots.tag == VAL_TABLE) {
            int j = 1; Value tmp;
            while (tbl_get(dots.as.t, V_int(j), &tmp)) { argv[ai++] = tmp; j++; }
          }
        } else {
          argv[ai++] = eval_expr(vm, arg);
        }
      }
      Value ret = call_any(vm, cal, final_argc, argv);
      if(argv) free(argv);
      return ret;
    }
    default: return V_nil();
  }
}
static void assign_loop_vars(VM *vm, AST *forin, Value a, Value b){
  size_t nvars = forin->as.forin.names.count;
  if (nvars >= 1) {
    AST *id0 = forin->as.forin.names.items[0];
    if (id0 && id0->kind==AST_IDENT) env_set(vm->env, id0->as.ident.name, a);
  }
  if (nvars >= 2) {
    AST *id1 = forin->as.forin.names.items[1];
    if (id1 && id1->kind==AST_IDENT) env_set(vm->env, id1->as.ident.name, b);
  }
  for (size_t i=2;i<nvars;i++){
    AST *id = forin->as.forin.names.items[i];
    if (id && id->kind==AST_IDENT) env_set(vm->env, id->as.ident.name, V_nil());
  }
}
typedef struct LabelMap {
  const char *name;
  size_t index; 
} LabelMap;
static int find_label_index(LabelMap *labels, size_t lab_count, const char *nm){
  for(size_t j=0;j<lab_count;j++){
    if(strcmp(labels[j].name, nm)==0) return (int)labels[j].index;
  }
  return -1;
}
static void exec_block(VM *vm, AST *blk){
  Env *saved = vm->env;
  vm->env = env_push(saved);
  ASTVec *S = &blk->as.block.stmts;
  LabelMap *labels = NULL; size_t lab_count=0, lab_cap=0;
  for(size_t i=0;i<S->count;i++){
    AST *st = S->items[i];
    if(st->kind==AST_LABEL){
      if(lab_count==lab_cap){ lab_cap=lab_cap?lab_cap*2:8; labels=realloc(labels, lab_cap*sizeof(LabelMap)); }
      labels[lab_count++] = (LabelMap){ .name = st->as.label.label, .index = i };
    }
  }
  size_t pc = 0;
  if (vm->pending_goto) {
    int idx = find_label_index(labels, lab_count, vm->goto_label);
    if (idx >= 0) {
      pc = (size_t)idx + 1;
      vm->pending_goto = false;
    } else {
      vm->env = saved;
      if(labels) free(labels);
      return;
    }
  }
  if (vm->active_co && vm->co_point.blk == blk && vm->co_point.pc > 0) {
    pc = vm->co_point.pc;
    vm->co_point.blk = NULL;
    vm->co_point.pc  = 0;
  }
  for(; pc<S->count; ){
    AST *st = S->items[pc];
    if(vm->has_ret) break;
    if(vm->break_flag) break;
    switch(st->kind){
      case AST_LABEL:
        pc++;
        break;
case AST_GOTO: {
  vm->pending_goto = true;
  vm->goto_label   = st->as.go.label;
  env_close_all(vm, vm->env, V_nil());
  vm->env = saved;
  if(labels) free(labels);
  return;
}
      case AST_STMT_EXPR:
        (void)eval_expr(vm, st->as.stmt_expr.expr);
        pc++;
        break;
      case AST_ASSIGN: {
        Value rv = eval_expr(vm, st->as.assign.rhs);
        AST *lhs = st->as.assign.lhs_ident;
        if(lhs->kind==AST_IDENT){
          Env *owner=NULL; int slot=-1;
          if(env_find(vm->env, lhs->as.ident.name, &owner, &slot)){
            owner->vals[slot]=rv;
          } else {
            env_add(env_root(vm->env), lhs->as.ident.name, rv, false);
          }
        } else if(lhs->kind==AST_INDEX){
          Value t = eval_expr(vm, lhs->as.index.target);
          Value k = eval_expr(vm, lhs->as.index.index);
          assign_index(vm, t, k, rv);
        } else if(lhs->kind==AST_FIELD){
          Value t = eval_expr(vm, lhs->as.field.target);
          Value k = V_str_from_c(lhs->as.field.field);
          assign_index(vm, t, k, rv);
        }
        pc++;
        break;
      }
      case AST_BLOCK:
        exec_block(vm, st);
        if (vm->pending_goto) {
          int idx = find_label_index(labels, lab_count, vm->goto_label);
          if (idx >= 0) { pc = (size_t)idx + 1; vm->pending_goto=false; }
          else { vm->env = saved; if(labels) free(labels); return; }
        } else {
          pc++;
        }
        break;
      case AST_IF: {
        AST *node = st;
        for(;;){
          if(node->kind!=AST_IF){
            if(node) exec_block(vm, node);
            break;
          }
          if(as_truthy(eval_expr(vm, node->as.ifs.cond))){
            exec_block(vm, node->as.ifs.then_blk);
            break;
          }
          node = node->as.ifs.else_blk;
          if(!node) break;
        }
        if (vm->pending_goto) {
          int idx = find_label_index(labels, lab_count, vm->goto_label);
          if (idx >= 0) { pc = (size_t)idx + 1; vm->pending_goto=false; }
          else { vm->env = saved; if(labels) free(labels); return; }
        } else {
          pc++;
        }
        break;
      }
      case AST_WHILE: {
        long long iters = 0;
        while(as_truthy(eval_expr(vm, st->as.whiles.cond))){
          if(++iters > LUA_PLUS_MAX_LOOP_ITERS){
            fprintf(stderr,"[LuaX]: while loop exceeded %d iterations (possible infinite loop) at line %d\n",
                    LUA_PLUS_MAX_LOOP_ITERS, st->line);
            break;
          }
          vm->break_flag=false;
          exec_block(vm, st->as.whiles.body);
          if(vm->has_ret) break;
          if(vm->pending_goto){
            int idx = find_label_index(labels, lab_count, vm->goto_label);
            if (idx >= 0) { pc = (size_t)idx + 1; vm->pending_goto=false; break; }
            else { vm->env = saved; if(labels) free(labels); return; }
          }
          if(vm->break_flag){ vm->break_flag=false; break; }
        }
        pc++;
        break;
      }
      case AST_REPEAT: {
        long long iters = 0;
        for(;;){
          if(++iters > LUA_PLUS_MAX_LOOP_ITERS){
            fprintf(stderr,"[LuaX]: repeat-until loop exceeded %d iterations (possible infinite loop) at line %d\n",
                    LUA_PLUS_MAX_LOOP_ITERS, st->line);
            break;
          }
          vm->break_flag=false;
          exec_block(vm, st->as.repeatstmt.body);
          if(vm->has_ret) break;
          if(vm->pending_goto){
            int idx = find_label_index(labels, lab_count, vm->goto_label);
            if (idx >= 0) { pc = (size_t)idx + 1; vm->pending_goto=false; break; }
            else { vm->env = saved; if(labels) free(labels); return; }
          }
          if(vm->break_flag){ vm->break_flag=false; break; }
          if(as_truthy(eval_expr(vm, st->as.repeatstmt.cond))) break;
        }
        pc++;
        break;
      }
      case AST_FOR_NUM: {
        const char *vname = st->as.fornum.var;
        long long start = as_int(eval_expr(vm, st->as.fornum.start));
        long long end   = as_int(eval_expr(vm, st->as.fornum.end));
        long long step  = st->as.fornum.step? as_int(eval_expr(vm, st->as.fornum.step)) : 1;
        if(step==0){ fprintf(stderr,"[LuaX]: numeric for with step=0 at line %d; skipping loop\n", st->line); pc++; break; }
        env_add(vm->env, vname, V_int(start), true);
        long long iters=0;
        if(step>0){
          for(long long i=start; i<=end; i+=step){
            if(++iters > LUA_PLUS_MAX_LOOP_ITERS){ fprintf(stderr,"[LuaX]: for loop exceeded %d iterations at line %d\n", LUA_PLUS_MAX_LOOP_ITERS, st->line); break; }
            env_set(vm->env, vname, V_int(i));
            vm->break_flag=false;
            exec_block(vm, st->as.fornum.body);
            if(vm->has_ret) break;
            if(vm->pending_goto){
              int idx = find_label_index(labels, lab_count, vm->goto_label);
              if (idx >= 0) { pc = (size_t)idx + 1; vm->pending_goto=false; break; }
              else { vm->env = saved; if(labels) free(labels); return; }
            }
            if(vm->break_flag){ vm->break_flag=false; break; }
          }
        } else {
          for(long long i=start; i>=end; i+=step){
            if(++iters > LUA_PLUS_MAX_LOOP_ITERS){ fprintf(stderr,"[LuaX]: for loop exceeded %d iterations at line %d\n", LUA_PLUS_MAX_LOOP_ITERS, st->line); break; }
            env_set(vm->env, vname, V_int(i));
            vm->break_flag=false;
            exec_block(vm, st->as.fornum.body);
            if(vm->has_ret) break;
            if(vm->pending_goto){
              int idx = find_label_index(labels, lab_count, vm->goto_label);
              if (idx >= 0) { pc = (size_t)idx + 1; vm->pending_goto=false; break; }
              else { vm->env = saved; if(labels) free(labels); return; }
            }
            if(vm->break_flag){ vm->break_flag=false; break; }
          }
        }
        pc++;
        break;
      }
      case AST_FOR_IN: {
        size_t nvars = st->as.forin.names.count;
        for (size_t i=0;i<nvars;i++){
          AST *id = st->as.forin.names.items[i];
          const char *nm = (id && id->kind==AST_IDENT) ? id->as.ident.name : "";
          env_add(vm->env, nm, V_nil(), true);
        }
        size_t niters = st->as.forin.iters.count;
        if (niters == 1) {
          Value it0 = eval_expr(vm, st->as.forin.iters.items[0]);
          if (it0.tag == VAL_TABLE) {
            Value iterV, stateV, ctrlV;
            int has1 = tbl_get(it0.as.t, V_int(1), &iterV);
            int has2 = tbl_get(it0.as.t, V_int(2), &stateV);
            int has3 = tbl_get(it0.as.t, V_int(3), &ctrlV);
            if (has1 && has2 && has3 && is_callable(iterV)) {
              long long iters_guard = 0;
              Value iterF = iterV, state = stateV, ctrl = ctrlV;
              for(;;){
                if (++iters_guard > LUA_PLUS_MAX_LOOP_ITERS){
                  fprintf(stderr,"[LuaX]: for-in (ipairs/generic) exceeded %d iterations at line %d\n",
                          LUA_PLUS_MAX_LOOP_ITERS, st->line);
                  break;
                }
                Value argv2[2]; int argc2 = 0;
                argv2[argc2++] = state;
                argv2[argc2++] = ctrl;
                Value res = call_any(vm, iterF, argc2, argv2);
                if (res.tag == VAL_NIL) break;
                Value a = res, b = V_nil();
                if (res.tag == VAL_TABLE) {
                  Value tmp;
                  if (tbl_get(res.as.t, V_int(1), &tmp)) a = tmp;
                  if (tbl_get(res.as.t, V_int(2), &tmp)) b = tmp;
                }
                assign_loop_vars(vm, st, a, b);
                ctrl = a;
                vm->break_flag=false;
                exec_block(vm, st->as.forin.body);
                if (vm->has_ret) break;
                if (vm->pending_goto){
                  int idx = find_label_index(labels, lab_count, vm->goto_label);
                  if (idx >= 0) { pc = (size_t)idx + 1; vm->pending_goto=false; break; }
                  else { vm->env = saved; if(labels) free(labels); return; }
                }
                if (vm->break_flag){ vm->break_flag=false; break; }
              }
              pc++;
              break;
            }
          }
          if (it0.tag == VAL_TABLE) {
            Table *tt = it0.as.t;
            long long iters_guard = 0;
            int stop = 0;
            for (int bi = 0; bi < tt->cap && !stop; ++bi) {
              for (TableEntry *e = tt->buckets[bi]; e && !stop; e = e->next) {
                if (++iters_guard > LUA_PLUS_MAX_LOOP_ITERS){
                  fprintf(stderr,"[LuaX]: for-in (table) exceeded %d iterations at line %d\n",
                          LUA_PLUS_MAX_LOOP_ITERS, st->line);
                  break;
                }
                if (nvars <= 1) assign_loop_vars(vm, st, e->val, V_nil());
                else            assign_loop_vars(vm, st, e->key, e->val);
                vm->break_flag=false;
                exec_block(vm, st->as.forin.body);
                if (vm->has_ret) { stop = 1; break; }
                if (vm->pending_goto){
                  int idx = find_label_index(labels, lab_count, vm->goto_label);
                  if (idx >= 0) { pc = (size_t)idx + 1; vm->pending_goto=false; stop = 1; break; }
                  else { vm->env = saved; if(labels) free(labels); return; }
                }
                if (vm->break_flag){ vm->break_flag=false; stop = 1; break; }
              }
            }
            pc++;
            break;
          }
          if (is_callable(it0)) {
            long long iters_guard = 0;
            for (;;) {
              if (++iters_guard > LUA_PLUS_MAX_LOOP_ITERS){
                fprintf(stderr,"[LuaX]: for-in (iter) exceeded %d iterations at line %d\n",
                        LUA_PLUS_MAX_LOOP_ITERS, st->line);
                break;
              }
              Value res = call_any(vm, it0, 0, NULL);
              if (res.tag == VAL_NIL) break;
              Value a = res, b = V_nil();
              if (res.tag == VAL_TABLE) {
                Value tmp;
                if (tbl_get(res.as.t, V_int(1), &tmp)) a = tmp;
                if (tbl_get(res.as.t, V_int(2), &tmp)) b = tmp;
              }
              assign_loop_vars(vm, st, a, b);
              vm->break_flag=false;
              exec_block(vm, st->as.forin.body);
              if (vm->has_ret) break;
              if (vm->pending_goto){
                int idx = find_label_index(labels, lab_count, vm->goto_label);
                if (idx >= 0) { pc = (size_t)idx + 1; vm->pending_goto=false; break; }
                else { vm->env = saved; if(labels) free(labels); return; }
              }
              if (vm->break_flag){ vm->break_flag=false; break; }
            }
            pc++;
            break;
          }
          pc++;
          break;
        }
        {
          Value iter = eval_expr(vm, st->as.forin.iters.items[0]);
          Value state = V_nil();
          Value ctrl  = V_nil();
          if (niters >= 2) state = eval_expr(vm, st->as.forin.iters.items[1]);
          if (niters >= 3) ctrl  = eval_expr(vm, st->as.forin.iters.items[2]);
          if (!is_callable(iter)) { pc++; break; }
          long long iters_guard = 0;
          for(;;){
            if (++iters_guard > LUA_PLUS_MAX_LOOP_ITERS){
              fprintf(stderr,"[LuaX]: for-in (generic) exceeded %d iterations at line %d\n",
                      LUA_PLUS_MAX_LOOP_ITERS, st->line);
              break;
            }
            Value argv2[2]; int argc2 = 0;
            if (niters >= 2) argv2[argc2++] = state;
            if (niters >= 3) argv2[argc2++] = ctrl;
            Value res = call_any(vm, iter, argc2, argv2);
            if (res.tag == VAL_NIL) break;
            Value a = res, b = V_nil();
            if (res.tag == VAL_TABLE) {
              Value tmp;
              if (tbl_get(res.as.t, V_int(1), &tmp)) a = tmp;
              if (tbl_get(res.as.t, V_int(2), &tmp)) b = tmp;
            }
            assign_loop_vars(vm, st, a, b);
            ctrl = a;
            vm->break_flag=false;
            exec_block(vm, st->as.forin.body);
            if (vm->has_ret) break;
            if (vm->pending_goto){
              int idx = find_label_index(labels, lab_count, vm->goto_label);
              if (idx >= 0) { pc = (size_t)idx + 1; vm->pending_goto=false; break; }
              else { vm->env = saved; if(labels) free(labels); return; }
            }
            if (vm->break_flag){ vm->break_flag=false; break; }
          }
          pc++;
          break;
        }
      }
      case AST_BREAK:
        vm->break_flag=true; pc++; break;
// Fixes for interpreter.c - Multi-return handling

// Replace the AST_RETURN case with this:
// Complete fix for interpreter.c - Multi-return handling
// This addresses the issue where single returns were being wrapped in tables

/* ==================================================================
   IMPORTANT: The issue is that we DON'T need the marker system.
   The real problem is simpler - just don't unpack regular tables!
   
   Solution: Only create multi-return tables in specific contexts
   where Lua actually supports multiple returns (function calls in
   the last position of an assignment list).
   ==================================================================*/

// 1. KEEP AST_RETURN SIMPLE - just return single values normally
case AST_RETURN: {
  Value rv = V_nil();
  if(st->as.ret.values.count == 0){
    rv = V_nil();
  } else if(st->as.ret.values.count == 1){
    // Single return value - return as-is
    rv = eval_expr(vm, st->as.ret.values.items[0]);
  } else {
    // Multiple return values - pack into table WITHOUT marker
    // We'll handle unpacking at the call site
    rv = V_table();
    for(size_t i = 0; i < st->as.ret.values.count; i++){
      Value v = eval_expr(vm, st->as.ret.values.items[i]);
      tbl_set(rv.as.t, V_int((long long)(i + 1)), v);
    }
  }
  vm->ret_val = rv; 
  vm->has_ret = true;
  env_close_all(vm, vm->env, V_nil());
  vm->env = saved;
  if(labels) free(labels);
  return;
}

// 2. FIX AST_ASSIGN_LIST - Don't unpack arbitrary tables!
case AST_ASSIGN_LIST: {
    size_t rn = st->as.massign.rvals.count;
    Value *rv = rn? xmalloc(sizeof(Value)*rn):NULL;
    
    // Track which RHS values are function calls (only those can expand)
    bool *is_call = rn? xmalloc(sizeof(bool)*rn):NULL;
    
    // Evaluate all right-hand side expressions
    for(size_t i=0;i<rn;i++) {
        AST *rhs = st->as.massign.rvals.items[i];
        is_call[i] = (rhs && rhs->kind == AST_CALL);
        rv[i]=eval_expr(vm, rhs);
    }
    
    // Check if we need to expand the last value
    // ONLY expand if: 1) it's a call, 2) returns a table, 3) we need more values
    Value *all_vals = rv;
    size_t total_vals = rn;
    bool expanded = false;
    
    if(rn > 0 && st->as.massign.lvals.count > rn && is_call[rn-1]){
        Value last = rv[rn-1];
        // Check if it's a table that looks like multi-return
        // (has sequential numeric keys starting from 1)
        if(last.tag == VAL_TABLE){
            Value test;
            if(tbl_get(last.as.t, V_int(1), &test) && test.tag != VAL_NIL){
                // Looks like multi-return, expand it
                size_t needed = st->as.massign.lvals.count;
                all_vals = xmalloc(sizeof(Value) * needed);
                expanded = true;
                
                // Copy first rn-1 values
                for(size_t i=0; i<rn-1; i++){
                    all_vals[i] = rv[i];
                }
                
                // Unpack the table
                size_t idx = rn-1;
                for(long long j=1; idx < needed; j++){
                    Value v;
                    if(tbl_get(last.as.t, V_int(j), &v)){
                        all_vals[idx++] = v;
                    } else {
                        all_vals[idx++] = V_nil();
                    }
                }
                total_vals = needed;
            }
        }
    }
    
    // Assign to left-hand side variables
    for(size_t i=0;i<st->as.massign.lvals.count;i++){
        AST *lhs = st->as.massign.lvals.items[i];
        Value val = (i<total_vals)?all_vals[i]:V_nil();
        
        if(lhs->kind==AST_IDENT){
            Env *owner=NULL; int slot=-1;
            if(env_find(vm->env, lhs->as.ident.name, &owner, &slot)){
                owner->vals[slot]=val;
            } else {
                env_add(env_root(vm->env), lhs->as.ident.name, val, false);
            }
        } else if(lhs->kind==AST_INDEX){
            Value t = eval_expr(vm, lhs->as.index.target);
            Value k = eval_expr(vm, lhs->as.index.index);
            assign_index(vm, t, k, val);
        } else if(lhs->kind==AST_FIELD){
            Value t = eval_expr(vm, lhs->as.field.target);
            Value k = V_str_from_c(lhs->as.field.field);
            assign_index(vm, t, k, val);
        }
    }
    
    // Cleanup
    if(expanded) free(all_vals);
    if(rv) free(rv);
    if(is_call) free(is_call);
    pc++;
    break;
}

// 3. ALSO NEED TO FIX AST_VAR if it supports multiple variables
// Add this check after the existing AST_VAR case:

case AST_VAR: {
  // Check if this is actually a multi-variable declaration
  // If your parser creates AST_VAR for "local a,b,c = one()",
  // you might need special handling here too.
  
  Value init = st->as.var.init? eval_expr(vm, st->as.var.init): V_nil();
  
  // TODO: If your AST supports multiple variables in one AST_VAR node,
  // you need to handle unpacking here similar to AST_ASSIGN_LIST
  
  env_add(vm->env, st->as.var.name, init, st->as.var.is_local);
  if (st->as.var.is_close) {
    Env *owner=NULL; int slot=-1;
    if (env_find(vm->env, st->as.var.name, &owner, &slot) && owner == vm->env) {
      env_register_close(owner, slot);
    }
  }
  pc++;
  break;
}

/* ==================================================================
   KEY INSIGHT:
   
   The real issue is distinguishing between:
   1. t = {1,2,3}    -- user table
   2. a,b,c = f()    -- where f() returns multiple values
   
   We can only expand case #2 safely. The way to tell them apart is:
   - Case #2: The RHS is an AST_CALL node
   - Case #1: The RHS is NOT an AST_CALL node
   
   So we ONLY unpack tables when:
   1. There are fewer RHS expressions than LHS variables
   2. The LAST RHS expression is an AST_CALL
   3. That call returned a table
   
   This prevents unpacking user-created tables while still supporting
   multiple return values from functions.
   ==================================================================*/
      case AST_FUNC_STMT: {
        AST *name = st->as.fnstmt.name;
        Value fval; fval.tag = VAL_FUNC; fval.as.fn = NULL;
        if(st->as.fnstmt.is_local && name->kind==AST_IDENT){
          env_add(vm->env, name->as.ident.name, V_nil(), true);
        } else if(name->kind==AST_IDENT){
          Env *owner=NULL; int slot=-1;
          if(!env_find(vm->env, name->as.ident.name, &owner, &slot)){
            env_add(env_root(vm->env), name->as.ident.name, V_nil(), false);
          }
        }
        Func *fn = func_new(st->as.fnstmt.params, st->as.fnstmt.vararg, st->as.fnstmt.body, vm->env);
        fval.tag = VAL_FUNC; fval.as.fn = fn;
        if(name->kind==AST_IDENT){
          Env *owner=NULL; int slot=-1;
          if(env_find(vm->env, name->as.ident.name, &owner, &slot)) owner->vals[slot]=fval;
          else env_add(env_root(vm->env), name->as.ident.name, fval, false);
        } else if(name->kind==AST_FIELD){
          Value t = eval_expr(vm, name->as.field.target);
          Value k = V_str_from_c(name->as.field.field);
          assign_index(vm, t, k, fval);
        } else if(name->kind==AST_INDEX){
          Value t = eval_expr(vm, name->as.index.target);
          Value k = eval_expr(vm, name->as.index.index);
          assign_index(vm, t, k, fval);
        }
        pc++;
        break;
      }
      default:
        pc++;
        break;
    }
    if (vm->co_yielding) {
      vm->co_point.blk = blk;
      vm->co_point.pc  = pc;  
      vm->env = saved;
      if(labels) free(labels);
      return;
    }
  }
  env_close_all(vm, vm->env, V_nil());      
  vm->env = saved;
  if(labels) free(labels);
}
static void exec_stmt(VM *vm, AST *n){
  if(n->kind==AST_BLOCK){ exec_block(vm,n); return; }
  AST fake; memset(&fake,0,sizeof(fake));
  fake.kind=AST_BLOCK;
  ASTVec v={0}; v.count=1; v.cap=1; v.items=&n;
  fake.as.block.stmts=v;
  exec_block(vm,&fake);
}
static const char *default_package_path(void){
  const char *env54 = getenv("LUA_PATH_5_4");
  const char *env   = getenv("LUA_PATH");
  if (env54 && *env54) return env54;
  if (env   && *env)   return env;
  
  // Add current directory and tests directory to the search path
  return "?.lua;?/init.lua;"                    // current directory
         "./?.lua;./?/init.lua;"                 // explicit current
         "/usr/local/share/lua/5.4/?.lua;/usr/local/share/lua/5.4/?/init.lua;"
         "/usr/share/lua/5.4/?.lua;/usr/share/lua/5.4/?/init.lua;"
         "/usr/local/lib/luarocks/rocks-5.4/?/init.lua;/usr/local/lib/luarocks/rocks-5.4/?.lua;"
         "/usr/local/lib/lua/5.4/?.lua;/usr/local/lib/luarocks/rocks-5.4/?/init.lua";
}
static Value ensure_package(VM *vm){
  Value pkg;
  if (!env_get(env_root(vm->env), "package", &pkg) || pkg.tag != VAL_TABLE){
    pkg = V_table();
    tbl_set(pkg.as.t, V_str_from_c("path"), V_str_from_c(default_package_path()));
    tbl_set(pkg.as.t, V_str_from_c("loaded"), V_table());
    tbl_set(pkg.as.t, V_str_from_c("preload"), V_table());
    env_add(env_root(vm->env), "package", pkg, false);
  } else {
    Value v;
    if (!tbl_get(pkg.as.t, V_str_from_c("path"), &v) || v.tag!=VAL_STR)
      tbl_set(pkg.as.t, V_str_from_c("path"), V_str_from_c(default_package_path()));
    if (!tbl_get(pkg.as.t, V_str_from_c("loaded"), &v) || v.tag!=VAL_TABLE)
      tbl_set(pkg.as.t, V_str_from_c("loaded"), V_table());
    if (!tbl_get(pkg.as.t, V_str_from_c("preload"), &v) || v.tag!=VAL_TABLE)
      tbl_set(pkg.as.t, V_str_from_c("preload"), V_table());
  }
  return pkg;
}
static char *modname_to_path(const char *name){
  size_t n = strlen(name);
  char *s = (char*)xmalloc(n+1);
  for(size_t i=0;i<n;i++) s[i] = (name[i]=='.')? '/' : name[i];
  s[n]='\0';
  return s;
}
static char *expand_one(const char *templ, const char *modpath){
  size_t nt = strlen(templ), nm = strlen(modpath);
  size_t cap = nt + nm*4 + 8;
  char *buf = (char*)xmalloc(cap);
  size_t j=0;
  for(size_t i=0;i<nt;i++){
    if (templ[i]=='?'){
      size_t need = j + nm + 1;
      if (need > cap){ cap = need*2; buf = (char*)realloc(buf, cap); }
      memcpy(buf+j, modpath, nm); j+=nm;
    } else {
      if (j+2 > cap){ cap*=2; buf=(char*)realloc(buf,cap); }
      buf[j++] = templ[i];
    }
  }
  buf[j]='\0';
  return buf;
}
static FILE *search_module_file(Value packageV, const char *name, char **used_path_out){
  char *modpath = modname_to_path(name);
  Value pathV;
  const char *path = default_package_path();
  if (packageV.tag==VAL_TABLE &&
      tbl_get(packageV.as.t, V_str_from_c("path"), &pathV) &&
      pathV.tag==VAL_STR){
    path = pathV.as.s->data;
  }
  const char *p = path;
  while (*p){
    const char *q = p;
    while (*q && *q!=';') q++;
    size_t len = (size_t)(q - p);
    char *templ = (char*)xmalloc(len+1);
    memcpy(templ, p, len);
    templ[len]='\0';
    char *try_path = expand_one(templ, modpath);
    free(templ);
    FILE *f = fopen(try_path, "rb");
    if (f){
      if (used_path_out) *used_path_out = try_path; else free(try_path);
      free(modpath);
      return f;
    }
    free(try_path);
    if (*q==';') q++;
    p = q;
  }
  size_t nlen = strlen(name);
  char *fallback = (char*)xmalloc(nlen + 5); 
  memcpy(fallback, name, nlen);
  memcpy(fallback+nlen, ".lua", 5);
  FILE *f = fopen(fallback, "rb");
  if (f){
    if (used_path_out) *used_path_out = fallback; else free(fallback);
    free(modpath);
    return f;
  }
  free(fallback);
  free(modpath);
  return NULL;
}
Value builtin_require(struct VM *vm, int argc, Value *argv){
  if (argc < 1 || argv[0].tag != VAL_STR) {
    fprintf(stderr, "[LuaX]: require: expected string argument\n");
    return V_nil();
  }
  const char *name = argv[0].as.s->data;
  
  Value packageV = ensure_package(vm);
  Value loadedV;
  if (!tbl_get(packageV.as.t, V_str_from_c("loaded"), &loadedV) || loadedV.tag!=VAL_TABLE){
    loadedV = V_table();
    tbl_set(packageV.as.t, V_str_from_c("loaded"), loadedV);
  }
  Value already;
  if (tbl_get(loadedV.as.t, V_str_from_c(name), &already)){
    fprintf(stderr, "[LuaX]: module '%s' already loaded, returning cached value (tag=%d)\n", name, already.tag);
    return already;
  }
  Value preloadV;
  if (!tbl_get(packageV.as.t, V_str_from_c("preload"), &preloadV) || preloadV.tag!=VAL_TABLE){
    preloadV = V_table();
    tbl_set(packageV.as.t, V_str_from_c("preload"), preloadV);
  }
  Value loader;
  if (tbl_get(preloadV.as.t, V_str_from_c(name), &loader) && is_callable(loader)){
    Value ret = call_any(vm, loader, 0, NULL);
    if (ret.tag == VAL_NIL) ret = V_bool(1);
    tbl_set(loadedV.as.t, V_str_from_c(name), ret);
    return ret;
  }
  char *used_path = NULL;
  FILE *fp = search_module_file(packageV, name, &used_path);
  if (!fp){
    Value pathV;
    if (packageV.tag==VAL_TABLE &&
        tbl_get(packageV.as.t, V_str_from_c("path"), &pathV) &&
        pathV.tag==VAL_STR){
    }
    return V_nil();
  }
  
  
  AST *program = compile_chunk_from_FILE(fp);
  fclose(fp);
  
  // Set sentinel value to prevent infinite recursion
  tbl_set(loadedV.as.t, V_str_from_c(name), V_bool(1));
  
  Func *fn = xmalloc(sizeof(*fn));
  memset(fn, 0, sizeof(*fn));
  fn->params = (ASTVec){0};
  fn->vararg = false;
  fn->body   = program;
  fn->env    = vm->env;
  Value chunk; chunk.tag = VAL_FUNC; chunk.as.fn = fn;
  
  Value ret = call_any(vm, chunk, 0, NULL);
  
  // Update with actual return value (or true if module returned nil)
  if (ret.tag == VAL_NIL) {
    ret = V_bool(1);
  } else {
  }
  tbl_set(loadedV.as.t, V_str_from_c(name), ret);
  
  if (used_path) free(used_path);
  return ret;
}
int interpret(AST *root){
  VM vm; memset(&vm,0,sizeof(vm));
  vm.env = env_push(NULL);
  vm.co_yielding   = false;
  vm.co_yield_vals = V_table();
  vm.co_point.blk  = NULL;
  vm.co_point.pc   = 0;
  vm.co_call_env   = NULL;
  vm.active_co     = NULL;
  vm.err_frame = NULL;
  vm.err_obj   = V_nil();
    //env_add(vm.env, "package",   (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_package},   false);
  env_add(vm.env, "print",   (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_print},   false);
  env_add(vm.env, "select",  (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_select},  false);
  env_add(vm.env, "pairs",   (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_pairs},   false);
  env_add(vm.env, "ipairs",  (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_ipairs},  false);
  env_add(vm.env, "assert",         (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_assert},         false);
  env_add(vm.env, "collectgarbage", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_collectgarbage}, false);
  env_add(vm.env, "error",          (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_error},          false);
  env_add(vm.env, "_G",             (Value){.tag=VAL_CFUNC,.as.cfunc=builtin__G},             false);
  env_add(vm.env, "getmetatable",   (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_getmetatable},   false);
  env_add(vm.env, "setmetatable",   (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_setmetatable},   false);
  env_add(vm.env, "rawequal",       (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_rawequal},       false);
  env_add(vm.env, "rawget",         (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_rawget},         false);
  env_add(vm.env, "rawset",         (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_rawset},         false);
  env_add(vm.env, "load",           (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_load},           false);
  env_add(vm.env, "loadfile",       (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_loadfile},       false);
  env_add(vm.env, "require",        (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_require},        false);
  env_add(vm.env, "next",           (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_next},           false);
  env_add(vm.env, "tonumber",       (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_tonumber},       false);
  env_add(vm.env, "tostring",       (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_tostring},       false);
  env_add(vm.env, "type",           (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_type},           false);
  env_add(vm.env, "_VERSION",       V_str_from_c("LuaX 1.0"),                                 false);
  env_add(vm.env, "xpcall",         (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_xpcall},         false);
  env_add(vm.env, "pcall",          (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_pcall},          false);
  {
    Value package = V_table();
    Value loaded  = V_table();
    Value preload = V_table();
    Value searchers = V_table();
    const char *lua_path_env  = getenv("LUA_PATH");
    const char *rocks_tree1   = "/usr/local/share/lua/5.4/?.lua;/usr/local/share/lua/5.4/?/init.lua";
    const char *rocks_tree2   = "/usr/share/lua/5.4/?.lua;/usr/share/lua/5.4/?/init.lua";
    const char *local_tree    = "?.lua;?/init.lua;./?.lua;./?/init.lua";
    char path_buf[2048];
    if (lua_path_env && *lua_path_env) {
      snprintf(path_buf, sizeof(path_buf), "%s;%s;%s;%s",
               lua_path_env, local_tree, rocks_tree1, rocks_tree2);
    } else {
      snprintf(path_buf, sizeof(path_buf), "%s;%s;%s",
               local_tree, rocks_tree1, rocks_tree2);
    }
    const char *lua_cpath_env = getenv("LUA_CPATH");
    const char *cpath_default = "./?.so;/usr/local/lib/lua/5.4/?.so;/usr/lib/lua/5.4/?.so";
    const char *cpath_final   = (lua_cpath_env && *lua_cpath_env) ? lua_cpath_env : cpath_default;
    tbl_set(package.as.t, V_str_from_c("loaded"),    loaded);
    tbl_set(package.as.t, V_str_from_c("preload"),   preload);
    tbl_set(package.as.t, V_str_from_c("searchers"), searchers);
    tbl_set(package.as.t, V_str_from_c("path"),      V_str_from_c(path_buf));
    tbl_set(package.as.t, V_str_from_c("cpath"),     V_str_from_c(cpath_final));
    env_add(vm.env, "package",  package, false);
    env_add(vm.env, "Packages", package, false);
  }
  register_package_lib(&vm);
  register_coroutine_lib(&vm);
  register_math_lib(&vm);
  register_string_lib(&vm);
  register_table_lib(&vm);
  register_utf8_lib(&vm);
  register_os_lib(&vm);
  register_io_lib(&vm);
  register_debug_lib(&vm);
  register_random_lib(&vm);
  register_date_lib(&vm);
  register_exception_lib(&vm);
  register_async_lib(&vm);
  register_class_lib(&vm);
  exec_stmt(&vm, root);
  return 0;
}
// Add these to interpreter.c

// Create a VM for REPL use (persistent across commands)
VM *vm_create_repl(void) {
    VM *vm = (VM*)malloc(sizeof(VM));
    if (!vm) return NULL;
    
    memset(vm, 0, sizeof(VM));
    vm->env = env_push(NULL);
    vm->co_yielding = false;
    vm->co_yield_vals = V_table();
    vm->co_point.blk = NULL;
    vm->co_point.pc = 0;
    vm->co_call_env = NULL;
    vm->active_co = NULL;
    vm->err_frame = NULL;
    vm->err_obj = V_nil();
    
    // Register all built-in functions (same as interpret())
    env_add(vm->env, "print", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_print}, false);
    env_add(vm->env, "select", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_select}, false);
    env_add(vm->env, "pairs", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_pairs}, false);
    env_add(vm->env, "ipairs", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_ipairs}, false);
    env_add(vm->env, "assert", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_assert}, false);
    env_add(vm->env, "collectgarbage", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_collectgarbage}, false);
    env_add(vm->env, "error", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_error}, false);
    env_add(vm->env, "_G", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin__G}, false);
    env_add(vm->env, "getmetatable", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_getmetatable}, false);
    env_add(vm->env, "setmetatable", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_setmetatable}, false);
    env_add(vm->env, "rawequal", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_rawequal}, false);
    env_add(vm->env, "rawget", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_rawget}, false);
    env_add(vm->env, "rawset", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_rawset}, false);
    env_add(vm->env, "load", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_load}, false);
    env_add(vm->env, "loadfile", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_loadfile}, false);
    env_add(vm->env, "require", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_require}, false);
    env_add(vm->env, "next", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_next}, false);
    env_add(vm->env, "tonumber", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_tonumber}, false);
    env_add(vm->env, "tostring", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_tostring}, false);
    env_add(vm->env, "type", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_type}, false);
    env_add(vm->env, "_VERSION", V_str_from_c("LuaX 1.0"), false);
    env_add(vm->env, "xpcall", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_xpcall}, false);
    env_add(vm->env, "pcall", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_pcall}, false);
    
    // Initialize package table (same as interpret())
    Value package = V_table();
    Value loaded = V_table();
    Value preload = V_table();
    Value searchers = V_table();
    
    const char *lua_path_env = getenv("LUA_PATH");
    const char *rocks_tree1 = "/usr/local/share/lua/5.4/?.lua;/usr/local/share/lua/5.4/?/init.lua";
    const char *rocks_tree2 = "/usr/share/lua/5.4/?.lua;/usr/share/lua/5.4/?/init.lua";
    const char *local_tree = "?.lua;?/init.lua;./?.lua;./?/init.lua";
    char path_buf[2048];
    
    if (lua_path_env && *lua_path_env) {
        snprintf(path_buf, sizeof(path_buf), "%s;%s;%s;%s",
                 lua_path_env, local_tree, rocks_tree1, rocks_tree2);
    } else {
        snprintf(path_buf, sizeof(path_buf), "%s;%s;%s",
                 local_tree, rocks_tree1, rocks_tree2);
    }
    
    const char *lua_cpath_env = getenv("LUA_CPATH");
    const char *cpath_default = "./?.so;/usr/local/lib/lua/5.4/?.so;/usr/lib/lua/5.4/?.so";
    const char *cpath_final = (lua_cpath_env && *lua_cpath_env) ? lua_cpath_env : cpath_default;
    
    tbl_set(package.as.t, V_str_from_c("loaded"), loaded);
    tbl_set(package.as.t, V_str_from_c("preload"), preload);
    tbl_set(package.as.t, V_str_from_c("searchers"), searchers);
    tbl_set(package.as.t, V_str_from_c("path"), V_str_from_c(path_buf));
    tbl_set(package.as.t, V_str_from_c("cpath"), V_str_from_c(cpath_final));
    
    env_add(vm->env, "package", package, false);
    env_add(vm->env, "Packages", package, false);
    
    // Register all library modules
    register_package_lib(vm);
    register_coroutine_lib(vm);
    register_math_lib(vm);
    register_string_lib(vm);
    register_table_lib(vm);
    register_utf8_lib(vm);
    register_os_lib(vm);
    register_io_lib(vm);
    register_debug_lib(vm);
    register_random_lib(vm);
    register_date_lib(vm);
    register_exception_lib(vm);
    register_async_lib(vm);
    register_class_lib(vm);
    
    return vm;
}

// Execute a statement in REPL mode (persistent VM)
void exec_stmt_repl(VM *vm, AST *n) {
    exec_stmt(vm, n);
}
