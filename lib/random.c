
// lib/random.c
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "../include/interpreter.h"

/* ========= PRNG: xorshift64* ========= */
static uint64_t rng_state = 0x9e3779b97f4a7c15ULL; /* default nonzero seed */

static inline uint64_t xs64star_next_u64(void) {
  uint64_t x = rng_state;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  rng_state = x;
  return x * 2685821657736338717ULL;
}
static inline uint32_t xs64star_next_u32(void) {
  return (uint32_t)(xs64star_next_u64() >> 32);
}
static inline double xs64star_next_unit_double(void) {
  /* 53-bit mantissa / 2^53 => [0,1) */
  return (xs64star_next_u64() >> 11) * (1.0 / 9007199254740992.0);
}

/* ========= helpers ========= */
static int to_int(Value v, long long *out) {
  if (v.tag == VAL_INT) { *out = v.as.i; return 1; }
  if (v.tag == VAL_NUM) { *out = (long long)v.as.n; return 1; }
  return 0;
}
static int is_array_like(Table *t, long long *out_len) {
  long long n = 0, i = 1;
  Value tmp;
  while (tbl_get_public(t, V_int(i), &tmp)) { n++; i++; }
  if (out_len) *out_len = n;
  return 1;
}
static void swap_table_ix(Table *t, long long i, long long j) {
  if (i == j) return;
  Value vi, vj;
  int hi = tbl_get_public(t, V_int(i), &vi);
  int hj = tbl_get_public(t, V_int(j), &vj);
  if (hi && hj) {
    tbl_set_public(t, V_int(i), vj);
    tbl_set_public(t, V_int(j), vi);
  } else if (hi && !hj) {
    tbl_set_public(t, V_int(j), vi);
    tbl_set_public(t, V_int(i), V_nil());
  } else if (!hi && hj) {
    tbl_set_public(t, V_int(i), vj);
    tbl_set_public(t, V_int(j), V_nil());
  }
}

/* ========= API =========
random.seed([x])                 -> nil
random.random()                  -> number in [0,1)
random.random(n)                 -> integer in [1,n]
random.random(a, b)              -> integer in [a,b]
random.float()                   -> number in [0,1)
random.int(a, b)                 -> integer in [a,b]
random.choice(t)                 -> one element from array part of table t
random.shuffle(t)                -> in-place Fisher–Yates; returns t
================================ */

/* seed PRNG (optional arg) */
static Value rnd_seed(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc >= 1) {
    long long s;
    if (to_int(argv[0], &s)) {
      uint64_t z = (uint64_t)s;
      if (z == 0) z = 0x9e3779b97f4a7c15ULL;
      rng_state = z;
      /* scramble a bit */
      for (int i = 0; i < 4; ++i) xs64star_next_u64();
      return V_nil();
    }
  }
  /* fallback: time + address noise */
  uint64_t z = (uint64_t)time(NULL) ^ (uintptr_t)&vm ^ (uintptr_t)&rng_state;
  if (z == 0) z = 0x9e3779b97f4a7c15ULL;
  rng_state = z;
  for (int i = 0; i < 4; ++i) xs64star_next_u64();
  return V_nil();
}

/* Lua-like random: (), (n), (a,b) */
static Value rnd_random(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc == 0) {
    return V_num(xs64star_next_unit_double());
  } else if (argc == 1) {
    long long n;
    if (!to_int(argv[0], &n) || n < 1) return V_nil();
    /* uniform 1..n */
    uint64_t r = xs64star_next_u64();
    long long v = (long long)((r % (uint64_t)n) + 1);
    return V_int(v);
  } else {
    long long a, b;
    if (!to_int(argv[0], &a) || !to_int(argv[1], &b)) return V_nil();
    if (a > b) { long long t=a; a=b; b=t; }
    uint64_t span = (uint64_t)(b - a + 1);
    uint64_t r = xs64star_next_u64();
    long long v = a + (long long)(r % span);
    return V_int(v);
  }
}

/* explicit float() */
static Value rnd_float(struct VM *vm, int argc, Value *argv) {
  (void)vm;(void)argc;(void)argv;
  return V_num(xs64star_next_unit_double());
}
static Value rnd_int(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2) return V_nil();
  long long a,b;
  if (!to_int(argv[0], &a) || !to_int(argv[1], &b)) return V_nil();
  if (a > b) { long long t=a; a=b; b=t; }
  uint64_t span = (uint64_t)(b - a + 1);
  uint64_t r = xs64star_next_u64();
  long long v = a + (long long)(r % span);
  return V_int(v);
}

/* choice(table) from array part */
static Value rnd_choice(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_TABLE) return V_nil();
  long long n = 0;
  is_array_like(argv[0].as.t, &n);
  if (n <= 0) return V_nil();
  long long idx = (long long)((xs64star_next_u64() % (uint64_t)n) + 1);
  Value v;
  if (tbl_get_public(argv[0].as.t, V_int(idx), &v)) return v;
  return V_nil();
}

/* shuffle(table) in-place; returns table */
static Value rnd_shuffle(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_TABLE) return V_nil();
  Table *t = argv[0].as.t;
  long long n = 0;
  is_array_like(t, &n);
  for (long long i = n; i >= 2; --i) {
    long long j = (long long)((xs64star_next_u64() % (uint64_t)i) + 1);
    swap_table_ix(t, i, j);
  }
  return argv[0];
}

/* registration */
void register_random_lib(struct VM *vm) {
  Value R = V_table();
  tbl_set_public(R.as.t, V_str_from_c("seed"),
                 (Value){.tag=VAL_CFUNC, .as.cfunc=rnd_seed});
  tbl_set_public(R.as.t, V_str_from_c("random"),
                 (Value){.tag=VAL_CFUNC, .as.cfunc=rnd_random});
  tbl_set_public(R.as.t, V_str_from_c("float"),
                 (Value){.tag=VAL_CFUNC, .as.cfunc=rnd_float});
  tbl_set_public(R.as.t, V_str_from_c("int"),
                 (Value){.tag=VAL_CFUNC, .as.cfunc=rnd_int});
  tbl_set_public(R.as.t, V_str_from_c("choice"),
                 (Value){.tag=VAL_CFUNC, .as.cfunc=rnd_choice});
  tbl_set_public(R.as.t, V_str_from_c("shuffle"),
                 (Value){.tag=VAL_CFUNC, .as.cfunc=rnd_shuffle});

  env_add_public(vm->env, "random", R, false);
}
