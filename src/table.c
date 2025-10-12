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
