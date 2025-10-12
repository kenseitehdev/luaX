#include <stdlib.h>
#include "env.h"
#include "builtins.h"
#include "interpreter.h"
#define STACK_MAX 256
extern Value builtin__G(struct VM *vm, int argc, Value *argv);
extern Value builtin_error(struct VM *vm, int argc, Value *argv);
extern Value builtin_rawequal(struct VM *vm, int argc, Value *argv);
extern Value builtin_rawget(struct VM *vm, int argc, Value *argv);
extern Value builtin_rawset(struct VM *vm, int argc, Value *argv);
extern Value builtin_next(struct VM *vm, int argc, Value *argv);
extern Value builtin_pairs(struct VM *vm, int argc, Value *argv);
extern Value builtin_load(struct VM *vm, int argc, Value *argv);
extern Value builtin_loadfile(struct VM *vm, int argc, Value *argv);
extern Value builtin_tostring(struct VM *vm, int argc, Value *argv);
extern Value builtin_type(struct VM *vm, int argc, Value *argv);
extern Value builtin_xpcall(struct VM *vm, int argc, Value *argv);
extern Value builtin_pcall(struct VM *vm, int argc, Value *argv);
extern Value builtin_print(struct VM *vm, int argc, Value *argv);
extern Value builtin_require(struct VM *vm, int argc, Value *argv);
Value vm_pop(VM *vm); 

void vm_push(VM *vm, Value v);
