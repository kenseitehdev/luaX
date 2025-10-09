// lib/debug.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/interpreter.h"

/* ---------- helpers ---------- */

static Value str_cat2(const char *a, const char *b) {
  size_t la = a ? strlen(a) : 0;
  size_t lb = b ? strlen(b) : 0;
  char *buf = (char*)malloc(la + lb + 1);
  if (!buf) return V_str_from_c("(oom)");
  if (la) memcpy(buf, a, la);
  if (lb) memcpy(buf + la, b, lb);
  buf[la + lb] = '\0';
  Value v = V_str_from_c(buf);
  free(buf);
  return v;
}

/* ---------- debug.traceback([message]) ---------- */
static Value dbg_traceback(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  const char *banner = "stack traceback:\n  (no stack; not implemented)\n";
  if (argc >= 1 && argv[0].tag == VAL_STR) {
    Value tmp = str_cat2(argv[0].as.s->data, "\n");
    Value out = str_cat2(tmp.as.s->data, banner);
    return out;
  }
  return V_str_from_c(banner);
}

/* ---------- debug.getinfo(f)  -- function-only for now ---------- */
static Value dbg_getinfo(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1) return V_nil();

  Value info = V_table();

  if (argv[0].tag == VAL_CFUNC) {
    tbl_set_public(info.as.t, V_str_from_c("what"),      V_str_from_c("C"));
    tbl_set_public(info.as.t, V_str_from_c("func"),      argv[0]);
    tbl_set_public(info.as.t, V_str_from_c("nups"),      V_int(0));
    tbl_set_public(info.as.t, V_str_from_c("isvararg"),  V_bool(0));
    tbl_set_public(info.as.t, V_str_from_c("linedefined"), V_int(0));
    return info;
  }

  if (argv[0].tag == VAL_FUNC && argv[0].as.fn) {
    Func *fn = argv[0].as.fn;
    tbl_set_public(info.as.t, V_str_from_c("what"),      V_str_from_c("Lua"));
    tbl_set_public(info.as.t, V_str_from_c("func"),      argv[0]);
    tbl_set_public(info.as.t, V_str_from_c("nups"),      V_int(0)); /* no upvalues tracked */
    tbl_set_public(info.as.t, V_str_from_c("isvararg"),  V_bool(fn->vararg ? 1 : 0));
    int line = fn->body ? fn->body->line : 0;
    tbl_set_public(info.as.t, V_str_from_c("linedefined"), V_int(line));
    /* optional fields like name/namewhat/source omitted for now */
    return info;
  }

  /* unsupported target (like numeric level) */
  return V_nil();
}

/* ---------- debug.getmetatable(x)  (local minimal impl) ---------- */
static Value dbg_getmetatable(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_TABLE) return V_nil();
  Value meta;
  if (tbl_get_public(argv[0].as.t, V_str_from_c("__metatable"), &meta)) return meta;
  return V_nil();
}

/* placeholders to keep API surface */
static Value dbg_sethook(struct VM *vm, int argc, Value *argv) { (void)vm;(void)argc;(void)argv; return V_nil(); }
static Value dbg_gethook(struct VM *vm, int argc, Value *argv) { (void)vm;(void)argc;(void)argv; return V_nil(); }
static Value dbg_upvalueid(struct VM *vm, int argc, Value *argv){ (void)vm;(void)argc;(void)argv; return V_nil(); }
static Value dbg_getupvalue(struct VM *vm, int argc, Value *argv){ (void)vm;(void)argc;(void)argv; return V_nil(); }
static Value dbg_setupvalue(struct VM *vm, int argc, Value *argv){ (void)vm;(void)argc;(void)argv; return V_nil(); }

/* ---------- registration ---------- */

void register_debug_lib(struct VM *vm) {
  Value debug = V_table();

  tbl_set_public(debug.as.t, V_str_from_c("traceback"),   (Value){.tag=VAL_CFUNC, .as.cfunc=dbg_traceback});
  tbl_set_public(debug.as.t, V_str_from_c("getinfo"),     (Value){.tag=VAL_CFUNC, .as.cfunc=dbg_getinfo});
  tbl_set_public(debug.as.t, V_str_from_c("getmetatable"),(Value){.tag=VAL_CFUNC, .as.cfunc=dbg_getmetatable});

  /* stubs to complete surface */
  tbl_set_public(debug.as.t, V_str_from_c("sethook"),     (Value){.tag=VAL_CFUNC, .as.cfunc=dbg_sethook});
  tbl_set_public(debug.as.t, V_str_from_c("gethook"),     (Value){.tag=VAL_CFUNC, .as.cfunc=dbg_gethook});
  tbl_set_public(debug.as.t, V_str_from_c("upvalueid"),   (Value){.tag=VAL_CFUNC, .as.cfunc=dbg_upvalueid});
  tbl_set_public(debug.as.t, V_str_from_c("getupvalue"),  (Value){.tag=VAL_CFUNC, .as.cfunc=dbg_getupvalue});
  tbl_set_public(debug.as.t, V_str_from_c("setupvalue"),  (Value){.tag=VAL_CFUNC, .as.cfunc=dbg_setupvalue});

  env_add_public(vm->env, "debug", debug, false);
}
