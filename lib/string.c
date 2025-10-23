// lib/string.c - Production-level Lua-compatible string library
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include "../include/interpreter.h"
#include "regex.h"

/* Maximum number of captures */
#define LUA_MAXCAPTURES 32

/* ---- safe string builder into VM Str ---- */
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

/* helpers */
static int clamp(int x, int lo, int hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

static int lua_index_adjust(int idx, int len) {
  if (idx >= 0) return idx;
  return len + idx + 1;
}

/* ---------------------------
 * Production Pattern Matching Engine
 * --------------------------- */

typedef struct {
    const char *src_init;    /* Start of source string */
    const char *src_end;     /* End of source string */
    const char *p_end;       /* End of pattern */
    int level;               /* Current capture level */
    struct {
        const char *init;
        ptrdiff_t len;
    } capture[LUA_MAXCAPTURES];
} MatchState;

/* Character class definitions - complete Lua compatibility */
static int match_class(int c, int p) {
    int res;
    switch (tolower(p)) {
        case 'a': res = isalpha(c); break;           /* Letters */
        case 'c': res = iscntrl(c); break;           /* Control chars */
        case 'd': res = isdigit(c); break;           /* Digits */
        case 'g': res = isgraph(c); break;           /* Printable except space */
        case 'l': res = islower(c); break;           /* Lowercase letters */
        case 'p': res = ispunct(c); break;           /* Punctuation */
        case 's': res = isspace(c); break;           /* Space characters */
        case 'u': res = isupper(c); break;           /* Uppercase letters */
        case 'w': res = isalnum(c); break;           /* Alphanumeric */
        case 'x': res = isxdigit(c); break;          /* Hexadecimal digits */
        case 'z': res = (c == 0); break;             /* Zero/null byte */
        default: return (c == p);                    /* Literal character */
    }
    return (islower(p) ? res : !res);  /* Uppercase = negation */
}

/* Match character class inside brackets [abc], [^abc], [a-z] */
static int matchbracketclass(int c, const char *p, const char *ec) {
    int sig = 1;
    if (*(p+1) == '^') {
        sig = 0;
        p++;  /* skip the '^' */
    }
    while (++p < ec) {
        if (*p == '%') {
            p++;
            if (p < ec && match_class(c, (unsigned char)*p))
                return sig;
        }
        else if ((*(p+1) == '-') && (p+2 < ec)) {
            p += 2;
            if ((unsigned char)*(p-2) <= c && c <= (unsigned char)*p)
                return sig;
        }
        else if ((unsigned char)*p == c) return sig;
    }
    return !sig;
}

/* Single character match with pattern element */
static int singlematch(MatchState *ms, const char *s, const char *p, const char *ep) {
    if (s >= ms->src_end)
        return 0;
    else {
        int c = (unsigned char)*s;
        switch (*p) {
            case '.': return 1;  /* matches any char except newline */
            case '%': return match_class(c, (unsigned char)*(p+1));
            case '[': return matchbracketclass(c, p, ep-1);
            default:  return ((unsigned char)*p == c);
        }
    }
}

/* Forward declarations */
static const char *match(MatchState *ms, const char *s, const char *p);
static const char *matchbalance(MatchState *ms, const char *s, const char *p);

/* Match balanced strings like %b() */
static const char *matchbalance(MatchState *ms, const char *s, const char *p) {
    if (p >= ms->p_end - 1)
        return NULL;  /* not enough chars in pattern */
    if (*s != *p) 
        return NULL;
    else {
        int b = *p;
        int e = *(p+1);
        int cont = 1;
        while (++s < ms->src_end) {
            if (*s == e) {
                if (--cont == 0) return s+1;
            }
            else if (*s == b) cont++;
        }
    }
    return NULL;  /* string ends out of balance */
}

/* Greedy quantifier matching (*, +) */
static const char *max_expand(MatchState *ms, const char *s, const char *p, const char *ep) {
    ptrdiff_t i = 0;  /* counts maximum expand for item */
    while (singlematch(ms, s + i, p, ep))
        i++;
    /* keeps trying to match with the maximum repetitions */
    while (i >= 0) {
        const char *res = match(ms, s + i, ep + 1);
        if (res) return res;
        i--;  /* else didn't match; reduce 1 repetition to try again */
    }
    return NULL;
}

/* Lazy quantifier matching (-) */
static const char *min_expand(MatchState *ms, const char *s, const char *p, const char *ep) {
    for (;;) {
        const char *res = match(ms, s, ep + 1);
        if (res != NULL)
            return res;
        else if (singlematch(ms, s, p, ep))
            s++;  /* try with one more repetition */
        else return NULL;
    }
}

/* Start a new capture */
static const char *start_capture(MatchState *ms, const char *s, const char *p, int what) {
    const char *res;
    int level = ms->level;
    if (level >= LUA_MAXCAPTURES) return NULL;  /* too many captures */
    ms->capture[level].init = s;
    ms->capture[level].len = what;
    ms->level = level + 1;
    if ((res = match(ms, s, p)) == NULL)  /* match failed? */
        ms->level--;  /* undo capture */
    return res;
}

/* End a capture */
static const char *end_capture(MatchState *ms, const char *s, const char *p) {
    int l = ms->level - 1;
    const char *res;
    ms->level = l;
    ms->capture[l].len = s - ms->capture[l].init;
    if ((res = match(ms, s, p)) == NULL)  /* match failed? */
        ms->level++;  /* undo capture */
    return res;
}

/* Match a previous capture (%1, %2, etc.) */
static const char *match_capture(MatchState *ms, const char *s, int l) {
    size_t len;
    l = l - '1';
    if (l < 0 || l >= ms->level || ms->capture[l].len == -1)
        return NULL;  /* invalid capture index */
    len = ms->capture[l].len;
    if ((size_t)(ms->src_end - s) >= len &&
        memcmp(ms->capture[l].init, s, len) == 0)
        return s + len;
    else return NULL;
}

/* Main pattern matching function - handles all Lua patterns */
static const char *match(MatchState *ms, const char *s, const char *p) {
    if (ms->level > 200) return NULL;  /* avoid stack overflow */
    
    init: /* tail call optimization using goto */
    if (p != ms->p_end) {  /* end of pattern? */
        switch (*p) {
            case '(': {  /* start capture */
                if (*(p + 1) == ')') /* position capture? */
                    return start_capture(ms, s, p + 2, (int)(s - ms->src_init));
                else
                    return start_capture(ms, s, p + 1, -1);
            }
            case ')': {  /* end capture */
                return end_capture(ms, s, p + 1);
            }
            case '$': {
                if ((p + 1) != ms->p_end)  /* is the '$' the last char in pattern? */
                    goto dflt;  /* no; go to default */
                return (s == ms->src_end) ? s : NULL;  /* check end of string */
            }
            case '%': {
                switch (*(p + 1)) {
                    case 'b': {  /* balanced string? */
                        s = matchbalance(ms, s, p + 2);
                        if (s != NULL) {
                            p += 4; goto init;  /* return match(ms, s, p + 4); */
                        }
                        return NULL;
                    }
                    case 'f': {  /* frontier? */
                        const char *ep; char previous;
                        p += 2;
                        if (*p != '[')
                            return NULL;  /* missing '[' after '%f' in pattern */
                        ep = p + 1;
                        while (*ep != ']') {
                            if (ep == ms->p_end)
                                return NULL;  /* missing ']' after '%f' in pattern */
                            ep++;
                        }
                        previous = (s == ms->src_init) ? '\0' : *(s - 1);
                        if (matchbracketclass((unsigned char)previous, p, ep - 1) ||
                           !matchbracketclass((unsigned char)*s, p, ep - 1))
                            return NULL;
                        else {
                            p = ep + 1; goto init;  /* return match(ms, s, ep + 1); */
                        }
                    }
                    case '0': case '1': case '2': case '3':
                    case '4': case '5': case '6': case '7':
                    case '8': case '9': {  /* capture results (%0-%9)? */
                        s = match_capture(ms, s, (unsigned char)*(p + 1));
                        if (s != NULL) {
                            p += 2; goto init;  /* return match(ms, s, p + 2); */
                        }
                        return NULL;
                    }
                    default: goto dflt;
                }
            }
            default: dflt: {  /* pattern class plus optional suffix */
                const char *ep = p + 1;  /* points to optional suffix */
                /* find end of pattern element (handle %x and [classes]) */
                if (*p == '%' && ep < ms->p_end)
                    ep++;
                else if (*p == '[') {
                    while (ep < ms->p_end && *ep != ']')
                        ep++;
                    if (ep < ms->p_end) ep++;  /* include the ']' */
                }
                
                /* get optional suffix */
                char suffix = (ep < ms->p_end) ? *ep : '\0';
                
                /* does not match at least once? */
                if (!singlematch(ms, s, p, ep)) {
                    if (suffix == '*' || suffix == '?' || suffix == '-') {  /* accept empty? */
                        p = (suffix != '\0') ? ep + 1 : ep; 
                        goto init;  /* return match(ms, s, ep + 1); */
                    }
                    else  /* '+' or no suffix */
                        return NULL;
                }
                else {  /* matched once */
                    switch (suffix) {
                        case '?': {  /* optional */
                            const char *res;
                            if ((res = match(ms, s + 1, ep + 1)) != NULL)
                                return res;
                            p = ep + 1; goto init;  /* else return match(ms, s, ep + 1); */
                        }
                        case '+':  /* 1 or more repetitions */
                            s++;  /* 1 match already done */
                            /* FALLTHROUGH */
                        case '*':  /* 0 or more repetitions */
                            return max_expand(ms, s, p, ep);
                        case '-':  /* 0 or more repetitions (minimum) */
                            return min_expand(ms, s, p, ep);
                        default:  /* no suffix */
                            s++; p = ep; goto init;  /* return match(ms, s + 1, ep); */
                    }
                }
            }
        }
    }
    else return s;  /* end of pattern */
}

/* String search utility */
static const char *lmemfind(const char *s1, size_t l1, const char *s2, size_t l2) {
    if (l2 == 0) return s1;  /* empty strings are everywhere */
    else if (l2 > l1) return NULL;  /* avoids a negative 'l1' */
    else {
        const char *init;  /* to search for a '*s2' inside 's1' */
        l2--;  /* 1st char will be checked by 'memchr' */
        l1 = l1 - l2;  /* 's2' cannot be found after that */
        while (l1 > 0 && (init = (const char *)memchr(s1, *s2, l1)) != NULL) {
            init++;   /* 1st char is already checked */
            if (memcmp(init, s2 + 1, l2) == 0)
                return init - 1;
            else {  /* correct 'l1' and 's1' to try again */
                l1 -= init - s1;
                s1 = init;
            }
        }
        return NULL;  /* not found */
    }
}

/* ---------------------------
 * String Library Functions  
 * --------------------------- */

/* string.len(s) */
static Value str_len(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();
  return V_int(argv[0].as.s->len);
}

/* string.lower(s) */
static Value str_lower(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();
  Str *in = argv[0].as.s;
  char *buf = (char*)malloc((size_t)in->len + 1);
  if (!buf) { fprintf(stderr,"OOM\n"); exit(1); }
  for (int i = 0; i < in->len; i++)
    buf[i] = (char)tolower((unsigned char)in->data[i]);
  buf[in->len] = '\0';
  Value v = V_str_copy_n(buf, (size_t)in->len);
  free(buf);
  return v;
}

/* string.upper(s) */
static Value str_upper(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();
  Str *in = argv[0].as.s;
  char *buf = (char*)malloc((size_t)in->len + 1);
  if (!buf) { fprintf(stderr,"OOM\n"); exit(1); }
  for (int i = 0; i < in->len; i++)
    buf[i] = (char)toupper((unsigned char)in->data[i]);
  buf[in->len] = '\0';
  Value v = V_str_copy_n(buf, (size_t)in->len);
  free(buf);
  return v;
}

/* string.reverse(s) */
static Value str_reverse(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();
  Str *in = argv[0].as.s;
  char *buf = (char*)malloc((size_t)in->len + 1);
  if (!buf) { fprintf(stderr,"OOM\n"); exit(1); }
  for (int i = 0; i < in->len; i++)
    buf[i] = in->data[in->len - 1 - i];
  buf[in->len] = '\0';
  Value v = V_str_copy_n(buf, (size_t)in->len);
  free(buf);
  return v;
}

/* string.sub(s, i [, j]) */
static Value str_sub(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2 || argv[0].tag != VAL_STR) return V_nil();
  Str *in = argv[0].as.s;
  int len = in->len;
  
  long long i = (argv[1].tag == VAL_INT) ? argv[1].as.i :
                (argv[1].tag == VAL_NUM) ? (long long)argv[1].as.n : 1;
  long long j;
  if (argc >= 3) {
    j = (argv[2].tag == VAL_INT) ? argv[2].as.i :
        (argv[2].tag == VAL_NUM) ? (long long)argv[2].as.n : len;
  } else {
    j = len;
  }
  
  int si = lua_index_adjust((int)i, len);
  int sj = lua_index_adjust((int)j, len);
  si = clamp(si, 1, len);
  sj = clamp(sj, 1, len);
  
  if (sj < si) return V_str_copy_n("", 0);
  
  int outlen = sj - si + 1;
  return V_str_copy_n(in->data + (si - 1), (size_t)outlen);
}

/* string.rep(s, n [, sep]) */
static Value str_rep(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2 || argv[0].tag != VAL_STR) return V_nil();
  Str *in = argv[0].as.s;
  long long n = (argv[1].tag == VAL_INT) ? argv[1].as.i :
                (argv[1].tag == VAL_NUM) ? (long long)argv[1].as.n : 0;
  
  if (n <= 0) return V_str_copy_n("", 0);
  
  const char *sep = "";
  size_t sep_len = 0;
  if (argc >= 3 && argv[2].tag == VAL_STR) {
    sep = argv[2].as.s->data;
    sep_len = (size_t)argv[2].as.s->len;
  }
  
  size_t total = (size_t)in->len * (size_t)n + sep_len * ((size_t)n - 1);
  char *buf = (char*)malloc(total + 1);
  if (!buf) { fprintf(stderr,"OOM\n"); exit(1); }
  
  char *p = buf;
  for (long long k = 0; k < n; k++) {
    if (k > 0 && sep_len > 0) {
      memcpy(p, sep, sep_len);
      p += sep_len;
    }
    memcpy(p, in->data, (size_t)in->len);
    p += in->len;
  }
  buf[total] = '\0';
  Value v = V_str_copy_n(buf, total);
  free(buf);
  return v;
}

/* string.byte(s [, i [, j]]) */
static Value str_byte(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();
  Str *in = argv[0].as.s;
  int len = in->len;
  
  int i = 1, j = 1;
  if (argc >= 2) {
    i = (argv[1].tag == VAL_INT) ? (int)argv[1].as.i :
        (argv[1].tag == VAL_NUM) ? (int)argv[1].as.n : 1;
  }
  if (argc >= 3) {
    j = (argv[2].tag == VAL_INT) ? (int)argv[2].as.i :
        (argv[2].tag == VAL_NUM) ? (int)argv[2].as.n : i;
  } else {
    j = i;
  }
  
  i = lua_index_adjust(i, len);
  j = lua_index_adjust(j, len);
  i = clamp(i, 1, len);
  j = clamp(j, 1, len);
  
  if (j < i) return V_table();
  
  Value t = V_table();
  int k = 1;
  for (int pos = i; pos <= j; ++pos) {
    unsigned char b = (unsigned char)in->data[pos - 1];
    tbl_set_public(t.as.t, V_int(k++), V_int((long long)b));
  }
  return t;
}

/* string.char(...) */
static Value str_char(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc <= 0) return V_str_copy_n("", 0);
  
  char *buf = (char*)malloc((size_t)argc + 1);
  if (!buf) { fprintf(stderr,"OOM\n"); exit(1); }
  
  for (int i = 0; i < argc; i++) {
    int v = 0;
    if (argv[i].tag == VAL_INT) v = (int)argv[i].as.i;
    else if (argv[i].tag == VAL_NUM) v = (int)argv[i].as.n;
    if (v < 0) v = 0; 
    if (v > 255) v = 255;
    buf[i] = (char)(unsigned char)v;
  }
  buf[argc] = '\0';
  Value out = V_str_copy_n(buf, (size_t)argc);
  free(buf);
  return out;
}

/* string.find(s, pattern [, init [, plain]]) */
static Value str_find(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2 || argv[0].tag != VAL_STR || argv[1].tag != VAL_STR) 
    return V_nil();
  
  const char *s = argv[0].as.s->data;
  size_t sl = (size_t)argv[0].as.s->len;
  const char *p = argv[1].as.s->data;
  size_t pl = (size_t)argv[1].as.s->len;
  
  int init = 1;
  if (argc >= 3) {
    if (argv[2].tag == VAL_INT) init = (int)argv[2].as.i;
    else if (argv[2].tag == VAL_NUM) init = (int)argv[2].as.n;
  }
  
  bool plain = false;
  if (argc >= 4) {
    plain = (argv[3].tag == VAL_BOOL) ? (argv[3].as.b != 0) : false;
  }
  
  init = lua_index_adjust(init, (int)sl);
  if (init < 1) init = 1;
  if (init > (int)sl + 1) return V_nil();
  
  if (plain || pl == 0) {
    /* Plain string search */
    const char *found = lmemfind(s + init - 1, sl - (size_t)init + 1, p, pl);
    if (found) {
      int start = (int)(found - s) + 1;
      int end = start + (int)pl - 1;
      Value t = V_table();
      tbl_set_public(t.as.t, V_int(1), V_int(start));
      tbl_set_public(t.as.t, V_int(2), V_int(end));
      return t;
    }
    return V_nil();
  } else {
    /* Pattern matching */
    MatchState ms;
    const char *s1 = s + init - 1;
    const char *s2;
    
    ms.src_init = s;
    ms.src_end = s + sl;
    ms.p_end = p + pl;
    ms.level = 0;
    
    /* Handle anchor */
    bool anchor = (*p == '^');
    if (anchor) p++;  /* skip anchor */
    
    do {
      ms.level = 0;
      s2 = match(&ms, s1, p);
      if (s2 != NULL) {
        int start = (int)(s1 - s) + 1;
        int end = (int)(s2 - s);
        Value t = V_table();
        tbl_set_public(t.as.t, V_int(1), V_int(start));
        tbl_set_public(t.as.t, V_int(2), V_int(end));
        
        /* Add captures */
        for (int i = 0; i < ms.level; i++) {
          if (ms.capture[i].len >= 0) {
            Value cap = V_str_copy_n(ms.capture[i].init, (size_t)ms.capture[i].len);
            tbl_set_public(t.as.t, V_int(i + 3), cap);
          }
        }
        return t;
      }
    } while (s1++ < ms.src_end && !anchor);
    
    return V_nil();
  }
}

/* string.match(s, pattern [, init]) */
static Value str_match(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2 || argv[0].tag != VAL_STR || argv[1].tag != VAL_STR) 
    return V_nil();
  
  const char *s = argv[0].as.s->data;
  size_t sl = (size_t)argv[0].as.s->len;
  const char *p = argv[1].as.s->data;
  size_t pl = (size_t)argv[1].as.s->len;
  
  int init = 1;
  if (argc >= 3) {
    if (argv[2].tag == VAL_INT) init = (int)argv[2].as.i;
    else if (argv[2].tag == VAL_NUM) init = (int)argv[2].as.n;
  }
  
  init = lua_index_adjust(init, (int)sl);
  if (init < 1) init = 1;
  if (init > (int)sl + 1) return V_nil();
  
  MatchState ms;
  const char *s1 = s + init - 1;
  const char *s2;
  
  ms.src_init = s;
  ms.src_end = s + sl;
  ms.p_end = p + pl;
  ms.level = 0;
  
  /* Handle anchor */
  bool anchor = (*p == '^');
  if (anchor) p++;  /* skip anchor */
  
  do {
    ms.level = 0;
    s2 = match(&ms, s1, p);
    if (s2 != NULL) {
      if (ms.level > 0) {
        /* Return captures */
        if (ms.level == 1) {
          if (ms.capture[0].len >= 0) {
            return V_str_copy_n(ms.capture[0].init, (size_t)ms.capture[0].len);
          }
          return V_nil();
        } else {
          Value t = V_table();
          for (int i = 0; i < ms.level; i++) {
            if (ms.capture[i].len >= 0) {
              Value cap = V_str_copy_n(ms.capture[i].init, (size_t)ms.capture[i].len);
              tbl_set_public(t.as.t, V_int(i + 1), cap);
            }
          }
          return t;
        }
      } else {
        /* Return whole match */
        size_t match_len = (size_t)(s2 - s1);
        return V_str_copy_n(s1, match_len);
      }
    }
  } while (s1++ < ms.src_end && !anchor);
  
  return V_nil();
}

/* Iterator state for gmatch (simple, global static pos) */
static Value gmatch_next(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2 || argv[0].tag != VAL_STR || argv[1].tag != VAL_STR) 
    return V_nil();
    
  static int pos = 1; /* simplistic; not thread-safe across different strings */
  
  const char *s = argv[0].as.s->data;
  size_t sl = (size_t)argv[0].as.s->len;
  const char *p = argv[1].as.s->data;
  size_t pl = (size_t)argv[1].as.s->len;
  
  if (pos > (int)sl) return V_nil();
  
  MatchState ms;
  const char *s1 = s + pos - 1;
  const char *s2;
  
  ms.src_init = s;
  ms.src_end = s + sl;
  ms.p_end = p + pl;
  ms.level = 0;
  
  while (s1 <= ms.src_end) {
    ms.level = 0;
    s2 = match(&ms, s1, p);
    if (s2 != NULL) {
      pos = (int)(s2 - s) + 1;
      if (s2 == s1) pos++; /* empty match, advance */
      
      if (ms.level > 0) {
        if (ms.level == 1) {
          if (ms.capture[0].len >= 0) {
            return V_str_copy_n(ms.capture[0].init, (size_t)ms.capture[0].len);
          }
        } else {
          Value t = V_table();
          for (int i = 0; i < ms.level; i++) {
            if (ms.capture[i].len >= 0) {
              Value cap = V_str_copy_n(ms.capture[i].init, (size_t)ms.capture[i].len);
              tbl_set_public(t.as.t, V_int(i + 1), cap);
            }
          }
          return t;
        }
      } else {
        size_t match_len = (size_t)(s2 - s1);
        return V_str_copy_n(s1, match_len);
      }
    }
    s1++;
  }
  
  pos = 1; /* reset */
  return V_nil();
}

/* string.gmatch(s, pattern) */
static Value str_gmatch(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2 || argv[0].tag != VAL_STR || argv[1].tag != VAL_STR) 
    return V_nil();
  Value iter;
  iter.tag = VAL_CFUNC;
  iter.as.cfunc = gmatch_next;
  return iter;
}

/* ---------- tiny builder for gsub ---------- */
static void gb_reserve(char **buf, size_t *cap, size_t need) {
  if (*cap >= need) return;
  size_t ncap = *cap ? *cap : 128;
  while (ncap < need) ncap *= 2;
  char *nb = (char*)realloc(*buf, ncap);
  if (!nb) { fprintf(stderr,"OOM\n"); exit(1); }
  *buf = nb; *cap = ncap;
}
static void gb_append_n(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
  gb_reserve(buf, cap, *len + n + 1);
  memcpy(*buf + *len, s, n);
  *len += n;
  (*buf)[*len] = '\0';
}
static void gb_append_c(char **buf, size_t *len, size_t *cap, char c) {
  gb_reserve(buf, cap, *len + 2);
  (*buf)[(*len)++] = c;
  (*buf)[*len] = '\0';
}

/* helper: append capture k (0=whole match) */
static void gsub_append_capture(char **out, size_t *olen, size_t *ocap,
                                MatchState *ms, int k,
                                const char *match_start, const char *match_end) {
  if (k == 0) {
    size_t L = (size_t)(match_end - match_start);
    gb_append_n(out, olen, ocap, match_start, L);
    return;
  }
  if (k < 0 || k > ms->level) return;
  if (ms->capture[k-1].len >= 0) {
    size_t L = (size_t)ms->capture[k-1].len;
    gb_append_n(out, olen, ocap, ms->capture[k-1].init, L);
  }
}

/* Expand replacement (string/table/function). */
static void gsub_expand_repl(VM *vm,
                             char **out, size_t *olen, size_t *ocap,
                             Value repl,
                             MatchState *ms,
                             const char *match_start, const char *match_end) {
  if (repl.tag == VAL_STR) {
    const char *rs = repl.as.s->data;
    size_t rl = (size_t)repl.as.s->len;
    for (size_t i = 0; i < rl; i++) {
      char c = rs[i];
      if (c != '%') { gb_append_c(out, olen, ocap, c); continue; }
      if (i + 1 >= rl) { gb_append_c(out, olen, ocap, '%'); break; }
      char n = rs[++i];
      if (n == '%') { gb_append_c(out, olen, ocap, '%'); continue; }
      if (n >= '0' && n <= '9') {
        int capn = n - '0';
        gsub_append_capture(out, olen, ocap, ms, capn, match_start, match_end);
      } else {
        /* treat as literal: `%x` -> '%' then 'x' */
        gb_append_c(out, olen, ocap, '%');
        gb_append_c(out, olen, ocap, n);
      }
    }
    return;
  }

  if (repl.tag == VAL_TABLE) {
    /* key = first capture if present, else whole match */
    Value key = V_nil();
    if (ms->level > 0 && ms->capture[0].len >= 0) {
      key = V_str_copy_n(ms->capture[0].init, (size_t)ms->capture[0].len);
    } else {
      key = V_str_copy_n(match_start, (size_t)(match_end - match_start));
    }
    if (key.tag != VAL_NIL) {
      Value val;
      if (tbl_get_public(repl.as.t, key, &val) && val.tag == VAL_STR) {
        gsub_expand_repl(vm, out, olen, ocap, val, ms, match_start, match_end);
      }
    }
    return;
  }

  if (repl.tag == VAL_FUNC || repl.tag == VAL_CFUNC) {
    /* call with captures if any; else whole match */
    int argc = (ms->level > 0) ? ms->level : 1;
    Value *args = (Value*)malloc(sizeof(Value)*argc);
    if (!args) { fprintf(stderr,"OOM\n"); exit(1); }
    if (ms->level > 0) {
      for (int i = 0; i < ms->level; i++) {
        if (ms->capture[i].len >= 0) {
          args[i] = V_str_copy_n(ms->capture[i].init, (size_t)ms->capture[i].len);
        } else { args[i] = V_str_copy_n("", 0); }
      }
    } else {
      args[0] = V_str_copy_n(match_start, (size_t)(match_end - match_start));
    }
    Value rv = call_any_public(vm, repl, argc, args);
    free(args);
    if (rv.tag == VAL_STR) {
      gb_append_n(out, olen, ocap, rv.as.s->data, (size_t)rv.as.s->len);
    }
    return;
  }
  /* other types: append nothing */
}

/* string.gsub(s, pattern, repl [, n]) -> {string, count} */
static Value str_gsub(struct VM *vm, int argc, Value *argv) {
  if (argc < 3 || argv[0].tag != VAL_STR || argv[1].tag != VAL_STR) 
    return V_nil();

  const char *src = argv[0].as.s->data;
  size_t      sl  = (size_t)argv[0].as.s->len;
  const char *pat = argv[1].as.s->data;
  size_t      pl  = (size_t)argv[1].as.s->len;
  Value       repl = argv[2];

  int limit = -1;
  if (argc >= 4) {
    if (argv[3].tag == VAL_INT) limit = (int)argv[3].as.i;
    else if (argv[3].tag == VAL_NUM) limit = (int)argv[3].as.n;
  }

  MatchState ms;
  ms.src_init = src;
  ms.src_end  = src + sl;
  ms.p_end    = pat + pl;

  bool anchor = (pl > 0 && *pat == '^');
  if (anchor) pat++; /* skip '^' */

  char  *out = NULL;
  size_t olen = 0, ocap = 0;
  const char *p = src;
  const char *last = src;
  int count = 0;

  while (p < ms.src_end && (limit < 0 || count < limit)) {
    ms.level = 0;
    const char *e = match(&ms, p, pat);
    if (e != NULL) {
      /* copy src[last .. p-1] */
      if (p > last) {
        gb_append_n(&out, &olen, &ocap, last, (size_t)(p - last));
      }
      /* expand replacement */
      gsub_expand_repl(vm, &out, &olen, &ocap, repl, &ms, p, e);
      count++;

      /* advance */
      if (e == p) {          /* empty match: avoid infinite loop */
        gb_append_c(&out, &olen, &ocap, *p);
        p++; last = p;
      } else {
        p = e; last = p;
      }
      if (anchor) break;     /* anchored: only once at beginning */
    } else {
      /* no match at p: copy char and advance one */
      gb_append_c(&out, &olen, &ocap, *p);
      p++; last = p;
    }
  }

  /* copy the tail */
  if (last < ms.src_end) {
    gb_append_n(&out, &olen, &ocap, last, (size_t)(ms.src_end - last));
  }

  /* no changes? return original and 0 */
  if (!out) {
    Value ret = V_table();
    tbl_set_public(ret.as.t, V_int(1), argv[0]);
    tbl_set_public(ret.as.t, V_int(2), V_int(0));
    return ret;
  }

  Value outstr = V_str_copy_n(out, olen);
  free(out);

  Value ret = V_table();
  tbl_set_public(ret.as.t, V_int(1), outstr);
  tbl_set_public(ret.as.t, V_int(2), V_int(count));
  return ret;
}

/* ========= string.format ========= */

/* String builder helpers (format) */
static void sb_reserve(char **buf, size_t *cap, size_t need) {
  if (*cap >= need) return;
  size_t ncap = *cap ? *cap : 128;
  while (ncap < need) ncap *= 2;
  char *nb = (char*)realloc(*buf, ncap);
  if (!nb) { fprintf(stderr, "OOM\n"); exit(1); }
  *buf = nb; *cap = ncap;
}

static void sb_append_n(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
  sb_reserve(buf, cap, *len + n + 1);
  memcpy(*buf + *len, s, n);
  *len += n;
  (*buf)[*len] = '\0';
}

static void sb_append_c(char **buf, size_t *len, size_t *cap, char c) {
  sb_reserve(buf, cap, *len + 2);
  (*buf)[(*len)++] = c;
  (*buf)[*len] = '\0';
}

/* Convert a Value to C-string for %s fallback */
static const char *val_to_cstr(Value v, char *tmp, size_t tmpsz) {
  switch (v.tag) {
    case VAL_STR:  return v.as.s->data;
    case VAL_INT:  snprintf(tmp, tmpsz, "%lld", v.as.i); return tmp;
    case VAL_NUM:  snprintf(tmp, tmpsz, "%.17g", v.as.n); return tmp;
    case VAL_BOOL: return v.as.b ? "true" : "false";
    case VAL_NIL:  return "nil";
    case VAL_TABLE: snprintf(tmp, tmpsz, "table:%p", (void*)v.as.t); return tmp;
    case VAL_FUNC:  snprintf(tmp, tmpsz, "function:%p", (void*)v.as.fn); return tmp;
    case VAL_CFUNC: snprintf(tmp, tmpsz, "function:%p", (void*)v.as.cfunc); return tmp;
    case VAL_COROUTINE: snprintf(tmp, tmpsz, "thread:%p", (void*)v.as.t); return tmp;
    default: return "<unknown>";
  }
}

/* string.format(fmt, ...) - Full Lua compatibility (common subset) */
static Value str_format(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();
  const char *fmt = argv[0].as.s->data;
  size_t fmt_len = (size_t)argv[0].as.s->len;

  char *out = NULL; 
  size_t len = 0, cap = 0;
  int argi = 1;

  for (size_t i = 0; i < fmt_len; i++) {
    if (fmt[i] != '%') {
      sb_append_c(&out, &len, &cap, fmt[i]);
      continue;
    }
    
    if (i + 1 < fmt_len && fmt[i + 1] == '%') {
      sb_append_c(&out, &len, &cap, '%');
      i++; /* skip second % */
      continue;
    }

    /* Parse format specifier */
    size_t spec_start = i;
    i++; /* skip '%' */
    
    /* Flags */
    bool left_align = false;
    bool show_sign = false;
    bool space_sign = false;
    bool zero_pad = false;
    bool alternate = false;
    
    while (i < fmt_len) {
      if (fmt[i] == '-') left_align = true;
      else if (fmt[i] == '+') show_sign = true;
      else if (fmt[i] == ' ') space_sign = true;
      else if (fmt[i] == '0') zero_pad = true;
      else if (fmt[i] == '#') alternate = true;
      else break;
      i++;
    }
    
    /* Width */
    int width = -1;
    if (i < fmt_len && fmt[i] >= '0' && fmt[i] <= '9') {
      width = 0;
      while (i < fmt_len && fmt[i] >= '0' && fmt[i] <= '9') {
        width = width * 10 + (fmt[i] - '0');
        i++;
      }
    }
    
    /* Precision */
    int precision = -1;
    if (i < fmt_len && fmt[i] == '.') {
      i++;
      precision = 0;
      while (i < fmt_len && fmt[i] >= '0' && fmt[i] <= '9') {
        precision = precision * 10 + (fmt[i] - '0');
        i++;
      }
    }
    
    /* Conversion specifier */
    char spec = (i < fmt_len) ? fmt[i] : '\0';
    if (!spec) {
      sb_append_c(&out, &len, &cap, '%');
      break;
    }

    Value arg = (argi < argc) ? argv[argi++] : V_nil();
    char tmp[512];
    
    switch (spec) {
      case 's': {
        const char *s = val_to_cstr(arg, tmp, sizeof(tmp));
        size_t slen = strlen(s);
        if (precision >= 0 && (size_t)precision < slen) 
          slen = (size_t)precision;
        
        if (width > 0 && !left_align && (size_t)width > slen) {
          for (int pad = 0; pad < width - (int)slen; pad++)
            sb_append_c(&out, &len, &cap, ' ');
        }
        sb_append_n(&out, &len, &cap, s, slen);
        if (width > 0 && left_align && (size_t)width > slen) {
          for (int pad = 0; pad < width - (int)slen; pad++)
            sb_append_c(&out, &len, &cap, ' ');
        }
        break;
      }
      case 'c': {
        int c = 0;
        if (arg.tag == VAL_INT) c = (int)arg.as.i;
        else if (arg.tag == VAL_NUM) c = (int)arg.as.n;
        if (c >= 0 && c <= 255) {
          sb_append_c(&out, &len, &cap, (char)c);
        }
        break;
      }
      case 'd': case 'i': {
        long long iv = 0;
        if (arg.tag == VAL_INT) iv = arg.as.i;
        else if (arg.tag == VAL_NUM) iv = (long long)arg.as.n;
        else if (arg.tag == VAL_BOOL) iv = arg.as.b ? 1 : 0;
        
        char fmt_str[32] = "%";
        char *p = fmt_str + 1;
        if (left_align) *p++ = '-';
        if (show_sign) *p++ = '+';
        if (space_sign) *p++ = ' ';
        if (zero_pad && !left_align) *p++ = '0';
        if (alternate) *p++ = '#';
        if (width > 0) p += sprintf(p, "%d", width);
        if (precision >= 0) p += sprintf(p, ".%d", precision);
        *p++ = 'l'; *p++ = 'l'; *p++ = 'd'; *p = '\0';
        
        snprintf(tmp, sizeof(tmp), fmt_str, iv);
        sb_append_n(&out, &len, &cap, tmp, strlen(tmp));
        break;
      }
      case 'o': case 'x': case 'X': {
        unsigned long long uv = 0;
        if (arg.tag == VAL_INT) uv = (unsigned long long)arg.as.i;
        else if (arg.tag == VAL_NUM) uv = (unsigned long long)arg.as.n;
        
        char fmt_str[32] = "%";
        char *p = fmt_str + 1;
        if (left_align) *p++ = '-';
        if (show_sign) *p++ = '+';
        if (space_sign) *p++ = ' ';
        if (zero_pad && !left_align) *p++ = '0';
        if (alternate) *p++ = '#';
        if (width > 0) p += sprintf(p, "%d", width);
        if (precision >= 0) p += sprintf(p, ".%d", precision);
        *p++ = 'l'; *p++ = 'l'; *p++ = spec; *p = '\0';
        
        snprintf(tmp, sizeof(tmp), fmt_str, uv);
        sb_append_n(&out, &len, &cap, tmp, strlen(tmp));
        break;
      }
      case 'u': {
        unsigned long long uv = 0;
        if (arg.tag == VAL_INT) uv = (unsigned long long)arg.as.i;
        else if (arg.tag == VAL_NUM) uv = (unsigned long long)arg.as.n;
        
        char fmt_str[32] = "%";
        char *p = fmt_str + 1;
        if (left_align) *p++ = '-';
        if (show_sign) *p++ = '+';
        if (space_sign) *p++ = ' ';
        if (zero_pad && !left_align) *p++ = '0';
        if (alternate) *p++ = '#';
        if (width > 0) p += sprintf(p, "%d", width);
        if (precision >= 0) p += sprintf(p, ".%d", precision);
        *p++ = 'l'; *p++ = 'l'; *p++ = 'u'; *p = '\0';
        
        snprintf(tmp, sizeof(tmp), fmt_str, uv);
        sb_append_n(&out, &len, &cap, tmp, strlen(tmp));
        break;
      }
      case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
        double dv = 0.0;
        if (arg.tag == VAL_NUM) dv = arg.as.n;
        else if (arg.tag == VAL_INT) dv = (double)arg.as.i;
        else if (arg.tag == VAL_BOOL) dv = arg.as.b ? 1.0 : 0.0;
        
        char fmt_str[32] = "%";
        char *p = fmt_str + 1;
        if (left_align) *p++ = '-';
        if (show_sign) *p++ = '+';
        if (space_sign) *p++ = ' ';
        if (zero_pad && !left_align) *p++ = '0';
        if (alternate) *p++ = '#';
        if (width > 0) p += sprintf(p, "%d", width);
        if (precision >= 0) p += sprintf(p, ".%d", precision);
        *p++ = spec; *p = '\0';
        
        snprintf(tmp, sizeof(tmp), fmt_str, dv);
        sb_append_n(&out, &len, &cap, tmp, strlen(tmp));
        break;
      }
      case 'q': {
        /* Lua-specific: quoted string */
        sb_append_c(&out, &len, &cap, '"');
        const char *s = val_to_cstr(arg, tmp, sizeof(tmp));
        for (size_t j = 0; s[j]; j++) {
          char c = s[j];
          if (c == '"' || c == '\\') {
            sb_append_c(&out, &len, &cap, '\\');
            sb_append_c(&out, &len, &cap, c);
          } else if (c == '\n') {
            sb_append_c(&out, &len, &cap, '\\');
            sb_append_c(&out, &len, &cap, 'n');
          } else if (c == '\r') {
            sb_append_c(&out, &len, &cap, '\\');
            sb_append_c(&out, &len, &cap, 'r');
          } else if (c == '\t') {
            sb_append_c(&out, &len, &cap, '\\');
            sb_append_c(&out, &len, &cap, 't');
          } else if (c >= 32 && c <= 126) {
            sb_append_c(&out, &len, &cap, c);
          } else {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\%03d", (unsigned char)c);
            sb_append_n(&out, &len, &cap, esc, strlen(esc));
          }
        }
        sb_append_c(&out, &len, &cap, '"');
        break;
      }
      default: {
        /* Unknown specifier: output literally */
        sb_append_n(&out, &len, &cap, fmt + spec_start, i - spec_start + 1);
        break;
      }
    }
  }

  if (!out) out = strdup("");
  Value v = V_str_copy_n(out, strlen(out));
  free(out);
  return v;
}

/* string.length(s) - Lua++ extension (alias for len) */
static Value str_length(struct VM *vm, int argc, Value *argv) {
  return str_len(vm, argc, argv);  /* Just call str_len */
}
static regex_t* compile_regex_cached(const char *pattern, int cflags) {
    regex_t *preg = (regex_t*)malloc(sizeof(regex_t));
    if (!preg) { fprintf(stderr,"OOM\n"); exit(1); }
    
    int ret = regcomp(preg, pattern, cflags | REG_EXTENDED);
    if (ret != 0) {
        char errbuf[256];
        regerror(ret, preg, errbuf, sizeof(errbuf));
        fprintf(stderr, "Regex compilation error: %s\n", errbuf);
        free(preg);
        return NULL;
    }
    return preg;
}

/* string.refind(s, pattern [, init [, flags]]) -> {start, end, captures...} or nil */
static Value str_refind(struct VM *vm, int argc, Value *argv) {
    (void)vm;
    if (argc < 2 || argv[0].tag != VAL_STR || argv[1].tag != VAL_STR) 
        return V_nil();
    
    const char *s = argv[0].as.s->data;
    size_t sl = (size_t)argv[0].as.s->len;
    const char *pattern = argv[1].as.s->data;
    
    int init = 1;
    if (argc >= 3) {
        if (argv[2].tag == VAL_INT) init = (int)argv[2].as.i;
        else if (argv[2].tag == VAL_NUM) init = (int)argv[2].as.n;
    }
    
    /* Parse flags */
    int cflags = 0;
    if (argc >= 4 && argv[3].tag == VAL_STR) {
        const char *flags = argv[3].as.s->data;
        for (int i = 0; flags[i]; i++) {
            switch (flags[i]) {
                case 'i': cflags |= REG_ICASE; break;
                case 'm': cflags |= REG_NEWLINE; break;
                default: break;
            }
        }
    }
    
    init = lua_index_adjust(init, (int)sl);
    if (init < 1) init = 1;
    if (init > (int)sl + 1) return V_nil();
    
    /* Compile regex */
    regex_t *preg = compile_regex_cached(pattern, cflags);
    if (!preg) return V_nil();
    
    /* Execute regex */
    regmatch_t matches[LUA_MAXCAPTURES];
    int ret = regexec(preg, s + init - 1, LUA_MAXCAPTURES, matches, 0);
    
    regfree(preg);
    free(preg);
    
    if (ret == REG_NOMATCH) return V_nil();
    if (ret != 0) return V_nil();
    
    /* Build result table {start, end, capture1, capture2, ...} */
    Value t = V_table();
    
    if (matches[0].rm_so != -1) {
        int start = (int)matches[0].rm_so + init;
        int end = (int)matches[0].rm_eo + init - 1;
        tbl_set_public(t.as.t, V_int(1), V_int(start));
        tbl_set_public(t.as.t, V_int(2), V_int(end));
        
        /* Add captures starting at index 3 */
        int idx = 3;
        for (int i = 1; i < LUA_MAXCAPTURES && matches[i].rm_so != -1; i++) {
            Value cap = V_str_copy_n(s + init - 1 + matches[i].rm_so,
                                    (size_t)(matches[i].rm_eo - matches[i].rm_so));
            tbl_set_public(t.as.t, V_int(idx++), cap);
        }
    }
    
    return t;
}

/* string.rematch(s, pattern [, init [, flags]]) -> match or {captures...} or nil */
static Value str_rematch(struct VM *vm, int argc, Value *argv) {
    (void)vm;
    if (argc < 2 || argv[0].tag != VAL_STR || argv[1].tag != VAL_STR) 
        return V_nil();
    
    const char *s = argv[0].as.s->data;
    size_t sl = (size_t)argv[0].as.s->len;
    const char *pattern = argv[1].as.s->data;
    
    int init = 1;
    if (argc >= 3) {
        if (argv[2].tag == VAL_INT) init = (int)argv[2].as.i;
        else if (argv[2].tag == VAL_NUM) init = (int)argv[2].as.n;
    }
    
    /* Parse flags */
    int cflags = 0;
    if (argc >= 4 && argv[3].tag == VAL_STR) {
        const char *flags = argv[3].as.s->data;
        for (int i = 0; flags[i]; i++) {
            switch (flags[i]) {
                case 'i': cflags |= REG_ICASE; break;
                case 'm': cflags |= REG_NEWLINE; break;
                default: break;
            }
        }
    }
    
    init = lua_index_adjust(init, (int)sl);
    if (init < 1) init = 1;
    if (init > (int)sl + 1) return V_nil();
    
    /* Compile regex */
    regex_t *preg = compile_regex_cached(pattern, cflags);
    if (!preg) return V_nil();
    
    /* Execute regex */
    regmatch_t matches[LUA_MAXCAPTURES];
    int ret = regexec(preg, s + init - 1, LUA_MAXCAPTURES, matches, 0);
    
    regfree(preg);
    free(preg);
    
    if (ret == REG_NOMATCH) return V_nil();
    if (ret != 0) return V_nil();
    
    /* Count captures */
    int capture_count = 0;
    for (int i = 1; i < LUA_MAXCAPTURES && matches[i].rm_so != -1; i++) {
        capture_count++;
    }
    
    /* If we have captures, return them */
    if (capture_count > 0) {
        if (capture_count == 1) {
            /* Single capture: return as string */
            return V_str_copy_n(s + init - 1 + matches[1].rm_so,
                              (size_t)(matches[1].rm_eo - matches[1].rm_so));
        } else {
            /* Multiple captures: return as table */
            Value t = V_table();
            for (int i = 1; i <= capture_count; i++) {
                Value cap = V_str_copy_n(s + init - 1 + matches[i].rm_so,
                                        (size_t)(matches[i].rm_eo - matches[i].rm_so));
                tbl_set_public(t.as.t, V_int(i), cap);
            }
            return t;
        }
    }
    
    /* No captures: return whole match */
    if (matches[0].rm_so != -1) {
        return V_str_copy_n(s + init - 1 + matches[0].rm_so,
                          (size_t)(matches[0].rm_eo - matches[0].rm_so));
    }
    
    return V_nil();
}

/* string.regsub(s, pattern, repl [, n [, flags]]) -> {string, count} */
static Value str_regsub(struct VM *vm, int argc, Value *argv) {
    if (argc < 3 || argv[0].tag != VAL_STR || argv[1].tag != VAL_STR) 
        return V_nil();
    
    const char *src = argv[0].as.s->data;
    size_t sl = (size_t)argv[0].as.s->len;
    const char *pattern = argv[1].as.s->data;
    Value repl = argv[2];
    
    int limit = -1;
    if (argc >= 4) {
        if (argv[3].tag == VAL_INT) limit = (int)argv[3].as.i;
        else if (argv[3].tag == VAL_NUM) limit = (int)argv[3].as.n;
    }
    
    /* Parse flags */
    int cflags = 0;
    if (argc >= 5 && argv[4].tag == VAL_STR) {
        const char *flags = argv[4].as.s->data;
        for (int i = 0; flags[i]; i++) {
            switch (flags[i]) {
                case 'i': cflags |= REG_ICASE; break;
                case 'm': cflags |= REG_NEWLINE; break;
                default: break;
            }
        }
    }
    
    /* Compile regex */
    regex_t *preg = compile_regex_cached(pattern, cflags);
    if (!preg) return V_nil();
    
    /* Build result string */
    char *out = NULL;
    size_t olen = 0, ocap = 0;
    
    const char *pos = src;
    int count = 0;
    
    while (pos < src + sl && (limit < 0 || count < limit)) {
        regmatch_t matches[LUA_MAXCAPTURES];
        int ret = regexec(preg, pos, LUA_MAXCAPTURES, matches, 0);
        
        if (ret == REG_NOMATCH) break;
        if (ret != 0) break;
        
        /* Append text before match */
        size_t before_len = (size_t)matches[0].rm_so;
        if (before_len > 0) {
            gb_reserve(&out, &ocap, olen + before_len + 1);
            memcpy(out + olen, pos, before_len);
            olen += before_len;
            out[olen] = '\0';
        }
        
        /* Append replacement */
        if (repl.tag == VAL_STR) {
            const char *rs = repl.as.s->data;
            size_t rl = (size_t)repl.as.s->len;
            
            /* Handle $0, $1, $2, etc. in replacement */
            for (size_t i = 0; i < rl; i++) {
                if (rs[i] == '$' && i + 1 < rl && rs[i+1] >= '0' && rs[i+1] <= '9') {
                    int cap_idx = rs[i+1] - '0';
                    if (cap_idx < LUA_MAXCAPTURES && matches[cap_idx].rm_so != -1) {
                        size_t cap_len = (size_t)(matches[cap_idx].rm_eo - matches[cap_idx].rm_so);
                        gb_reserve(&out, &ocap, olen + cap_len + 1);
                        memcpy(out + olen, pos + matches[cap_idx].rm_so, cap_len);
                        olen += cap_len;
                        out[olen] = '\0';
                    }
                    i++;  /* Skip digit */
                } else {
                    gb_reserve(&out, &ocap, olen + 2);
                    out[olen++] = rs[i];
                    out[olen] = '\0';
                }
            }
        } else if (repl.tag == VAL_FUNC || repl.tag == VAL_CFUNC) {
            /* Call function with match */
            Value match_str = V_str_copy_n(pos + matches[0].rm_so, 
                                          (size_t)(matches[0].rm_eo - matches[0].rm_so));
            Value args[1] = { match_str };
            Value result_val = call_any_public(vm, repl, 1, args);
            
            if (result_val.tag == VAL_STR) {
                size_t repl_len = (size_t)result_val.as.s->len;
                gb_reserve(&out, &ocap, olen + repl_len + 1);
                memcpy(out + olen, result_val.as.s->data, repl_len);
                olen += repl_len;
                out[olen] = '\0';
            }
        } else if (repl.tag == VAL_TABLE) {
            /* Use first capture or whole match as key */
            Value key;
            if (matches[1].rm_so != -1) {
                key = V_str_copy_n(pos + matches[1].rm_so,
                                  (size_t)(matches[1].rm_eo - matches[1].rm_so));
            } else {
                key = V_str_copy_n(pos + matches[0].rm_so,
                                  (size_t)(matches[0].rm_eo - matches[0].rm_so));
            }
            
            Value val;
            if (tbl_get_public(repl.as.t, key, &val) && val.tag == VAL_STR) {
                size_t repl_len = (size_t)val.as.s->len;
                gb_reserve(&out, &ocap, olen + repl_len + 1);
                memcpy(out + olen, val.as.s->data, repl_len);
                olen += repl_len;
                out[olen] = '\0';
            }
        }
        
        count++;
        pos += matches[0].rm_eo;
        
        /* Handle empty matches */
        if (matches[0].rm_eo == 0) {
            if (pos < src + sl) {
                gb_reserve(&out, &ocap, olen + 2);
                out[olen++] = *pos++;
                out[olen] = '\0';
            } else {
                break;
            }
        }
    }
    
    /* Append remaining text */
    size_t remaining = src + sl - pos;
    if (remaining > 0) {
        gb_reserve(&out, &ocap, olen + remaining + 1);
        memcpy(out + olen, pos, remaining);
        olen += remaining;
        out[olen] = '\0';
    }
    
    regfree(preg);
    free(preg);
    
    /* Return {string, count} */
    Value ret = V_table();
    if (out) {
        Value result_str = V_str_copy_n(out, olen);
        free(out);
        tbl_set_public(ret.as.t, V_int(1), result_str);
    } else {
        tbl_set_public(ret.as.t, V_int(1), argv[0]);
    }
    tbl_set_public(ret.as.t, V_int(2), V_int(count));
    
    return ret;
}

/* string.retest(s, pattern [, flags]) -> boolean */
static Value str_retest(struct VM *vm, int argc, Value *argv) {
    (void)vm;
    if (argc < 2 || argv[0].tag != VAL_STR || argv[1].tag != VAL_STR) 
        return V_bool(0);
    
    const char *s = argv[0].as.s->data;
    const char *pattern = argv[1].as.s->data;
    
    /* Parse flags */
    int cflags = 0;
    if (argc >= 3 && argv[2].tag == VAL_STR) {
        const char *flags = argv[2].as.s->data;
        for (int i = 0; flags[i]; i++) {
            switch (flags[i]) {
                case 'i': cflags |= REG_ICASE; break;
                case 'm': cflags |= REG_NEWLINE; break;
                default: break;
            }
        }
    }
    
    /* Compile regex */
    regex_t *preg = compile_regex_cached(pattern, cflags);
    if (!preg) return V_bool(0);
    
    /* Test */
    int ret = regexec(preg, s, 0, NULL, 0);
    
    regfree(preg);
    free(preg);
    
    return V_bool(ret == 0);
}
/* Register the complete string library */
void register_string_lib(struct VM *vm) {
  Value S = V_table();
  
  /* Core string functions */
  tbl_set_public(S.as.t, V_str_from_c("byte"),    (Value){.tag=VAL_CFUNC,.as.cfunc=str_byte});
  tbl_set_public(S.as.t, V_str_from_c("char"),    (Value){.tag=VAL_CFUNC,.as.cfunc=str_char});
  tbl_set_public(S.as.t, V_str_from_c("find"),    (Value){.tag=VAL_CFUNC,.as.cfunc=str_find});
  tbl_set_public(S.as.t, V_str_from_c("format"),  (Value){.tag=VAL_CFUNC,.as.cfunc=str_format});
  tbl_set_public(S.as.t, V_str_from_c("gmatch"),  (Value){.tag=VAL_CFUNC,.as.cfunc=str_gmatch});
  tbl_set_public(S.as.t, V_str_from_c("gsub"),    (Value){.tag=VAL_CFUNC,.as.cfunc=str_gsub});
  tbl_set_public(S.as.t, V_str_from_c("len"),     (Value){.tag=VAL_CFUNC,.as.cfunc=str_len});
  tbl_set_public(S.as.t, V_str_from_c("lower"),   (Value){.tag=VAL_CFUNC,.as.cfunc=str_lower});
  tbl_set_public(S.as.t, V_str_from_c("match"),   (Value){.tag=VAL_CFUNC,.as.cfunc=str_match});
  tbl_set_public(S.as.t, V_str_from_c("rep"),     (Value){.tag=VAL_CFUNC,.as.cfunc=str_rep});
  tbl_set_public(S.as.t, V_str_from_c("reverse"), (Value){.tag=VAL_CFUNC,.as.cfunc=str_reverse});
  tbl_set_public(S.as.t, V_str_from_c("sub"),     (Value){.tag=VAL_CFUNC,.as.cfunc=str_sub});
  tbl_set_public(S.as.t, V_str_from_c("upper"),   (Value){.tag=VAL_CFUNC,.as.cfunc=str_upper});
   tbl_set_public(S.as.t, V_str_from_c("refind"),  (Value){.tag=VAL_CFUNC,.as.cfunc=str_refind});
  tbl_set_public(S.as.t, V_str_from_c("rematch"), (Value){.tag=VAL_CFUNC,.as.cfunc=str_rematch});
  tbl_set_public(S.as.t, V_str_from_c("regsub"),  (Value){.tag=VAL_CFUNC,.as.cfunc=str_regsub});
  tbl_set_public(S.as.t, V_str_from_c("retest"),  (Value){.tag=VAL_CFUNC,.as.cfunc=str_retest});
  /* Lua++ extensions */
  tbl_set_public(S.as.t, V_str_from_c("length"),  (Value){.tag=VAL_CFUNC,.as.cfunc=str_length});

  env_add_public(vm->env, "string", S, false);
}
