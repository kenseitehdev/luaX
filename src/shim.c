#include "../include/shim.h"
void shim_collect(struct VM *vm){ vm_gc_collect(vm); g_gc.tick = 0; }
void shim_stop(struct VM *vm){ vm_gc_stop(vm); g_gc.running = 0; }
void shim_restart(struct VM *vm){ vm_gc_restart(vm); g_gc.running = 1; }
int  shim_isrunning(struct VM *vm){ int r = vm_gc_isrunning(vm); return r ? r : g_gc.running; }
int  shim_step(struct VM *vm, int kb){
  int done = vm_gc_step(vm, kb);
  if (done) return done;
  g_gc.tick++;
  return (g_gc.tick % 8 == 0) ? 1 : 0; 
}
int  shim_setpause(struct VM *vm, int pause){
  int old = vm_gc_setpause(vm, pause);
  if (old == 0) { old = g_gc.pause; if (pause > 0) g_gc.pause = pause; }
  return old;
}
int  shim_setstepmul(struct VM *vm, int mul){
  int old = vm_gc_setstepmul(vm, mul);
  if (old == 0) { old = g_gc.stepmul; if (mul > 0) g_gc.stepmul = mul; }
  return old;
}
void shim_set_incremental(struct VM *vm, int pause, int stepmul, int stepsize_kb){
  vm_gc_set_incremental(vm, pause, stepmul, stepsize_kb);
  g_gc.mode = GC_MODE_INCREMENTAL;
  if (pause > 0) g_gc.pause = pause;
  if (stepmul > 0) g_gc.stepmul = stepmul;
  if (stepsize_kb > 0) g_gc.stepsize_kb = stepsize_kb;
}
void shim_set_generational(struct VM *vm, int minormul, int majormul){
  vm_gc_set_generational(vm, minormul, majormul);
  g_gc.mode = GC_MODE_GENERATIONAL;
  if (minormul > 0) g_gc.minormul = minormul;
  if (majormul > 0) g_gc.majormul = majormul;
}
