#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h> 
#include <ctype.h>
#include <setjmp.h> 
#include "../include/env.h"

Env *env_push(Env *parent){
  Env *e=xmalloc(sizeof(*e));
  e->parent=parent; e->count=0; e->cap=8;
  e->names=xmalloc(sizeof(char*)*e->cap);
  e->vals =xmalloc(sizeof(Value)*e->cap);
  e->is_local=xmalloc(sizeof(bool)*e->cap);
  e->closers = NULL;
  e->ccount  = 0;
  e->ccap    = 0;
  return e;
}
void env_add(Env *e, const char *name, Value v, bool is_local){
  if(e->count==e->cap){
    e->cap*=2;
    e->names=realloc(e->names,sizeof(char*)*e->cap);
    e->vals =realloc(e->vals ,sizeof(Value)*e->cap);
    e->is_local=realloc(e->is_local,sizeof(bool)*e->cap);
  }
  e->names[e->count]=xstrdup(name);
  e->vals [e->count]=v;
  e->is_local[e->count]=is_local;
  e->count++;
}
int env_set(Env *e, const char *name, Value v){
  for(Env *cur=e; cur; cur=cur->parent){
    for(int i=0;i<cur->count;i++){
      if(strcmp(cur->names[i],name)==0){ cur->vals[i]=v; return 1; }
    }
  }
  return 0;
}
int env_get(Env *e, const char *name, Value *out){
  for(Env *cur=e; cur; cur=cur->parent){
    for(int i=0;i<cur->count;i++){
      if(strcmp(cur->names[i],name)==0){ *out=cur->vals[i]; return 1; }
    }
  }
  return 0;
}
int env_find(Env *e, const char *name, Env **owner, int *slot){
  for(Env *cur=e; cur; cur=cur->parent){
    for(int i=0;i<cur->count;i++){
      if(strcmp(cur->names[i],name)==0){ if(owner)*owner=cur; if(slot)*slot=i; return 1; }
    }
  }
  return 0;
}
Env* env_root(Env *e){
  if(!e) return NULL;
  while(e->parent) e = e->parent;
  return e;
}
static inline void add_global(VM *vm, Value G, const char *name, Value v) {
    tbl_set_public(G.as.t, V_str_from_c(name), v);
    env_add(vm->env, name, v, false);
}

void env_add_builtins(VM *vm) {
    /* create the true global table _G */
    Value G = V_table();
    env_add(vm->env, "_G", G, false);
    tbl_set_public(G.as.t, V_str_from_c("_G"), G); // self-reference

    /* core builtins */
    add_global(vm, G, "print",   (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_print});
    add_global(vm, G, "select",  (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_select});
    add_global(vm, G, "pairs",   (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_pairs});
    add_global(vm, G, "ipairs",  (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_ipairs});
    add_global(vm, G, "assert",  (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_assert});
    add_global(vm, G, "collectgarbage", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_collectgarbage});
    add_global(vm, G, "error",   (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_error});
    add_global(vm, G, "getmetatable", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_getmetatable});
    add_global(vm, G, "setmetatable", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_setmetatable});
    add_global(vm, G, "rawequal", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_rawequal});
    add_global(vm, G, "rawget",  (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_rawget});
    add_global(vm, G, "rawset",  (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_rawset});
    add_global(vm, G, "load",    (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_load});
    add_global(vm, G, "loadfile",(Value){.tag=VAL_CFUNC,.as.cfunc=builtin_loadfile});
    add_global(vm, G, "require", (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_require});
    add_global(vm, G, "next",    (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_next});
    add_global(vm, G, "tonumber",(Value){.tag=VAL_CFUNC,.as.cfunc=builtin_tonumber});
    add_global(vm, G, "tostring",(Value){.tag=VAL_CFUNC,.as.cfunc=builtin_tostring});
    add_global(vm, G, "type",    (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_type});
    add_global(vm, G, "_VERSION",V_str_from_c("LuaX 1.0.4"));
    add_global(vm, G, "xpcall",  (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_xpcall});
    add_global(vm, G, "pcall",   (Value){.tag=VAL_CFUNC,.as.cfunc=builtin_pcall});
}
void env_add_public(Env *e, const char *name, Value v, bool is_local) {
    /* add to current environment */
    env_add(e, name, v, is_local);

    /* also add into _G if it exists */
    Value G;
    if (env_get(e, "_G", &G) && G.tag == VAL_TABLE) {
        tbl_set_public(G.as.t, V_str_from_c(name), v);
    }
}
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
