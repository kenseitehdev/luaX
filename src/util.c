#include "../include/util.h"

void *xmalloc(size_t n){ void *p=malloc(n); if(!p){fprintf(stderr,"OOM\n"); exit(1);} return p; }
char *xstrdup(const char*s){ if(!s) s=""; size_t n=strlen(s)+1; char *p=xmalloc(n); memcpy(p,s,n); return p; }
Value op_len(Value v){
  if (v.tag == VAL_STR)  return V_int(v.as.s->len);
  if (v.tag == VAL_TABLE){
    long long n = 0, i = 1; Value out;
    while (tbl_get(v.as.t, V_int(i), &out)) { n++; i++; }
    return V_int(n);
  }
  return V_int(0);
}
FILE* open_string_as_FILE(const char *code) {
    if (!code) code = "";
#if defined(_GNU_SOURCE) || defined(__GLIBC__)
    FILE *f = fmemopen((void*)code, strlen(code), "r");
    if (f) return f;
#endif
    FILE *f = tmpfile();
    if (!f) return NULL;
    size_t len = strlen(code);
    if (len && fwrite(code, 1, len, f) != len) { fclose(f); return NULL; }
    rewind(f);
    return f;
}
void print_value(Value v){
  switch(v.tag){
    case VAL_NIL:   printf("nil"); break;
    case VAL_BOOL:  printf(v.as.b?"true":"false"); break;
    case VAL_INT:   printf("%lld", v.as.i); break;
    case VAL_NUM:   printf("%g", v.as.n); break;
    case VAL_STR:   fwrite(v.as.s->data,1,v.as.s->len,stdout); break;
    case VAL_TABLE: printf("table:%p", (void*)v.as.t); break;
    case VAL_CFUNC: printf("function:%p", (void*)v.as.cfunc); break;
    case VAL_FUNC:  printf("function:%p",  (void*)v.as.fn); break;
  }
}
Str *to_string_buf(Value v){
  char tmp[64];
  switch(v.tag){
    case VAL_NIL:   return Str_new_len("nil", 3);
    case VAL_BOOL:  return v.as.b? Str_new_len("true",4):Str_new_len("false",5);
    case VAL_INT:   snprintf(tmp,sizeof(tmp),"%lld", v.as.i); return Str_new_len(tmp,(int)strlen(tmp));
    case VAL_NUM:   snprintf(tmp,sizeof(tmp),"%.17g", v.as.n); return Str_new_len(tmp,(int)strlen(tmp));
    case VAL_STR:   return Str_new_len(v.as.s->data, v.as.s->len);
    case VAL_TABLE: snprintf(tmp,sizeof(tmp),"table:%p",(void*)v.as.t); return Str_new_len(tmp,(int)strlen(tmp));
    case VAL_CFUNC: snprintf(tmp,sizeof(tmp),"function:%p",(void*)v.as.cfunc); return Str_new_len(tmp,(int)strlen(tmp));
    case VAL_FUNC:  snprintf(tmp,sizeof(tmp),"function:%p",(void*)v.as.fn); return Str_new_len(tmp,(int)strlen(tmp));
  }
  return Str_new_len("<unknown>",9);
}

Value V_nil(void){ Value v={.tag=VAL_NIL}; return v; }
Value V_bool(bool b){ Value v={.tag=VAL_BOOL}; v.as.b=b?1:0; return v; }
Value V_int(long long x){ Value v={.tag=VAL_INT}; v.as.i=x; return v; }
Value V_num(double x){ Value v={.tag=VAL_NUM}; v.as.n=x; return v; }
Str *Str_new_len(const char *s,int len){ Str *st=xmalloc(sizeof(*st)); st->len=len; st->data=xmalloc(len+1); if(s&&len) memcpy(st->data,s,len); st->data[len]='\0'; return st; }
Value V_str_from_c(const char *s){ if(!s) s=""; return (Value){.tag=VAL_STR,.as.s=Str_new_len(s,(int)strlen(s))}; }
unsigned long long hash_mix(unsigned long long x){ x ^= x>>33; x*=0xff51afd7ed558ccdULL; x ^= x>>33; x*=0xc4ceb9fe1a85ec53ULL; x ^= x>>33; return x; }
