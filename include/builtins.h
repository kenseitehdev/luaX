
#ifndef BUILTINS_H
#define BUILTINS_H
#include <string.h>
#include "interpreter.h"

// in builtins.h (or a common header)
#define MT_STORE   "_mt"
#define PROT_KEY   "__metatable"
Value builtin_select(struct VM *vm, int argc, Value *argv);
static Value builtin_require(struct VM *vm, int argc, Value *argv);
Value builtin_getmetatable(struct VM *vm, int argc, Value *argv);
Value builtin_setmetatable(struct VM *vm, int argc, Value *argv);
Value builtin_assert(struct VM *vm, int argc, Value *argv);
Value builtin_collectgarbage(struct VM *vm, int argc, Value *argv);
Value builtin_error(struct VM *vm, int argc, Value *argv);
Value builtin__G(struct VM *vm, int argc, Value *argv);
Value builtin_rawequal(struct VM *vm, int argc, Value *argv);
Value builtin_rawget(struct VM *vm, int argc, Value *argv);
Value builtin_rawset(struct VM *vm, int argc, Value *argv);
Value builtin_next(struct VM *vm, int argc, Value *argv);
Value builtin_pairs(struct VM *vm, int argc, Value *argv);
Value ipairs_iter(struct VM *vm, int argc, Value *argv); /* iterator used by ipairs */
Value builtin_ipairs(struct VM *vm, int argc, Value *argv);
Value builtin_tonumber(struct VM *vm, int argc, Value *argv);
Value builtin_package(struct VM *vm, int argc, Value *argv);
Value builtin_package(struct VM *vm, int argc, Value *argv); 
extern Value vm_load_and_run_file(VM *vm, const char *path, const char *modname);
#endif
