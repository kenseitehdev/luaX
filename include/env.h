#ifndef ENV_H
#define ENV_H

#include <stdbool.h>
#include "interpreter.h" 
extern Env *VM_env;  /* pointer to current lexical environment */

extern Value builtin_select(struct VM *vm, int argc, Value *argv);
extern Value builtin_require(struct VM *vm, int argc, Value *argv);
extern Value builtin_getmetatable(struct VM *vm, int argc, Value *argv);
extern Value builtin_setmetatable(struct VM *vm, int argc, Value *argv);
extern Value builtin_assert(struct VM *vm, int argc, Value *argv);
extern Value builtin_collectgarbage(struct VM *vm, int argc, Value *argv);
extern Value builtin_error(struct VM *vm, int argc, Value *argv);
extern Value builtin__G(struct VM *vm, int argc, Value *argv);
extern Value builtin_rawequal(struct VM *vm, int argc, Value *argv);
extern Value builtin_rawget(struct VM *vm, int argc, Value *argv);
extern Value builtin_rawset(struct VM *vm, int argc, Value *argv);
extern Value builtin_load(struct VM *vm, int argc, Value *argv);
extern Value builtin_loadfile(struct VM *vm, int argc, Value *argv);
extern Value builtin_next(struct VM *vm, int argc, Value *argv);
extern Value builtin_tonumber(struct VM *vm, int argc, Value *argv);
extern Value builtin_tostring(struct VM *vm, int argc, Value *argv);
extern Value builtin_type(struct VM *vm, int argc, Value *argv);
extern Value builtin_xpcall(struct VM *vm, int argc, Value *argv);
extern Value builtin_pcall(struct VM *vm, int argc, Value *argv);
extern Value builtin_pairs(struct VM *vm, int argc, Value *argv);
extern Value builtin_print(struct VM *vm, int argc, Value *argv);
extern Value ipairs_iter(struct VM *vm, int argc, Value *argv);
extern Value builtin_ipairs(struct VM *vm, int argc, Value *argv);
extern Value builtin_package(struct VM *vm, int argc, Value *argv);
/* Environment management functions */
extern void env_add(Env *e, const char *name, Value v, bool is_local);
extern int env_get(Env *e, const char *name, Value *out);
extern Env* env_root(Env *e);

/* Close variable support */
extern void env_register_close(Env *e, int slot);
extern void env_close_all(VM *vm, Env *e, Value err_obj);

/* Public wrappers */
extern void env_add_public(Env *e, const char *name, Value v, bool is_local); 
extern Value call_any_public(VM *vm, Value cal, int argc, Value *argv);

#endif /* ENV_H */
