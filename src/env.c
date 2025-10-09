#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h> 
#include <ctype.h>
#include <setjmp.h> 
#include "../include/env.h"

void  env_add_public(Env *e, const char *name, Value v, bool is_local) { env_add(e, name, v, is_local); }
Value call_any_public(VM *vm, Value cal, int argc, Value *argv) { return call_any(vm, cal, argc, argv); }
void env_register_close(Env *e, int slot) {
  if (!e) return;
  if (e->ccount == e->ccap) {
    e->ccap = e->ccap ? e->ccap * 2 : 4;
    e->closers = (CloseReg*)realloc(e->closers, sizeof(CloseReg) * (size_t)e->ccap);
  }
  e->closers[e->ccount++] = (CloseReg){ .slot = slot, .open = true };
}
void env_close_all(VM *vm, Env *e, Value err_obj) {
  if (!e) return;
  for (int i = e->ccount - 1; i >= 0; --i) {
    CloseReg *cr = &e->closers[i];
    if (!cr->open) continue;
    cr->open = false;
    int slot = cr->slot;
    if (slot < 0 || slot >= e->count) continue;
    Value v = e->vals[slot];
    Value mm = mm_of(v, "__close");
    if (mm.tag != VAL_NIL) {
      Value args[2] = { v, err_obj };
      (void)call_any(vm, mm, 2, args);
    }
  }
}
