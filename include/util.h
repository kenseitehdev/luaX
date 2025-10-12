#include <string.h>
#include <stdlib.h>
#include "interpreter.h"

void *xmalloc(size_t n);
char *xstrdup(const char *s);
Value op_len(Value v);
FILE *open_string_as_FILE(const char *code);
Value V_nil(void);
Value V_bool(bool b);
Value V_int(long long x);
Value V_num(double x);
Str *Str_new_len(const char *s, int len);
Value V_str_from_c(const char *s);
unsigned long long hash_mix(unsigned long long x);
