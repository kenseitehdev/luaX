#include "../include/builtins.h"

Value builtin_load(struct VM *vm, int argc, Value *argv){
  if (argc<1 || argv[0].tag!=VAL_STR) return V_nil();
  FILE *fp = open_string_as_FILE(argv[0].as.s->data);
  if (!fp) return V_nil();
  AST *program = compile_chunk_from_FILE(fp);
  fclose(fp);
  Func *fn = xmalloc(sizeof(*fn)); memset(fn,0,sizeof(*fn));
  fn->params=(ASTVec){0}; fn->vararg=false; fn->body=program; fn->env=vm->env;
  Value v; v.tag=VAL_FUNC; v.as.fn=fn; return v;
}
Value builtin_loadfile(struct VM *vm, int argc, Value *argv){
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
Value builtin_pcall(struct VM *vm, int argc, Value *argv){
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
Value builtin_xpcall(struct VM *vm, int argc, Value *argv){
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

Value builtin_select(struct VM *vm, int argc, Value *argv){
  (void)vm;
  if (argc < 1) return V_nil();
  if (argv[0].tag == VAL_STR && argv[0].as.s->len > 0 &&
      argv[0].as.s->data[0] == '#') {
    return V_int(argc - 1);
  }
  long long i = 0;
  if (argv[0].tag == VAL_INT) i = argv[0].as.i;
  else if (argv[0].tag == VAL_NUM) i = (long long)argv[0].as.n;
  else i = 0;
  if (i < 1 || i > argc - 1) return V_nil();
  return argv[i];
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

/* getmetatable with protection: return mt.__metatable if present */
Value builtin_getmetatable(struct VM *vm, int argc, Value *argv){
  (void)vm;
  if (argc<1 || argv[0].tag!=VAL_TABLE) return V_nil();
  Value mt;
  if (!tbl_get(argv[0].as.t, V_str_from_c(MT_STORE), &mt)) return V_nil();
  if (mt.tag != VAL_TABLE) return V_nil();
  Value prot;
  if (tbl_get(mt.as.t, V_str_from_c(PROT_KEY), &prot)) return prot;
  return mt;
}

/* setmetatable obeys protection: if current mt has __metatable, error */
Value builtin_setmetatable(struct VM *vm, int argc, Value *argv){
  if (argc<2 || argv[0].tag!=VAL_TABLE || (argv[1].tag!=VAL_TABLE && argv[1].tag!=VAL_NIL))
    return V_nil();
  Value cur;
  if (tbl_get(argv[0].as.t, V_str_from_c(MT_STORE), &cur) && cur.tag==VAL_TABLE){
    Value prot;
    if (tbl_get(cur.as.t, V_str_from_c(PROT_KEY), &prot) && prot.tag!=VAL_NIL){
      vm_raise(vm, V_str_from_c("cannot change a protected metatable"));
      return V_nil();
    }
  }
  if (argv[1].tag==VAL_NIL){
    tbl_set(argv[0].as.t, V_str_from_c(MT_STORE), V_nil());
  } else {
    tbl_set(argv[0].as.t, V_str_from_c(MT_STORE), argv[1]);
  }
  return argv[0];
}

Value builtin_assert(struct VM *vm, int argc, Value *argv){
  if (argc < 1) {
    vm_raise(vm, V_str_from_c("assertion failed!"));
    return V_nil();
  }
  
  if (!as_truthy(argv[0])) {
    const char *msg = (argc >= 2 && argv[1].tag == VAL_STR) 
                      ? argv[1].as.s->data 
                      : "assertion failed!";
    vm_raise(vm, V_str_from_c(msg));
    return V_nil();
  }
  
  return argv[0];
}

Value builtin_collectgarbage(struct VM *vm, int argc, Value *argv){
  const char *mode = "collect";
  if (argc >= 1 && argv[0].tag == VAL_STR && argv[0].as.s && argv[0].as.s->data)
    mode = argv[0].as.s->data;

  if (strcmp(mode, "collect") == 0) {
    shim_collect(vm);
    return V_nil();
  }
  if (strcmp(mode, "count") == 0) {
    size_t bytes = vm_gc_total_bytes(vm);
    return V_num((double)bytes / 1024.0);
  }
  if (strcmp(mode, "stop") == 0) {
    shim_stop(vm);
    return V_nil();
  }
  if (strcmp(mode, "restart") == 0) {
    shim_restart(vm);
    return V_nil();
  }
  if (strcmp(mode, "step") == 0) {
    int kb = (argc >= 2) ? to_int_val(argv[1], 0) : 0;
    int done = shim_step(vm, kb);
    return V_bool(done ? 1 : 0);
  }
  if (strcmp(mode, "isrunning") == 0) {
    return V_bool(shim_isrunning(vm) ? 1 : 0);
  }
  if (strcmp(mode, "setpause") == 0) {
    int pause = (argc >= 2) ? to_int_val(argv[1], g_gc.pause) : g_gc.pause;
    return V_int(shim_setpause(vm, pause));
  }
  if (strcmp(mode, "setstepmul") == 0) {
    int mul = (argc >= 2) ? to_int_val(argv[1], g_gc.stepmul) : g_gc.stepmul;
    return V_int(shim_setstepmul(vm, mul));
  }
  if (strcmp(mode, "incremental") == 0) {
    int pause    = (argc >= 2) ? to_int_val(argv[1], g_gc.pause)     : g_gc.pause;
    int stepmul  = (argc >= 3) ? to_int_val(argv[2], g_gc.stepmul)   : g_gc.stepmul;
    int stepsize = (argc >= 4) ? to_int_val(argv[3], g_gc.stepsize_kb) : g_gc.stepsize_kb;
    shim_set_incremental(vm, pause, stepmul, stepsize);
    return V_nil();
  }
  if (strcmp(mode, "generational") == 0) {
    int minormul = (argc >= 2) ? to_int_val(argv[1], g_gc.minormul) : g_gc.minormul;
    int majormul = (argc >= 3) ? to_int_val(argv[2], g_gc.majormul) : g_gc.majormul;
    shim_set_generational(vm, minormul, majormul);
    return V_nil();
  }

  /* unknown mode -> nil (Lua returns nil + error; single-return VM => nil) */
  return V_nil();
}
