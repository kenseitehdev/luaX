#ifndef ENV_H
#define ENV_H

#include "interpreter.h"  /* for Env, VM, Value */

extern Env *VM_env;  /* pointer to current lexical environment */

void env_register_close(Env *e, int slot);
void env_close_all(VM *vm, Env *e, Value err_obj);

void  env_add_public(Env *e, const char *name, Value v, bool is_local); 
Value call_any_public(VM *vm, Value cal, int argc, Value *argv); 
#endif /* ENV_H */
