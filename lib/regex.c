// lib/regex.c - POSIX Regular Expression Library
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../include/interpreter.h"

/* Maximum number of capture groups */
#define MAX_MATCHES 32

/* Helper to create string from buffer */
static Value V_str_copy_n(const char *src, size_t n) {
  Str *s = (Str*)malloc(sizeof(Str));
  if (!s) { fprintf(stderr,"OOM\n"); exit(1); }
  s->len = (int)n;
  s->data = (char*)malloc(n + 1);
  if (!s->data) { fprintf(stderr,"OOM\n"); exit(1); }
  if (n) memcpy(s->data, src, n);
  s->data[n] = '\0';
  Value v; v.tag = VAL_STR; v.as.s = s; return v;
}

/* regex.compile(pattern [, flags]) -> regex object or nil */
static Value regex_compile(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();
  
  const char *pattern = argv[0].as.s->data;
  int cflags = REG_EXTENDED;  /* Use extended regex by default */
  
  /* Parse flags if provided */
  if (argc >= 2 && argv[1].tag == VAL_STR) {
    const char *flags = argv[1].as.s->data;
    for (int i = 0; flags[i]; i++) {
      switch (flags[i]) {
        case 'i': cflags |= REG_ICASE; break;     /* Case insensitive */
        case 'm': cflags |= REG_NEWLINE; break;   /* Multiline */
        case 'b': cflags &= ~REG_EXTENDED; break; /* Basic regex */
        default: break;
      }
    }
  }
  
  /* Allocate and compile regex */
  regex_t *preg = (regex_t*)malloc(sizeof(regex_t));
  if (!preg) { fprintf(stderr,"OOM\n"); exit(1); }
  
  int ret = regcomp(preg, pattern, cflags);
  if (ret != 0) {
    char errbuf[256];
    regerror(ret, preg, errbuf, sizeof(errbuf));
    fprintf(stderr, "Regex compilation error: %s\n", errbuf);
    free(preg);
    return V_nil();
  }
  
  /* Store regex in a table with a special marker */
  Value t = V_table();
  tbl_set_public(t.as.t, V_str_from_c("_regex_ptr"), V_int((long long)(intptr_t)preg));
  tbl_set_public(t.as.t, V_str_from_c("pattern"), argv[0]);
  
  return t;
}

/* Helper to extract regex_t from regex object */
static regex_t* get_regex(Value obj) {
  if (obj.tag != VAL_TABLE) return NULL;
  Value ptr_val;
  if (!tbl_get_public(obj.as.t, V_str_from_c("_regex_ptr"), &ptr_val)) return NULL;
  if (ptr_val.tag != VAL_INT) return NULL;
  return (regex_t*)(intptr_t)ptr_val.as.i;
}

/* regex.match(regex_obj, string [, offset]) -> table or nil */
static Value regex_match(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2 || argv[1].tag != VAL_STR) return V_nil();
  
  regex_t *preg = get_regex(argv[0]);
  if (!preg) return V_nil();
  
  const char *str = argv[1].as.s->data;
  size_t str_len = (size_t)argv[1].as.s->len;
  int offset = 0;
  
  if (argc >= 3) {
    if (argv[2].tag == VAL_INT) offset = (int)argv[2].as.i;
    else if (argv[2].tag == VAL_NUM) offset = (int)argv[2].as.n;
    if (offset < 0) offset = 0;
    if (offset >= (int)str_len) return V_nil();
  }
  
  regmatch_t matches[MAX_MATCHES];
  int ret = regexec(preg, str + offset, MAX_MATCHES, matches, 0);
  
  if (ret == REG_NOMATCH) return V_nil();
  if (ret != 0) return V_nil();
  
  /* Build result table */
  Value result = V_table();
  
  /* Full match at index 0 */
  if (matches[0].rm_so != -1) {
    int start = (int)matches[0].rm_so + offset + 1;  /* 1-indexed */
    int end = (int)matches[0].rm_eo + offset;
    tbl_set_public(result.as.t, V_str_from_c("start"), V_int(start));
    tbl_set_public(result.as.t, V_str_from_c("end"), V_int(end));
    
    Value full = V_str_copy_n(str + matches[0].rm_so + offset, 
                              (size_t)(matches[0].rm_eo - matches[0].rm_so));
    tbl_set_public(result.as.t, V_int(0), full);
  }
  
  /* Capture groups starting at index 1 */
  int capture_count = 0;
  for (int i = 1; i < MAX_MATCHES && matches[i].rm_so != -1; i++) {
    Value cap = V_str_copy_n(str + matches[i].rm_so + offset,
                            (size_t)(matches[i].rm_eo - matches[i].rm_so));
    tbl_set_public(result.as.t, V_int(i), cap);
    capture_count++;
  }
  
  tbl_set_public(result.as.t, V_str_from_c("captures"), V_int(capture_count));
  
  return result;
}

/* regex.find(regex_obj, string [, offset]) -> {start, end, captures...} or nil */
static Value regex_find(struct VM *vm, int argc, Value *argv) {
  Value match_result = regex_match(vm, argc, argv);
  if (match_result.tag == VAL_NIL) return V_nil();
  
  /* Convert to find-style result */
  Value result = V_table();
  Value start_val, end_val;
  
  if (tbl_get_public(match_result.as.t, V_str_from_c("start"), &start_val))
    tbl_set_public(result.as.t, V_int(1), start_val);
  if (tbl_get_public(match_result.as.t, V_str_from_c("end"), &end_val))
    tbl_set_public(result.as.t, V_int(2), end_val);
  
  /* Copy captures starting at index 3 */
  for (int i = 1; i < MAX_MATCHES; i++) {
    Value cap;
    if (tbl_get_public(match_result.as.t, V_int(i), &cap)) {
      tbl_set_public(result.as.t, V_int(i + 2), cap);
    } else {
      break;
    }
  }
  
  return result;
}

/* regex.test(regex_obj, string) -> boolean */
static Value regex_test(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2 || argv[1].tag != VAL_STR) return V_bool(0);
  
  regex_t *preg = get_regex(argv[0]);
  if (!preg) return V_bool(0);
  
  const char *str = argv[1].as.s->data;
  int ret = regexec(preg, str, 0, NULL, 0);
  
  return V_bool(ret == 0);
}

/* regex.gsub(regex_obj, string, replacement [, limit]) -> {string, count} */
static Value regex_gsub(struct VM *vm, int argc, Value *argv) {
  if (argc < 3 || argv[1].tag != VAL_STR) return V_nil();
  
  regex_t *preg = get_regex(argv[0]);
  if (!preg) return V_nil();
  
  const char *str = argv[1].as.s->data;
  size_t str_len = (size_t)argv[1].as.s->len;
  Value replacement = argv[2];
  
  int limit = -1;
  if (argc >= 4) {
    if (argv[3].tag == VAL_INT) limit = (int)argv[3].as.i;
    else if (argv[3].tag == VAL_NUM) limit = (int)argv[3].as.n;
  }
  
  /* Build result string */
  char *result = NULL;
  size_t result_len = 0;
  size_t result_cap = 0;
  
  const char *pos = str;
  int count = 0;
  
  while (pos < str + str_len && (limit < 0 || count < limit)) {
    regmatch_t matches[MAX_MATCHES];
    int ret = regexec(preg, pos, MAX_MATCHES, matches, 0);
    
    if (ret == REG_NOMATCH) break;
    if (ret != 0) break;
    
    /* Append text before match */
    size_t before_len = (size_t)matches[0].rm_so;
    if (before_len > 0) {
      size_t new_len = result_len + before_len;
      if (new_len + 1 > result_cap) {
        result_cap = (result_cap == 0) ? 256 : result_cap * 2;
        while (result_cap < new_len + 1) result_cap *= 2;
        result = (char*)realloc(result, result_cap);
        if (!result) { fprintf(stderr,"OOM\n"); exit(1); }
      }
      memcpy(result + result_len, pos, before_len);
      result_len = new_len;
    }
    
    /* Append replacement */
    if (replacement.tag == VAL_STR) {
      const char *repl = replacement.as.s->data;
      size_t repl_len = (size_t)replacement.as.s->len;
      
      /* Handle $1, $2, etc. in replacement */
      for (size_t i = 0; i < repl_len; i++) {
        if (repl[i] == '$' && i + 1 < repl_len && repl[i+1] >= '0' && repl[i+1] <= '9') {
          int cap_idx = repl[i+1] - '0';
          if (cap_idx < MAX_MATCHES && matches[cap_idx].rm_so != -1) {
            size_t cap_len = (size_t)(matches[cap_idx].rm_eo - matches[cap_idx].rm_so);
            size_t new_len = result_len + cap_len;
            if (new_len + 1 > result_cap) {
              result_cap = (result_cap == 0) ? 256 : result_cap * 2;
              while (result_cap < new_len + 1) result_cap *= 2;
              result = (char*)realloc(result, result_cap);
              if (!result) { fprintf(stderr,"OOM\n"); exit(1); }
            }
            memcpy(result + result_len, pos + matches[cap_idx].rm_so, cap_len);
            result_len = new_len;
          }
          i++;  /* Skip digit */
        } else {
          size_t new_len = result_len + 1;
          if (new_len + 1 > result_cap) {
            result_cap = (result_cap == 0) ? 256 : result_cap * 2;
            result = (char*)realloc(result, result_cap);
            if (!result) { fprintf(stderr,"OOM\n"); exit(1); }
          }
          result[result_len++] = repl[i];
        }
      }
    } else if (replacement.tag == VAL_FUNC || replacement.tag == VAL_CFUNC) {
      /* Call function with match */
      Value match_str = V_str_copy_n(pos + matches[0].rm_so, 
                                     (size_t)(matches[0].rm_eo - matches[0].rm_so));
      Value args[1] = { match_str };
      Value result_val = call_any_public(vm, replacement, 1, args);
      
      if (result_val.tag == VAL_STR) {
        size_t repl_len = (size_t)result_val.as.s->len;
        size_t new_len = result_len + repl_len;
        if (new_len + 1 > result_cap) {
          result_cap = (result_cap == 0) ? 256 : result_cap * 2;
          while (result_cap < new_len + 1) result_cap *= 2;
          result = (char*)realloc(result, result_cap);
          if (!result) { fprintf(stderr,"OOM\n"); exit(1); }
        }
        memcpy(result + result_len, result_val.as.s->data, repl_len);
        result_len = new_len;
      }
    }
    
    count++;
    pos += matches[0].rm_eo;
    
    /* Handle empty matches */
    if (matches[0].rm_eo == 0) {
      if (pos < str + str_len) {
        size_t new_len = result_len + 1;
        if (new_len + 1 > result_cap) {
          result_cap = (result_cap == 0) ? 256 : result_cap * 2;
          result = (char*)realloc(result, result_cap);
          if (!result) { fprintf(stderr,"OOM\n"); exit(1); }
        }
        result[result_len++] = *pos++;
      } else {
        break;
      }
    }
  }
  
  /* Append remaining text */
  size_t remaining = str + str_len - pos;
  if (remaining > 0) {
    size_t new_len = result_len + remaining;
    if (new_len + 1 > result_cap) {
      result_cap = new_len + 1;
      result = (char*)realloc(result, result_cap);
      if (!result) { fprintf(stderr,"OOM\n"); exit(1); }
    }
    memcpy(result + result_len, pos, remaining);
    result_len = new_len;
  }
  
  /* Return {string, count} */
  Value ret = V_table();
  if (result) {
    result[result_len] = '\0';
    Value result_str = V_str_copy_n(result, result_len);
    free(result);
    tbl_set_public(ret.as.t, V_int(1), result_str);
  } else {
    tbl_set_public(ret.as.t, V_int(1), argv[1]);
  }
  tbl_set_public(ret.as.t, V_int(2), V_int(count));
  
  return ret;
}

/* regex.free(regex_obj) */
static Value regex_free(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1) return V_nil();
  
  regex_t *preg = get_regex(argv[0]);
  if (preg) {
    regfree(preg);
    free(preg);
    /* Clear the pointer in the table */
    tbl_set_public(argv[0].as.t, V_str_from_c("_regex_ptr"), V_int(0));
  }
  
  return V_nil();
}

/* Register regex library */
void register_regex_lib(struct VM *vm) {
  Value t = V_table();
  
  tbl_set_public(t.as.t, V_str_from_c("compile"), (Value){.tag=VAL_CFUNC,.as.cfunc=regex_compile});
  tbl_set_public(t.as.t, V_str_from_c("match"),   (Value){.tag=VAL_CFUNC,.as.cfunc=regex_match});
  tbl_set_public(t.as.t, V_str_from_c("find"),    (Value){.tag=VAL_CFUNC,.as.cfunc=regex_find});
  tbl_set_public(t.as.t, V_str_from_c("test"),    (Value){.tag=VAL_CFUNC,.as.cfunc=regex_test});
  tbl_set_public(t.as.t, V_str_from_c("gsub"),    (Value){.tag=VAL_CFUNC,.as.cfunc=regex_gsub});
  tbl_set_public(t.as.t, V_str_from_c("free"),    (Value){.tag=VAL_CFUNC,.as.cfunc=regex_free});
  
  env_add_public(vm->env, "regex", t, false);
}
