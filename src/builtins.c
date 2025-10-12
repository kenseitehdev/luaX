#include "../include/builtins.h"

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
