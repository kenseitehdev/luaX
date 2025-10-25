// test_cmodule.c
// Example C module that uses standard Lua C API
// This tests that the compatibility layer works correctly
// Compile with: make test-module-mac

#include "include/lua_compat.h"
#include <string.h>

// Simple function: add two numbers
static int test_add(lua_State *L) {
    lua_Number a = luaL_checknumber(L, 1);
    lua_Number b = luaL_checknumber(L, 2);
    lua_pushnumber(L, a + b);
    return 1;  // one return value
}

// Function that returns multiple values
static int test_multi(lua_State *L) {
    lua_pushstring(L, "hello");
    lua_pushnumber(L, 42);
    lua_pushboolean(L, 1);
    return 3;  // three return values
}

// Function that works with tables
static int test_tablesum(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    
    lua_Number sum = 0;
    int i = 1;
    while (1) {
        lua_rawgeti(L, 1, i);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        sum += lua_tonumber(L, -1);
        lua_pop(L, 1);
        i++;
    }
    
    lua_pushnumber(L, sum);
    return 1;
}

// Function that creates and returns a table
static int test_makeperson(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    lua_Integer age = luaL_checkinteger(L, 2);
    
    lua_newtable(L);
    
    lua_pushstring(L, name);
    lua_setfield(L, -2, "name");
    
    lua_pushinteger(L, age);
    lua_setfield(L, -2, "age");
    
    return 1;
}

// Test string operations
static int test_concat(lua_State *L) {
    const char *a = luaL_checkstring(L, 1);
    const char *b = luaL_checkstring(L, 2);
    
    lua_pushstring(L, a);
    lua_pushstring(L, b);
    lua_concat(L, 2);
    
    return 1;
}

// Test error handling
static int test_error(lua_State *L) {
    const char *msg = luaL_optstring(L, 1, "test error");
    return luaL_error(L, "Intentional error: %s", msg);
}

// Module registration table
static const luaL_Reg testlib[] = {
    {"add", test_add},
    {"multi", test_multi},
    {"tablesum", test_tablesum},
    {"makeperson", test_makeperson},
    {"concat", test_concat},
    {"error", test_error},
    {NULL, NULL}
};

// Module initialization function
// This is what package.loadlib will call
int luaopen_test(lua_State *L) {
    // Create the module table
    luaL_register(L, "test", testlib);
    
    // Add a version field
    lua_pushstring(L, "1.0.0");
    lua_setfield(L, -2, "_VERSION");
    
    // Add a description
    lua_pushstring(L, "Test module for Lua C API compatibility");
    lua_setfield(L, -2, "_DESCRIPTION");
    
    return 1;  // return the module table
}
