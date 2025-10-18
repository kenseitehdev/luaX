#ifndef TABLE_H
#define TABLE_H
#include <string.h>
#include "interpreter.h"  /* for Env, VM, Value */


void  tbl_set_public(Table *t, Value key, Value val);
int   tbl_get_public(Table *t, Value key, Value *out);
Table *tbl_new(void);
void tbl_set(Table *t, Value key, Value val);
int tbl_get(Table *t, Value key, Value *out);
extern int value_equal(Value a, Value b);
extern unsigned long long hash_value(Value v);
#endif
