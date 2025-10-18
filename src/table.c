#include "../include/table.h"
#include "../include/interpreter.h"

void  tbl_set_public(Table *t, Value key, Value val) { tbl_set(t, key, val); }
int   tbl_get_public(Table *t, Value key, Value *out) { return tbl_get(t, key, out); }
void tbl_foreach_public(struct Table *t, TableIterCallback callback, void *userdata) {
    if (!t || !callback) return;
    for (int i = 0; i < t->cap; i++) {
        for (TableEntry *e = t->buckets[i]; e; e = e->next) {
            callback(e->key, e->val, userdata);
        }
    }
}

Value V_table(void){ return (Value){.tag=VAL_TABLE,.as.t=tbl_new()}; }
int value_equal(Value a, Value b){
  if(a.tag!=b.tag){
    if((a.tag==VAL_INT&&b.tag==VAL_NUM)) return (double)a.as.i==b.as.n;
    if((a.tag==VAL_NUM&&b.tag==VAL_INT)) return a.as.n==(double)b.as.i;
    return 0;
  }
  switch(a.tag){
    case VAL_NIL: return 1;
    case VAL_BOOL: return a.as.b==b.as.b;
    case VAL_INT: return a.as.i==b.as.i;
    case VAL_NUM: return a.as.n==b.as.n;
    case VAL_STR: return a.as.s->len==b.as.s->len && memcmp(a.as.s->data,b.as.s->data,a.as.s->len)==0;
    default: return a.as.t==b.as.t;
  }
}
void tbl_set(Table *t, Value key, Value val){
  unsigned long long h=hash_value(key);
  int idx = (int)(h % t->cap);
  for(TableEntry *e=t->buckets[idx]; e; e=e->next){
    if(value_equal(e->key,key)){ e->val=val; return; }
  }
  TableEntry *ne=xmalloc(sizeof(*ne));
  ne->key=key; ne->val=val; ne->next=t->buckets[idx];
  t->buckets[idx]=ne;
}
int tbl_get(Table *t, Value key, Value *out){
  unsigned long long h=hash_value(key);
  int idx = (int)(h % t->cap);
  for(TableEntry *e=t->buckets[idx]; e; e=e->next){
    if(value_equal(e->key,key)){ *out=e->val; return 1; }
  }
  return 0;
}
Table *tbl_new(void){
  Table *t=xmalloc(sizeof(*t));
  t->cap=32; t->buckets=xmalloc(sizeof(TableEntry*)*t->cap);
  for(int i=0;i<t->cap;i++) t->buckets[i]=NULL;
  return t;
}
