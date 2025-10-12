#ifndef ENV_H
#define ENV_H

#include <stdbool.h>
#include "interpreter.h"  /* for Env, VM, Value */

extern Env *VM_env;  /* pointer to current lexical environment */

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
