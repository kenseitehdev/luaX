#ifndef TABLE_H
#define TABLE_H

#include "interpreter.h"  /* for Env, VM, Value */


void  tbl_set_public(Table *t, Value key, Value val);
int   tbl_get_public(Table *t, Value key, Value *out); 
#endif
