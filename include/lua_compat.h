// lua_compat.h
// Lua C API compatibility layer for LuaRocks modules
// This allows standard Lua C modules to work with your VM

#ifndef LUA_COMPAT_H
#define LUA_COMPAT_H

#include <stddef.h>
#include <stdarg.h>
#include "../include/interpreter.h"

// ===== Type aliases =====
typedef struct VM lua_State;
typedef long long lua_Integer;
typedef double lua_Number;
typedef int (*lua_CFunction)(lua_State *L);

// ===== Constants =====
#define LUA_OK          0
#define LUA_YIELD       1
#define LUA_ERRRUN      2
#define LUA_ERRSYNTAX   3
#define LUA_ERRMEM      4
#define LUA_ERRERR      5

// Pseudo-indices for registry and globals
#define LUA_REGISTRYINDEX   (-10000)
#define LUA_ENVIRONINDEX    (-10001)
#define LUA_GLOBALSINDEX    (-10002)

// Types
#define LUA_TNONE          (-1)
#define LUA_TNIL           0
#define LUA_TBOOLEAN       1
#define LUA_TLIGHTUSERDATA 2
#define LUA_TNUMBER        3
#define LUA_TSTRING        4
#define LUA_TTABLE         5
#define LUA_TFUNCTION      6
#define LUA_TUSERDATA      7
#define LUA_TTHREAD        8

// ===== Stack manipulation =====
int lua_gettop(lua_State *L);
void lua_settop(lua_State *L, int idx);
void lua_pushvalue(lua_State *L, int idx);
void lua_remove(lua_State *L, int idx);
void lua_insert(lua_State *L, int idx);
void lua_replace(lua_State *L, int idx);
int lua_checkstack(lua_State *L, int extra);

// ===== Access functions (stack -> C) =====
int lua_type(lua_State *L, int idx);
const char *lua_typename(lua_State *L, int tp);
int lua_isnumber(lua_State *L, int idx);
int lua_isstring(lua_State *L, int idx);
int lua_iscfunction(lua_State *L, int idx);
int lua_isboolean(lua_State *L, int idx);
int lua_isnoneornil(lua_State *L, int idx);

lua_Number lua_tonumber(lua_State *L, int idx);
lua_Integer lua_tointeger(lua_State *L, int idx);
int lua_toboolean(lua_State *L, int idx);
const char *lua_tolstring(lua_State *L, int idx, size_t *len);
size_t lua_objlen(lua_State *L, int idx);
lua_CFunction lua_tocfunction(lua_State *L, int idx);

// ===== Push functions (C -> stack) =====
void lua_pushnil(lua_State *L);
void lua_pushnumber(lua_State *L, lua_Number n);
void lua_pushinteger(lua_State *L, lua_Integer n);
void lua_pushlstring(lua_State *L, const char *s, size_t len);
void lua_pushstring(lua_State *L, const char *s);
const char *lua_pushfstring(lua_State *L, const char *fmt, ...);
void lua_pushcclosure(lua_State *L, lua_CFunction fn, int n);
void lua_pushboolean(lua_State *L, int b);

#define lua_pushcfunction(L,f) lua_pushcclosure(L, f, 0)
#define lua_pushliteral(L, s) lua_pushlstring(L, "" s, (sizeof(s)/sizeof(char))-1)

// ===== Get functions (Lua -> stack) =====
void lua_gettable(lua_State *L, int idx);
void lua_getfield(lua_State *L, int idx, const char *k);
void lua_rawget(lua_State *L, int idx);
void lua_rawgeti(lua_State *L, int idx, int n);
void lua_createtable(lua_State *L, int narr, int nrec);
int lua_getmetatable(lua_State *L, int idx);

#define lua_newtable(L) lua_createtable(L, 0, 0)

// ===== Set functions (stack -> Lua) =====
void lua_settable(lua_State *L, int idx);
void lua_setfield(lua_State *L, int idx, const char *k);
void lua_rawset(lua_State *L, int idx);
void lua_rawseti(lua_State *L, int idx, int n);
int lua_setmetatable(lua_State *L, int idx);

// ===== 'load' and 'call' functions =====
void lua_call(lua_State *L, int nargs, int nresults);
int lua_pcall(lua_State *L, int nargs, int nresults, int errfunc);

// ===== Miscellaneous functions =====
int lua_error(lua_State *L);
int lua_next(lua_State *L, int idx);
void lua_concat(lua_State *L, int n);

// ===== Some useful macros =====
#define lua_pop(L,n) lua_settop(L, -(n)-1)
#define lua_register(L,n,f) (lua_pushcfunction(L, f), lua_setglobal(L, n))
#define lua_isfunction(L,n) (lua_type(L, (n)) == LUA_TFUNCTION)
#define lua_istable(L,n) (lua_type(L, (n)) == LUA_TTABLE)
#define lua_isnil(L,n) (lua_type(L, (n)) == LUA_TNIL)

// ===== Global get/set =====
void lua_getglobal(lua_State *L, const char *name);
void lua_setglobal(lua_State *L, const char *name);

// ===== Auxiliary library (lauxlib) =====
typedef struct luaL_Reg {
  const char *name;
  lua_CFunction func;
} luaL_Reg;

void luaL_register(lua_State *L, const char *libname, const luaL_Reg *l);
void luaL_openlib(lua_State *L, const char *libname, const luaL_Reg *l, int nup);
int luaL_error(lua_State *L, const char *fmt, ...);
void luaL_checktype(lua_State *L, int narg, int t);
void luaL_checkany(lua_State *L, int narg);
lua_Integer luaL_checkinteger(lua_State *L, int narg);
lua_Number luaL_checknumber(lua_State *L, int narg);
const char *luaL_checklstring(lua_State *L, int narg, size_t *len);
const char *luaL_checkstring(lua_State *L, int narg);
lua_Integer luaL_optinteger(lua_State *L, int narg, lua_Integer def);
lua_Number luaL_optnumber(lua_State *L, int narg, lua_Number def);
int luaL_checkoption(lua_State *L, int narg, const char *def, const char *const lst[]);
void luaL_setmetatable(lua_State *L, const char *tname);
int luaL_newmetatable(lua_State *L, const char *tname);

#define luaL_argcheck(L, cond, numarg, extramsg) \
  ((void)((cond) || luaL_argerror(L, (numarg), (extramsg))))

int luaL_argerror(lua_State *L, int narg, const char *extramsg);

// ===== Buffer management =====
typedef struct luaL_Buffer {
  char *b;
  size_t size;
  size_t n;
  lua_State *L;
} luaL_Buffer;

void luaL_buffinit(lua_State *L, luaL_Buffer *B);
char *luaL_prepbuffer(luaL_Buffer *B);
void luaL_addlstring(luaL_Buffer *B, const char *s, size_t l);
void luaL_addstring(luaL_Buffer *B, const char *s);
void luaL_addvalue(luaL_Buffer *B);
void luaL_pushresult(luaL_Buffer *B);

// ===== Registry and references =====
void lua_getregistry(lua_State *L);
int luaL_ref(lua_State *L, int t);
void luaL_unref(lua_State *L, int t, int ref);

#define LUA_NOREF  (-2)
#define LUA_REFNIL (-1)

// ===== Version check (for module compatibility) =====
#define LUA_VERSION     "Lua 5.1"
#define LUA_RELEASE     "Lua 5.1.5"
#define LUA_VERSION_NUM 501

// ===== Helper to convert standard C module init to your format =====
// Use this in package.c when loading C modules
Value compat_call_cmodule_init(VM *vm, int (*lua_init)(lua_State*), const char *modname);

#endif // LUA_COMPAT_H
