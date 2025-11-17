#include "../include/vm.h"
char path_buf[2048];

Value vm_pop(VM *vm) {
    if (vm->top < 0) {
        vm_raise(vm, V_str_from_c("stack underflow"));
    }
    return vm->stack[vm->top--];
}

void vm_push(VM *vm, Value v) {
    if (vm->top >= STACK_MAX - 1) {
        vm_raise(vm, V_str_from_c("stack overflow"));
    }
    vm->stack[++vm->top] = v;
}
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
    env_add_builtins(vm);  // <- ADD THIS LINE
    register_libs(vm); 
    Value package = V_table();
    Value loaded = V_table();
    Value preload = V_table();
    Value searchers = V_table();

    const char *lua_path_env = getenv("LUA_PATH");
    const char *rocks_tree1 = "/usr/local/share/lua/5.4/?.lua;/usr/local/share/lua/5.4/?/init.lua";
    const char *rocks_tree2 = "/usr/share/lua/5.4/?.lua;/usr/share/lua/5.4/?/init.lua";
    const char *local_tree = "?.lua;?/init.lua;./?.lua;./?/init.lua";

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
    void register_libs(struct VM *vm);  
    return vm;
}

