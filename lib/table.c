// lib/table.c - Production-level Lua-compatible table library
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include "../include/interpreter.h"

/* Maximum values for Lua compatibility */
#define MAX_INT   ((1LL << 53) - 1)  /* 2^53 - 1, max safe integer in double */
#define MAXASIZE  (1 << 26)          /* Maximum array size */

/* ---- Small helpers ---- */

static char *sdup(const char *s) {
  if (!s) s = "";
  size_t n = strlen(s) + 1;
  char *p = (char*)malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}

static int is_callable(Value v) {
  return v.tag == VAL_CFUNC || v.tag == VAL_FUNC;
}

/* does t[i] exist and is non-nil? (1-indexed) */
static int index_present(Table *t, int i) {
  if (!t || i < 1) return 0;
  Value tmp;
  return tbl_get_public(t, V_int(i), &tmp) && tmp.tag != VAL_NIL;
}

/* Get the length of array part (1-indexed contiguous sequence).
   Safe on empty/holey arrays, never probes index 0, never returns negative. */
static int get_array_length(Table *t) {
  if (!t) return 0;

  if (!index_present(t, 1)) return 0;   /* quick exit: empty array */

  int lo = 1;
  int hi = 2;

  /* Find an upper bound where element is absent */
  while (index_present(t, hi)) {
    if (hi > INT_MAX / 2) { hi = INT_MAX; break; } /* clamp growth */
    lo = hi;
    hi *= 2;
  }

  /* Binary search between (lo, hi]; invariant: present(lo)=true, present(hi)=false */
  while (lo + 1 < hi) {
    int mid = lo + (hi - lo) / 2;
    if (index_present(t, mid)) lo = mid; else hi = mid;
  }
  return lo;
}

/* Check if value can be converted to integer */
static int to_integer(Value v, long long *result) {
  switch (v.tag) {
    case VAL_INT: *result = v.as.i; return 1;
    case VAL_NUM: {
      double d = v.as.n;
      if (d >= (double)LLONG_MIN && d <= (double)LLONG_MAX && floor(d) == d) {
        *result = (long long)d; return 1;
      }
      return 0;
    }
    default: return 0;
  }
}

/* Convert value to number */
static double to_number(Value v) {
  switch (v.tag) {
    case VAL_INT:  return (double)v.as.i;
    case VAL_NUM:  return v.as.n;
    case VAL_BOOL: return v.as.b ? 1.0 : 0.0;
    default:       return 0.0;
  }
}

/* Convert value to string for concatenation / display */
static char *value_to_string(Value v) {
  char buf[128];
  switch (v.tag) {
    case VAL_STR: {
      char *s = (char*)malloc((size_t)v.as.s->len + 1);
      if (s) {
        memcpy(s, v.as.s->data, (size_t)v.as.s->len);
        s[v.as.s->len] = '\0';
      }
      return s;
    }
    case VAL_NIL:   return sdup("nil");
    case VAL_BOOL:  return sdup(v.as.b ? "true" : "false");
    case VAL_INT:   snprintf(buf, sizeof(buf), "%lld", v.as.i); return sdup(buf);
    case VAL_NUM: {
      double d = v.as.n;
      if (floor(d) == d && d >= (double)LLONG_MIN && d <= (double)LLONG_MAX)
        snprintf(buf, sizeof(buf), "%.0f", d);
      else
        snprintf(buf, sizeof(buf), "%.14g", d);
      return sdup(buf);
    }
    case VAL_TABLE:  return sdup("table");
    case VAL_FUNC:   return sdup("function");
    case VAL_CFUNC:  return sdup("function");
#ifdef VAL_COROUTINE
    case VAL_COROUTINE: return sdup("thread");
#endif
    default:         return sdup("<unknown>");
  }
}

/* Error reporting helper (raises when VM available) */
static Value table_error(const char *msg) {
  fprintf(stderr, "table error: %s\n", msg);
  return V_nil();
}

/* ---- Core Table Functions ---- */

/* table.concat(list [, sep [, i [, j]]]) */
static Value tbl_concat(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_TABLE)
    return table_error("bad argument #1 to 'concat' (table expected)");

  Table *t = argv[0].as.t;

  /* separator */
  const char *sep = "";
  size_t sep_len = 0;
  if (argc >= 2) {
    if (argv[1].tag == VAL_STR) { sep = argv[1].as.s->data; sep_len = argv[1].as.s->len; }
    else if (argv[1].tag != VAL_NIL) return table_error("bad argument #2 to 'concat' (string expected)");
  }

  long long i = 1;
  long long j = get_array_length(t);
  if (argc >= 3 && !to_integer(argv[2], &i)) return table_error("bad argument #3 to 'concat' (number expected)");
  if (argc >= 4 && !to_integer(argv[3], &j)) return table_error("bad argument #4 to 'concat' (number expected)");
  if (i < 1) i = 1;
  if (j < i) return V_str_from_c("");

  /* size guard */
  if (j - i + 1 > MAXASIZE) return table_error("too many elements");

  long long count = j - i + 1;
  char **strings = (char**)malloc(sizeof(char*) * (size_t)count);
  if (!strings) return table_error("out of memory");

  size_t total_len = 0;
  for (long long idx = 0; idx < count; idx++) {
    Value v;
    if (!tbl_get_public(t, V_int((int)(i + idx)), &v)) v = V_nil();

    if (v.tag != VAL_STR && v.tag != VAL_INT && v.tag != VAL_NUM) {
      for (long long k = 0; k < idx; k++) free(strings[k]);
      free(strings);
      return table_error("invalid value for concatenation");
    }

    strings[idx] = value_to_string(v);
    if (strings[idx]) {
      total_len += strlen(strings[idx]);
      if (idx < count - 1) total_len += sep_len;
    }
  }

  char *result = (char*)malloc(total_len + 1);
  if (!result) {
    for (long long k = 0; k < count; k++) free(strings[k]);
    free(strings);
    return table_error("out of memory");
  }

  char *p = result;
  for (long long idx = 0; idx < count; idx++) {
    if (strings[idx]) {
      size_t len = strlen(strings[idx]);
      memcpy(p, strings[idx], len);
      p += len;
      if (idx < count - 1 && sep_len > 0) {
        memcpy(p, sep, sep_len);
        p += sep_len;
      }
    }
  }
  *p = '\0';

  for (long long k = 0; k < count; k++) free(strings[k]);
  free(strings);

  Value ret = V_str_from_c(result);
  free(result);
  return ret;
}

/* table.insert(list [, pos], value) */
static Value tbl_insert(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2 || argv[0].tag != VAL_TABLE)
    return table_error("bad argument to 'insert'");

  Table *t = argv[0].as.t;
  int n = get_array_length(t);

  long long pos;
  Value value;

  if (argc == 2) { pos = n + 1; value = argv[1]; } /* append */
  else {
    if (!to_integer(argv[1], &pos)) return table_error("bad argument #2 to 'insert' (number expected)");
    value = argv[2];
  }

  if (pos < 1) pos = 1;
  if (pos > (long long)n + 1) pos = (long long)n + 1;
  if (n >= MAXASIZE) return table_error("table overflow");

  for (int i = n; i >= (int)pos; i--) {
    Value v;
    if (tbl_get_public(t, V_int(i), &v)) tbl_set_public(t, V_int(i + 1), v);
  }
  tbl_set_public(t, V_int((int)pos), value);
  return V_nil();
}

/* table.move(a1, f, e, t [, a2]) */
static Value tbl_move(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 4) return table_error("wrong number of arguments to 'move'");
  if (argv[0].tag != VAL_TABLE) return table_error("bad argument #1 to 'move' (table expected)");

  Table *src = argv[0].as.t;
  Table *dst = src;

  long long f, e, tpos;
  if (!to_integer(argv[1], &f))    return table_error("bad argument #2 to 'move' (number expected)");
  if (!to_integer(argv[2], &e))    return table_error("bad argument #3 to 'move' (number expected)");
  if (!to_integer(argv[3], &tpos)) return table_error("bad argument #4 to 'move' (number expected)");

  if (argc >= 5) {
    if (argv[4].tag != VAL_TABLE) return table_error("bad argument #5 to 'move' (table expected)");
    dst = argv[4].as.t;
  }

  if (e < f) { Value r; r.tag = VAL_TABLE; r.as.t = dst; return r; }
  if (f < 1 || e > MAX_INT || tpos < 1 || tpos > MAX_INT) return table_error("table index out of range");
  if (e - f + 1 > MAX_INT - tpos + 1) return table_error("destination wrap around");

  long long n = e - f + 1;

  if (src == dst && tpos > f && tpos < e) {
    for (long long i = n - 1; i >= 0; i--) {
      Value v; if (!tbl_get_public(src, V_int((int)(f + i)), &v)) v = V_nil();
      tbl_set_public(dst, V_int((int)(tpos + i)), v);
    }
  } else {
    for (long long i = 0; i < n; i++) {
      Value v; if (!tbl_get_public(src, V_int((int)(f + i)), &v)) v = V_nil();
      tbl_set_public(dst, V_int((int)(tpos + i)), v);
    }
  }

  Value r; r.tag = VAL_TABLE; r.as.t = dst; return r;
}

/* table.pack(...) */
static Value tbl_pack(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  Value t = V_table();
  for (int i = 0; i < argc; i++) tbl_set_public(t.as.t, V_int(i + 1), argv[i]);
  tbl_set_public(t.as.t, V_str_from_c("n"), V_int(argc));
  return t;
}

/* table.remove(list [, pos]) */
static Value tbl_remove(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_TABLE)
    return table_error("bad argument to 'remove'");

  Table *t = argv[0].as.t;
  int n = get_array_length(t);
  if (n == 0) return V_nil();

  long long pos = n;
  if (argc >= 2 && !to_integer(argv[1], &pos)) return table_error("bad argument #2 to 'remove' (number expected)");
  if (pos < 1 || pos > n) return V_nil();

  Value removed;
  if (!tbl_get_public(t, V_int((int)pos), &removed)) removed = V_nil();

  for (long long i = pos; i < n; i++) {
    Value v;
    if (tbl_get_public(t, V_int((int)(i + 1)), &v)) tbl_set_public(t, V_int((int)i), v);
    else tbl_set_public(t, V_int((int)i), V_nil());
  }
  tbl_set_public(t, V_int(n), V_nil());
  return removed;
}

/* ---- pairs (iterator-based, array-part only for now) ---- */

typedef struct {
  Value table;  /* keep whole Value so we don't rely on internal TableEntry type */
  int   last;   /* last yielded numeric index */
} PairsState;

static const char *PAIRS_PTR = "_pairs_ptr";

static Value box_pairs_state(PairsState *ps) {
  Value t = V_table();
  Value p = { .tag = VAL_CFUNC };
  p.as.cfunc = (CFunc)ps;
  tbl_set_public(t.as.t, V_str_from_c(PAIRS_PTR), p);
  return t;
}
static PairsState* unbox_pairs_state(Value v) {
  if (v.tag != VAL_TABLE) return NULL;
  Value p;
  if (!tbl_get_public(v.as.t, V_str_from_c(PAIRS_PTR), &p)) return NULL;
  if (p.tag != VAL_CFUNC) return NULL;
  return (PairsState*)p.as.cfunc;
}

/* iterator: returns {key, value} or nil */
static Value pairs_iter(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2) return V_nil();
  PairsState *st = unbox_pairs_state(argv[0]);
  if (!st || st->table.tag != VAL_TABLE) return V_nil();

  Table *t = st->table.as.t;
  int n = get_array_length(t);

  int i = st->last + 1;
  while (i <= n) {
    Value v;
    if (tbl_get_public(t, V_int(i), &v) && v.tag != VAL_NIL) {
      st->last = i;
      Value pair = V_table();
      tbl_set_public(pair.as.t, V_int(1), V_int(i));
      tbl_set_public(pair.as.t, V_int(2), v);
      return pair;
    }
    i++;
  }
  return V_nil();
}

/* table.pairs(t) -> iterator triple */
static Value tbl_pairs(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_TABLE)
    return table_error("bad argument #1 to 'pairs' (table expected)");

  PairsState *st = (PairsState*)malloc(sizeof(PairsState));
  if (!st) return V_nil();
  st->table = argv[0];
  st->last  = 0;

  Value state = box_pairs_state(st);
  Value iter; iter.tag = VAL_CFUNC; iter.as.cfunc = pairs_iter;

  Value triple = V_table();
  tbl_set_public(triple.as.t, V_int(1), iter);
  tbl_set_public(triple.as.t, V_int(2), state);
  tbl_set_public(triple.as.t, V_int(3), V_nil());
  return triple;
}

/* ---- sort ---- */

/* Validate comparability (Lua errors on incomparable types without comparator) */
static void ensure_comparable_or_error(struct VM *vm, Value *arr, int n) {
  int has_num = 0, has_str = 0;
  for (int i = 0; i < n; i++) {
    if (arr[i].tag == VAL_NIL) continue; /* nils are allowed but will be compared only via comparator */
    if (arr[i].tag == VAL_INT || arr[i].tag == VAL_NUM) has_num = 1;
    else if (arr[i].tag == VAL_STR) has_str = 1;
    else {
      /* other types are not directly comparable in Lua's default sort */
      vm_raise(vm, V_str_from_c("attempt to compare non-numeric/non-string values (provide a comparator)"));
      return;
    }
    if (has_num && has_str) {
      vm_raise(vm, V_str_from_c("attempt to compare number with string"));
      return;
    }
  }
}

/* helper: readable typename for error messages */
static const char *typename_of(Value v) {
  switch (v.tag) {
    case VAL_NIL:   return "nil";
    case VAL_BOOL:  return "boolean";
    case VAL_INT:
    case VAL_NUM:   return "number";
    case VAL_STR:   return "string";
    case VAL_TABLE: return "table";
    case VAL_FUNC:
    case VAL_CFUNC: return "function";
    default:        return "value";
  }
}

/* default_less with strict Lua semantics */
static int default_less(struct VM *vm, Value a, Value b) {
  /* numbers */
  if ((a.tag == VAL_INT || a.tag == VAL_NUM) &&
      (b.tag == VAL_INT || b.tag == VAL_NUM)) {
    double da = (a.tag == VAL_INT) ? (double)a.as.i : a.as.n;
    double db = (b.tag == VAL_INT) ? (double)b.as.i : b.as.n;
    if (isnan(da) || isnan(db)) {
      vm_raise(vm, V_str_from_c("invalid value (NaN) to 'sort'"));
      return 0; /* never reached */
    }
    return da < db;
  }

  /* strings */
  if (a.tag == VAL_STR && b.tag == VAL_STR) {
    int min_len = (a.as.s->len < b.as.s->len) ? a.as.s->len : b.as.s->len;
    int cmp = memcmp(a.as.s->data, b.as.s->data, (size_t)min_len);
    if (cmp != 0) return cmp < 0;
    return a.as.s->len < b.as.s->len;
  }

  /* otherwise: incompatible */
  char buf[128];
  snprintf(buf, sizeof(buf), "attempt to compare %s with %s",
           typename_of(a), typename_of(b));
  vm_raise(vm, V_str_from_c(buf));
  return 0; /* never reached */
}

static void quicksort(Value *arr, int low, int high, struct VM *vm, Value comp, int has_comp) {
  if (low < high) {
    Value pivot = arr[high];
    int i = low - 1;
    for (int j = low; j < high; j++) {
      int less = 0;
      if (has_comp) {
        Value args[2] = { arr[j], pivot };
        Value res = call_any_public(vm, comp, 2, args);
        less = (res.tag == VAL_BOOL) ? res.as.b : (res.tag != VAL_NIL);
      } else {
        less = default_less(vm,arr[j], pivot);
      }
      if (less) {
        i++;
        Value tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
      }
    }
    Value tmp = arr[i + 1]; arr[i + 1] = arr[high]; arr[high] = tmp;
    int pi = i + 1;
    quicksort(arr, low,     pi - 1, vm, comp, has_comp);
    quicksort(arr, pi + 1,  high,   vm, comp, has_comp);
  }
}

/* table.sort(list [, comp]) */
static Value tbl_sort(struct VM *vm, int argc, Value *argv) {
  if (argc < 1 || argv[0].tag != VAL_TABLE)
    return table_error("bad argument to 'sort'");

  Table *t = argv[0].as.t;
  int n = get_array_length(t);
  if (n <= 1) return V_nil();

  int has_comp = 0;
  Value comp = V_nil();
  if (argc >= 2) {
    if (is_callable(argv[1])) { has_comp = 1; comp = argv[1]; }
    else if (argv[1].tag != VAL_NIL) return table_error("bad argument #2 to 'sort' (function expected)");
  }

  Value *arr = (Value*)malloc(sizeof(Value) * (size_t)n);
  if (!arr) return table_error("out of memory");

  for (int i = 0; i < n; i++) {
    if (!tbl_get_public(t, V_int(i + 1), &arr[i])) arr[i] = V_nil();
  }

  if (!has_comp) {
    /* enforce Lua error on incomparable types */
    ensure_comparable_or_error(vm, arr, n);
  }

  quicksort(arr, 0, n - 1, vm, comp, has_comp);

  for (int i = 0; i < n; i++) tbl_set_public(t, V_int(i + 1), arr[i]);

  free(arr);
  return V_nil();
}

/* table.unpack(list [, i [, j]])
   NOTE: Your VM currently supports only *single* return values.
   Lua's unpack returns multiple values, which requires VM-level multi-return.
   Until then, we return a table {1..(j-i+1)} so callers can do:
     local tmp = table.unpack(t);  -- tmp is a packed array of values
   When you add multi-return, replace this with actual multi returns. */
static Value tbl_unpack(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_TABLE)
    return table_error("bad argument to 'unpack'");

  Table *t = argv[0].as.t;

  long long i = 1;
  long long j = get_array_length(t);
  if (argc >= 2 && !to_integer(argv[1], &i)) return table_error("bad argument #2 to 'unpack' (number expected)");
  if (argc >= 3 && !to_integer(argv[2], &j)) return table_error("bad argument #3 to 'unpack' (number expected)");
  if (i < 1) i = 1;
  if (j < i) j = i - 1;

  Value result = V_table();
  long long k = 0;
  for (long long idx = i; idx <= j; idx++) {
    Value v; if (!tbl_get_public(t, V_int((int)idx), &v)) v = V_nil();
    tbl_set_public(result.as.t, V_int((int)(++k)), v);
  }
  tbl_set_public(result.as.t, V_str_from_c("n"), V_int((long long)k));
  return result;
}

/* ---- Lua 5.4 Compatibility Helpers (deprecated APIs) ---- */

static Value tbl_foreach(struct VM *vm, int argc, Value *argv) {
  if (argc < 2 || argv[0].tag != VAL_TABLE || !is_callable(argv[1]))
    return table_error("bad arguments to 'foreach'");
  Table *t = argv[0].as.t;
  Value func = argv[1];
  int n = get_array_length(t);
  for (int i = 1; i <= n; i++) {
    Value v;
    if (tbl_get_public(t, V_int(i), &v)) {
      Value args[2] = { V_int(i), v };
      Value r = call_any_public(vm, func, 2, args);
      if (r.tag != VAL_NIL) return r;
    }
  }
  return V_nil();
}

static Value tbl_foreachi(struct VM *vm, int argc, Value *argv) {
  return tbl_foreach(vm, argc, argv);
}

static Value tbl_getn(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_TABLE)
    return table_error("bad argument to 'getn'");
  return V_int(get_array_length(argv[0].as.t));
}

static Value tbl_setn(struct VM *vm, int argc, Value *argv) {
  (void)vm; (void)argc; (void)argv;
  return V_nil(); /* no-op */
}

static Value tbl_maxn(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_TABLE)
    return table_error("bad argument to 'maxn'");
  /* For simplicity, return array length (true max numeric key would scan all buckets) */
  return V_int(get_array_length(argv[0].as.t));
}

/* ---- Registration ---- */

void register_table_lib(struct VM *vm) {
  Value T = V_table();

  /* Core functions (Lua 5.1+) */
  tbl_set_public(T.as.t, V_str_from_c("concat"),   (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_concat});
  tbl_set_public(T.as.t, V_str_from_c("insert"),   (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_insert});
  tbl_set_public(T.as.t, V_str_from_c("remove"),   (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_remove});
  tbl_set_public(T.as.t, V_str_from_c("sort"),     (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_sort});
  tbl_set_public(T.as.t, V_str_from_c("pairs"),    (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_pairs});

  /* Lua 5.3+ functions */
  tbl_set_public(T.as.t, V_str_from_c("move"),     (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_move});
  tbl_set_public(T.as.t, V_str_from_c("pack"),     (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_pack});
  tbl_set_public(T.as.t, V_str_from_c("unpack"),   (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_unpack});

  /* Compatibility (deprecated) */
  tbl_set_public(T.as.t, V_str_from_c("foreach"),  (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_foreach});
  tbl_set_public(T.as.t, V_str_from_c("foreachi"), (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_foreachi});
  tbl_set_public(T.as.t, V_str_from_c("getn"),     (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_getn});
  tbl_set_public(T.as.t, V_str_from_c("setn"),     (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_setn});
  tbl_set_public(T.as.t, V_str_from_c("maxn"),     (Value){.tag=VAL_CFUNC, .as.cfunc=tbl_maxn});

  env_add_public(vm->env, "table", T, false);
}
