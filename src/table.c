#include "../include/table.h"
void  tbl_set_public(Table *t, Value key, Value val) { tbl_set(t, key, val); }
int   tbl_get_public(Table *t, Value key, Value *out) { return tbl_get(t, key, out); }
