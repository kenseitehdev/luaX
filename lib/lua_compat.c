// lua_compat.c
// Implementation of Lua C API compatibility layer

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "lua_compat.h"

// ===== Internal helpers =====

// Normalize negative indices to positive
static int abs_index(lua_State *L, int idx) {
  if (idx > 0 || idx <= LUA_REGISTRYINDEX) return idx;
  return L->top + idx + 1;
}

// Get value at stack index
static Value *stack_at(lua_State *L, int idx) {
  if (idx > 0) {
    if (idx > L->top) return NULL;
    return &L->stack[idx - 1];
  } else if (idx < 0 && idx > LUA_REGISTRYINDEX) {
    int pos = L->top + idx;
    if (pos < 0) return NULL;
    return &L->stack[pos];
  }
  return NULL;
}

// Global registry table (stored in VM env root)
static Table *get_registry(lua_State *L) {
  static Table *reg = NULL;
  if (!reg) {
    Value v = V_table();
    reg = v.as.t;
    // Store in root env
    env_add(env_root(L->env), "LUA_REGISTRY", v, false);
  }
  return reg;
}

// ===== Stack manipulation =====

int lua_gettop(lua_State *L) {
  return L->top;
}

void lua_settop(lua_State *L, int idx) {
  if (idx >= 0) {
    while (L->top < idx) L->stack[L->top++] = V_nil();
    L->top = idx;
  } else {
    L->top += idx + 1;
    if (L->top < 0) L->top = 0;
  }
}

void lua_pushvalue(lua_State *L, int idx) {
  Value *v = stack_at(L, idx);
  if (v) vm_push(L, *v);
  else vm_push(L, V_nil());
}

void lua_remove(lua_State *L, int idx) {
  int abs = abs_index(L, idx);
  if (abs < 1 || abs > L->top) return;
  for (int i = abs; i < L->top; i++) {
    L->stack[i-1] = L->stack[i];
  }
  L->top--;
}

void lua_insert(lua_State *L, int idx) {
  int abs = abs_index(L, idx);
  if (abs < 1 || abs > L->top) return;
  Value tmp = L->stack[L->top - 1];
  for (int i = L->top - 1; i > abs - 1; i--) {
    L->stack[i] = L->stack[i-1];
  }
  L->stack[abs - 1] = tmp;
}

void lua_replace(lua_State *L, int idx) {
  Value *v = stack_at(L, idx);
  if (v && L->top > 0) {
    *v = L->stack[--L->top];
  }
}

int lua_checkstack(lua_State *L, int extra) {
  return (L->top + extra < 256);
}

// ===== Access functions =====

int lua_type(lua_State *L, int idx) {
  Value *v = stack_at(L, idx);
  if (!v) return LUA_TNONE;
  switch (v->tag) {
    case VAL_NIL: return LUA_TNIL;
    case VAL_BOOL: return LUA_TBOOLEAN;
    case VAL_INT:
    case VAL_NUM: return LUA_TNUMBER;
    case VAL_STR: return LUA_TSTRING;
    case VAL_TABLE: return LUA_TTABLE;
    case VAL_CFUNC:
    case VAL_FUNC: return LUA_TFUNCTION;
    case VAL_COROUTINE: return LUA_TTHREAD;
    default: return LUA_TNONE;
  }
}

const char *lua_typename(lua_State *L, int tp) {
  (void)L;
  static const char *names[] = {
    "nil", "boolean", "userdata", "number", "string",
    "table", "function", "userdata", "thread"
  };
  if (tp >= 0 && tp <= LUA_TTHREAD) return names[tp];
  return "no value";
}

int lua_isnumber(lua_State *L, int idx) {
  int t = lua_type(L, idx);
  return t == LUA_TNUMBER;
}

int lua_isstring(lua_State *L, int idx) {
  int t = lua_type(L, idx);
  return t == LUA_TSTRING || t == LUA_TNUMBER;
}

int lua_iscfunction(lua_State *L, int idx) {
  Value *v = stack_at(L, idx);
  return v && v->tag == VAL_CFUNC;
}

int lua_isboolean(lua_State *L, int idx) {
  return lua_type(L, idx) == LUA_TBOOLEAN;
}

int lua_isnoneornil(lua_State *L, int idx) {
  int t = lua_type(L, idx);
  return t == LUA_TNONE || t == LUA_TNIL;
}

lua_Number lua_tonumber(lua_State *L, int idx) {
  Value *v = stack_at(L, idx);
  if (!v) return 0;
  if (v->tag == VAL_NUM) return v->as.n;
  if (v->tag == VAL_INT) return (lua_Number)v->as.i;
  return 0;
}

lua_Integer lua_tointeger(lua_State *L, int idx) {
  Value *v = stack_at(L, idx);
  if (!v) return 0;
  if (v->tag == VAL_INT) return v->as.i;
  if (v->tag == VAL_NUM) return (lua_Integer)v->as.n;
  return 0;
}

int lua_toboolean(lua_State *L, int idx) {
  Value *v = stack_at(L, idx);
  if (!v) return 0;
  return as_truthy(*v);
}

const char *lua_tolstring(lua_State *L, int idx, size_t *len) {
  Value *v = stack_at(L, idx);
  if (!v || v->tag != VAL_STR) {
    if (len) *len = 0;
    return NULL;
  }
  if (len) *len = v->as.s->len;
  return v->as.s->data;
}

size_t lua_objlen(lua_State *L, int idx) {
  Value *v = stack_at(L, idx);
  if (!v) return 0;
  if (v->tag == VAL_STR) return v->as.s->len;
  Value len = op_len(*v);
  if (len.tag == VAL_INT) return len.as.i;
  return 0;
}

lua_CFunction lua_tocfunction(lua_State *L, int idx) {
  Value *v = stack_at(L, idx);
  if (!v || v->tag != VAL_CFUNC) return NULL;
  return (lua_CFunction)v->as.cfunc;
}

// ===== Push functions =====

void lua_pushnil(lua_State *L) {
  vm_push(L, V_nil());
}

void lua_pushnumber(lua_State *L, lua_Number n) {
  vm_push(L, V_num(n));
}

void lua_pushinteger(lua_State *L, lua_Integer n) {
  vm_push(L, V_int(n));
}

void lua_pushlstring(lua_State *L, const char *s, size_t len) {
  Str *str = Str_new_len(s, len);
  Value v; v.tag = VAL_STR; v.as.s = str;
  vm_push(L, v);
}

void lua_pushstring(lua_State *L, const char *s) {
  if (!s) lua_pushnil(L);
  else vm_push(L, V_str_from_c(s));
}

const char *lua_pushfstring(lua_State *L, const char *fmt, ...) {
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  lua_pushstring(L, buf);
  return buf;
}

void lua_pushcclosure(lua_State *L, lua_CFunction fn, int n) {
  // For now, ignore upvalues (n) - full implementation would store them
  (void)n;
  // Store the function pointer directly - it will be called via compat layer
  Value v; v.tag = VAL_CFUNC;
  // Note: This is a type mismatch, but we handle it in compat_call_cmodule_init
  v.as.cfunc = (CFunc)(void*)fn;
  vm_push(L, v);
}

void lua_pushboolean(lua_State *L, int b) {
  vm_push(L, V_bool(b != 0));
}

// ===== Get functions =====

void lua_gettable(lua_State *L, int idx) {
  Value *tbl = stack_at(L, idx);
  Value key = vm_pop(L);
  if (tbl && tbl->tag == VAL_TABLE) {
    Value result;
    if (tbl_get(tbl->as.t, key, &result)) {
      vm_push(L, result);
    } else {
      vm_push(L, V_nil());
    }
  } else {
    vm_push(L, V_nil());
  }
}

void lua_getfield(lua_State *L, int idx, const char *k) {
  lua_pushstring(L, k);
  lua_gettable(L, idx < 0 ? idx - 1 : idx);
}

void lua_rawget(lua_State *L, int idx) {
  lua_gettable(L, idx); // same for now
}

void lua_rawgeti(lua_State *L, int idx, int n) {
  Value *tbl = stack_at(L, idx);
  if (tbl && tbl->tag == VAL_TABLE) {
    Value result;
    if (tbl_get(tbl->as.t, V_int(n), &result)) {
      vm_push(L, result);
    } else {
      vm_push(L, V_nil());
    }
  } else {
    vm_push(L, V_nil());
  }
}

void lua_createtable(lua_State *L, int narr, int nrec) {
  (void)narr; (void)nrec;
  vm_push(L, V_table());
}

int lua_getmetatable(lua_State *L, int idx) {
  Value *v = stack_at(L, idx);
  if (!v || v->tag != VAL_TABLE) return 0;
  
  Value mt_key = V_str_from_c("__metatable");
  Value mt;
  if (tbl_get(v->as.t, mt_key, &mt)) {
    vm_push(L, mt);
    return 1;
  }
  return 0;
}

// ===== Set functions =====

void lua_settable(lua_State *L, int idx) {
  Value *tbl = stack_at(L, idx);
  Value val = vm_pop(L);
  Value key = vm_pop(L);
  if (tbl && tbl->tag == VAL_TABLE) {
    tbl_set(tbl->as.t, key, val);
  }
}

void lua_setfield(lua_State *L, int idx, const char *k) {
  Value val = vm_pop(L);
  Value *tbl = stack_at(L, idx);
  if (tbl && tbl->tag == VAL_TABLE) {
    tbl_set(tbl->as.t, V_str_from_c(k), val);
  }
}

void lua_rawset(lua_State *L, int idx) {
  lua_settable(L, idx); // same for now
}

void lua_rawseti(lua_State *L, int idx, int n) {
  Value val = vm_pop(L);
  Value *tbl = stack_at(L, idx);
  if (tbl && tbl->tag == VAL_TABLE) {
    tbl_set(tbl->as.t, V_int(n), val);
  }
}

int lua_setmetatable(lua_State *L, int idx) {
  Value *v = stack_at(L, idx);
  Value mt = vm_pop(L);
  if (!v || v->tag != VAL_TABLE) return 0;
  
  tbl_set(v->as.t, V_str_from_c("__metatable"), mt);
  return 1;
}

// ===== Call functions =====

void lua_call(lua_State *L, int nargs, int nresults) {
  (void)nresults;
  
  Value fn = L->stack[L->top - nargs - 1];
  Value *args = &L->stack[L->top - nargs];
  
  L->top -= (nargs + 1);
  
  Value result = call_any_public(L, fn, nargs, args);
  vm_push(L, result);
}

int lua_pcall(lua_State *L, int nargs, int nresults, int errfunc) {
  (void)errfunc;
  lua_call(L, nargs, nresults);
  return LUA_OK;
}

// ===== Miscellaneous =====

int lua_error(lua_State *L) {
  Value err = vm_pop(L);
  vm_raise(L, err);
  return 0;
}

int lua_next(lua_State *L, int idx) {
  // Simplified - full implementation needs table iteration state
  return 0;
}

void lua_concat(lua_State *L, int n) {
  if (n == 0) {
    lua_pushstring(L, "");
    return;
  }
  // Simplified concatenation
  char buf[4096] = "";
  for (int i = 0; i < n; i++) {
    const char *s = lua_tolstring(L, -n + i, NULL);
    if (s) strncat(buf, s, sizeof(buf) - strlen(buf) - 1);
  }
  lua_settop(L, -n - 1);
  lua_pushstring(L, buf);
}

// ===== Global get/set =====

void lua_getglobal(lua_State *L, const char *name) {
  Value v;
  if (env_get(env_root(L->env), name, &v)) {
    vm_push(L, v);
  } else {
    vm_push(L, V_nil());
  }
}

void lua_setglobal(lua_State *L, const char *name) {
  Value v = vm_pop(L);
  env_add(env_root(L->env), name, v, false);
}

// ===== Auxiliary library =====

void luaL_register(lua_State *L, const char *libname, const luaL_Reg *l) {
  if (libname) {
    lua_newtable(L);
  }
  
  for (; l->name; l++) {
    lua_pushcfunction(L, l->func);
    lua_setfield(L, -2, l->name);
  }
  
  if (libname) {
    lua_setglobal(L, libname);
  }
}

void luaL_openlib(lua_State *L, const char *libname, const luaL_Reg *l, int nup) {
  (void)nup;
  luaL_register(L, libname, l);
}

int luaL_error(lua_State *L, const char *fmt, ...) {
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  lua_pushstring(L, buf);
  return lua_error(L);
}

void luaL_checktype(lua_State *L, int narg, int t) {
  if (lua_type(L, narg) != t) {
    luaL_error(L, "bad argument #%d (expected %s, got %s)",
               narg, lua_typename(L, t), lua_typename(L, lua_type(L, narg)));
  }
}

void luaL_checkany(lua_State *L, int narg) {
  if (lua_type(L, narg) == LUA_TNONE) {
    luaL_error(L, "bad argument #%d (value expected)", narg);
  }
}

lua_Integer luaL_checkinteger(lua_State *L, int narg) {
  if (!lua_isnumber(L, narg)) {
    luaL_error(L, "bad argument #%d (number expected)", narg);
  }
  return lua_tointeger(L, narg);
}

lua_Number luaL_checknumber(lua_State *L, int narg) {
  if (!lua_isnumber(L, narg)) {
    luaL_error(L, "bad argument #%d (number expected)", narg);
  }
  return lua_tonumber(L, narg);
}

const char *luaL_checklstring(lua_State *L, int narg, size_t *len) {
  if (!lua_isstring(L, narg)) {
    luaL_error(L, "bad argument #%d (string expected)", narg);
  }
  return lua_tolstring(L, narg, len);
}

const char *luaL_checkstring(lua_State *L, int narg) {
  return luaL_checklstring(L, narg, NULL);
}

const char *luaL_optlstring(lua_State *L, int narg, const char *def, size_t *len) {
  if (lua_isnoneornil(L, narg)) {
    if (len) *len = (def ? strlen(def) : 0);
    return def;
  }
  return luaL_checklstring(L, narg, len);
}

const char *luaL_optstring(lua_State *L, int narg, const char *def) {
  return luaL_optlstring(L, narg, def, NULL);
}

lua_Integer luaL_optinteger(lua_State *L, int narg, lua_Integer def) {
  return lua_isnoneornil(L, narg) ? def : luaL_checkinteger(L, narg);
}

lua_Number luaL_optnumber(lua_State *L, int narg, lua_Number def) {
  return lua_isnoneornil(L, narg) ? def : luaL_checknumber(L, narg);
}

int luaL_checkoption(lua_State *L, int narg, const char *def, const char *const lst[]);

// Forward declaration for luaL_optstring
const char *luaL_optstring(lua_State *L, int narg, const char *def);

int luaL_checkoption(lua_State *L, int narg, const char *def, const char *const lst[]) {
  const char *name = (def && lua_isnoneornil(L, narg)) ? def : luaL_checkstring(L, narg);
  for (int i = 0; lst[i]; i++) {
    if (strcmp(lst[i], name) == 0) return i;
  }
  luaL_error(L, "invalid option '%s'", name);
  return 0;
}

void luaL_setmetatable(lua_State *L, const char *tname) {
  lua_getfield(L, LUA_REGISTRYINDEX, tname);
  lua_setmetatable(L, -2);
}

int luaL_newmetatable(lua_State *L, const char *tname) {
  lua_getregistry(L);
  lua_getfield(L, -1, tname);
  if (!lua_isnil(L, -1)) {
    lua_remove(L, -2);
    return 0;
  }
  lua_pop(L, 1);
  lua_newtable(L);
  lua_pushvalue(L, -1);
  lua_setfield(L, -3, tname);
  lua_remove(L, -2);
  return 1;
}

int luaL_argerror(lua_State *L, int narg, const char *extramsg) {
  return luaL_error(L, "bad argument #%d (%s)", narg, extramsg);
}

// ===== Buffer management =====

#define LUAL_BUFFERSIZE 1024

void luaL_buffinit(lua_State *L, luaL_Buffer *B) {
  B->L = L;
  B->b = (char*)malloc(LUAL_BUFFERSIZE);
  B->size = LUAL_BUFFERSIZE;
  B->n = 0;
}

char *luaL_prepbuffer(luaL_Buffer *B) {
  if (B->n + LUAL_BUFFERSIZE > B->size) {
    B->size = (B->size + LUAL_BUFFERSIZE) * 2;
    B->b = (char*)realloc(B->b, B->size);
  }
  return B->b + B->n;
}

void luaL_addlstring(luaL_Buffer *B, const char *s, size_t l) {
  if (B->n + l > B->size) {
    B->size = (B->n + l) * 2;
    B->b = (char*)realloc(B->b, B->size);
  }
  memcpy(B->b + B->n, s, l);
  B->n += l;
}

void luaL_addstring(luaL_Buffer *B, const char *s) {
  luaL_addlstring(B, s, strlen(s));
}

void luaL_addvalue(luaL_Buffer *B) {
  size_t len;
  const char *s = lua_tolstring(B->L, -1, &len);
  if (s) luaL_addlstring(B, s, len);
  lua_pop(B->L, 1);
}

void luaL_pushresult(luaL_Buffer *B) {
  lua_pushlstring(B->L, B->b, B->n);
  free(B->b);
  B->b = NULL;
  B->n = 0;
  B->size = 0;
}

// ===== Registry and references =====

void lua_getregistry(lua_State *L) {
  Table *reg = get_registry(L);
  Value v; v.tag = VAL_TABLE; v.as.t = reg;
  vm_push(L, v);
}

static int next_ref = 1;

int luaL_ref(lua_State *L, int t) {
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return LUA_REFNIL;
  }
  
  Value *tbl = stack_at(L, t);
  if (!tbl || tbl->tag != VAL_TABLE) {
    lua_pop(L, 1);
    return LUA_NOREF;
  }
  
  int ref = next_ref++;
  Value val = vm_pop(L);
  tbl_set(tbl->as.t, V_int(ref), val);
  return ref;
}

void luaL_unref(lua_State *L, int t, int ref) {
  if (ref < 0) return;
  Value *tbl = stack_at(L, t);
  if (!tbl || tbl->tag != VAL_TABLE) return;
  tbl_set(tbl->as.t, V_int(ref), V_nil());
}

// ===== Main compatibility wrapper =====

// This is the key function that bridges standard Lua C modules to your VM
Value compat_call_cmodule_init(VM *vm, int (*lua_init)(lua_State*), const char *modname) {
  fprintf(stderr, "[COMPAT] Calling C module init for: %s\n", modname);
  
  // Save VM state
  int old_top = vm->top;
  
  // Call the standard Lua init function
  // It expects lua_State* (which is just VM*) and returns number of results
  int nresults = lua_init((lua_State*)vm);
  
  fprintf(stderr, "[COMPAT] C module init returned %d results\n", nresults);
  
  // Collect results
  if (nresults == 0) {
    // Module returned nothing, return true
    return V_bool(true);
  }
  
  if (nresults == 1) {
    // Single return value
    if (vm->top > old_top) {
      return vm->stack[vm->top - 1];
    }
    return V_bool(true);
  }
  
  // Multiple returns - package in a table
  Value result = V_table();
  for (int i = 0; i < nresults && (old_top + i) < vm->top; i++) {
    tbl_set(result.as.t, V_int(i + 1), vm->stack[old_top + i]);
  }
  
  // Clean up stack
  vm->top = old_top;
  
  return result;
}
