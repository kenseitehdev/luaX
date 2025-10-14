#include "interpreter.h"

void shim_collect(struct VM *vm);
void shim_stop(struct VM *vm);
void shim_restart(struct VM *vm);
int  shim_isrunning(struct VM *vm);
int  shim_step(struct VM *vm, int kb);
int  shim_setpause(struct VM *vm, int pause);
int  shim_setstepmul(struct VM *vm, int mul);
void shim_set_incremental(struct VM *vm, int pause, int stepmul, int stepsize_kb);
void shim_set_generational(struct VM *vm, int minormul, int majormul);

