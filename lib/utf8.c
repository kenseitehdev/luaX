// lib/utf8.c
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../include/interpreter.h"

/* ===== small helpers ===== */

static int clamp(int x, int lo, int hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

/* Decode one UTF-8 codepoint from s[pos..len-1].
   pos is 0-based byte index, returns:
   - *out_cp = decoded scalar (or 0xFFFD on malformed)
   - return value = number of bytes consumed (>=1), never 0
   We always consume at least 1 byte to avoid infinite loops. */
static int utf8_decode_one(const char *s, int len, int pos, uint32_t *out_cp) {
  if (pos >= len) { *out_cp = 0; return 0; }
  unsigned char c0 = (unsigned char)s[pos];

  if (c0 < 0x80) { *out_cp = c0; return 1; }

  int need = 0;
  if      ((c0 & 0xE0) == 0xC0) { need = 2; }
  else if ((c0 & 0xF0) == 0xE0) { need = 3; }
  else if ((c0 & 0xF8) == 0xF0) { need = 4; }
  else { *out_cp = 0xFFFD; return 1; }

  if (pos + need > len) { *out_cp = 0xFFFD; return 1; }

  uint32_t cp = 0;
  if (need == 2) {
    unsigned char c1 = (unsigned char)s[pos+1];
    if ((c1 & 0xC0) != 0x80) { *out_cp = 0xFFFD; return 1; }
    cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
    if (cp < 0x80) { *out_cp = 0xFFFD; return 1; } // overlong
  } else if (need == 3) {
    unsigned char c1 = (unsigned char)s[pos+1];
    unsigned char c2 = (unsigned char)s[pos+2];
    if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80)) { *out_cp = 0xFFFD; return 1; }
    cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
    if (cp < 0x800) { *out_cp = 0xFFFD; return 1; } // overlong
    if (cp >= 0xD800 && cp <= 0xDFFF) { *out_cp = 0xFFFD; return 1; } // surrogate
  } else { // need == 4
    unsigned char c1 = (unsigned char)s[pos+1];
    unsigned char c2 = (unsigned char)s[pos+2];
    unsigned char c3 = (unsigned char)s[pos+3];
    if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80)) { *out_cp = 0xFFFD; return 1; }
    cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    if (cp < 0x10000 || cp > 0x10FFFF) { *out_cp = 0xFFFD; return 1; }
  }

  *out_cp = cp;
  return need;
}

/* Encode one Unicode scalar to UTF-8, write into buf (at least 4 bytes).
   Returns number of bytes written (1..4). If cp invalid, encodes U+FFFD. */
static int utf8_encode_one(uint32_t cp, char buf[4]) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;

  if (cp < 0x80) {
    buf[0] = (char)cp; return 1;
  } else if (cp < 0x800) {
    buf[0] = (char)(0xC0 | (cp >> 6));
    buf[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  } else if (cp < 0x10000) {
    buf[0] = (char)(0xE0 | (cp >> 12));
    buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  } else {
    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
  }
}

/* ===== utf8.len(s [, i [, j]]) -> integer =====
   Counts codepoints starting in byte range [i..j] (1-based, inclusive). */
static Value utf8_len(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();
  const char *s = argv[0].as.s->data;
  int len = argv[0].as.s->len;

  int i = 1, j = len;
  if (argc >= 2) {
    if (argv[1].tag == VAL_INT) i = (int)argv[1].as.i;
    else if (argv[1].tag == VAL_NUM) i = (int)argv[1].as.n;
  }
  if (argc >= 3) {
    if (argv[2].tag == VAL_INT) j = (int)argv[2].as.i;
    else if (argv[2].tag == VAL_NUM) j = (int)argv[2].as.n;
  }
  if (len == 0 || j < i) return V_int(0);

  i = clamp(i, 1, len);
  j = clamp(j, 1, len);

  int pos = i - 1;           // 0-based
  int end = j;               // inclusive end: we ensure next start <= j
  int count = 0;

  while (pos < end) {
    uint32_t cp;
    int adv = utf8_decode_one(s, len, pos, &cp);
    if (adv <= 0) break;
    count++;
    pos += adv;
  }

  return V_int(count);
}

/* ===== utf8.codepoint(s [, i [, j]]) -> integer | { ... } | nil =====
   Returns a single integer if exactly one cp in range; a table if multiple; nil if none. */
static Value utf8_codepoint(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();
  const char *s = argv[0].as.s->data;
  int len = argv[0].as.s->len;

  int i = 1, j = len;
  if (argc >= 2) {
    if (argv[1].tag == VAL_INT) i = (int)argv[1].as.i;
    else if (argv[1].tag == VAL_NUM) i = (int)argv[1].as.n;
  }
  if (argc >= 3) {
    if (argv[2].tag == VAL_INT) j = (int)argv[2].as.i;
    else if (argv[2].tag == VAL_NUM) j = (int)argv[2].as.n;
  }
  if (len == 0 || j < i) return V_nil();

  i = clamp(i, 1, len);
  j = clamp(j, 1, len);

  int pos = i - 1;
  int end = j;

  long long first_cp = -1;
  int count = 0;

  /* First pass: count and remember the first cp (to avoid double work if 1) */
  int scan = pos;
  while (scan < end) {
    uint32_t cp;
    int adv = utf8_decode_one(s, len, scan, &cp);
    if (adv <= 0) break;
    if (scan + adv - 1 > end) break; /* would end after j */
    if (count == 0) first_cp = (long long)cp;
    count++;
    scan += adv;
    if (count > 1) break; /* we only need to know if there are more than one */
  }

  if (count == 0) return V_nil();
  if (count == 1) return V_int(first_cp);

  /* Multiple: build the full table in a second pass */
  Value out = V_table();
  int k = 1;
  while (pos < end) {
    uint32_t cp;
    int adv = utf8_decode_one(s, len, pos, &cp);
    if (adv <= 0) break;
    if (pos + adv - 1 > end) break;
    tbl_set_public(out.as.t, V_int(k++), V_int((long long)cp));
    pos += adv;
  }
  return out;
}

/* ===== utf8.char(...codepoints) -> string ===== */
static Value utf8_char(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc <= 0) return V_str_from_c("");

  int cap = argc * 4;
  char *buf = (char*)malloc((size_t)cap + 1);
  if (!buf) return V_nil();
  int n = 0;

  for (int i = 0; i < argc; ++i) {
    uint32_t cp = 0xFFFD;
    if (argv[i].tag == VAL_INT) cp = (uint32_t)argv[i].as.i;
    else if (argv[i].tag == VAL_NUM) cp = (uint32_t)argv[i].as.n;

    char tmp[4];
    int w = utf8_encode_one(cp, tmp);
    if (n + w > cap) {
      cap = cap * 2 + 4;
      char *nb = (char*)realloc(buf, (size_t)cap + 1);
      if (!nb) { free(buf); return V_nil(); }
      buf = nb;
    }
    memcpy(buf + n, tmp, (size_t)w);
    n += w;
  }
  buf[n] = '\0';
  Value v = V_str_from_c(buf);
  free(buf);
  return v;
}

/* ===== Iterator for utf8.codes(s) ===== */
static Value utf8_codes_iter(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2) return V_nil();
  Value state = argv[0];
  Value ctrl  = argv[1];
  if (state.tag != VAL_TABLE) return V_nil();

  Value sV, lenV;
  if (!tbl_get_public(state.as.t, V_str_from_c("s"), &sV) || sV.tag != VAL_STR) return V_nil();
  if (!tbl_get_public(state.as.t, V_str_from_c("len"), &lenV) || (lenV.tag != VAL_INT && lenV.tag != VAL_NUM)) return V_nil();

  const char *s = sV.as.s->data;
  int len = (lenV.tag == VAL_INT) ? (int)lenV.as.i : (int)lenV.as.n;

  int last = 0;
  if (ctrl.tag == VAL_INT) last = (int)ctrl.as.i;
  else if (ctrl.tag == VAL_NUM) last = (int)ctrl.as.n;

  int pos = last;                       // 0-based
  if (pos < 0) pos = 0;
  if (pos >= len) return V_nil();

  uint32_t cp;
  int adv = utf8_decode_one(s, len, pos, &cp);
  if (adv <= 0) return V_nil();

  int start_index = pos + 1;            // 1-based for Lua-side
  pos += adv;

  Value pair = V_table();
  tbl_set_public(pair.as.t, V_int(1), V_int((long long)start_index));
  tbl_set_public(pair.as.t, V_int(2), V_int((long long)cp));
  return pair;
}

static Value utf8_codes(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();

  Value state = V_table();
  tbl_set_public(state.as.t, V_str_from_c("s"), argv[0]);
  tbl_set_public(state.as.t, V_str_from_c("len"), V_int((long long)argv[0].as.s->len));

  Value iter; iter.tag = VAL_CFUNC; iter.as.cfunc = utf8_codes_iter;

  Value triple = V_table();
  tbl_set_public(triple.as.t, V_int(1), iter);
  tbl_set_public(triple.as.t, V_int(2), state);
  tbl_set_public(triple.as.t, V_int(3), V_int(0));  // ctrl = 0 (internal 0-based)
  return triple;
}

/* ===== utf8.offset(s, n [, i]) -> integer|nil ===== */
static Value utf8_offset(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2 || argv[0].tag != VAL_STR) return V_nil();
  const char *s = argv[0].as.s->data;
  int len = argv[0].as.s->len;

  long long n = 0;
  if (argv[1].tag == VAL_INT) n = argv[1].as.i;
  else if (argv[1].tag == VAL_NUM) n = (long long)argv[1].as.n;
  else return V_nil();

  int i = 1;
  if (argc >= 3) {
    if (argv[2].tag == VAL_INT) i = (int)argv[2].as.i;
    else if (argv[2].tag == VAL_NUM) i = (int)argv[2].as.n;
  }

  if (len == 0) return V_nil();
  i = clamp(i, 1, len);

  if (n == 0) {
    int pos = i - 1;
    if (pos >= len) pos = len - 1;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) pos--;
    return V_int((long long)pos + 1);
  }

  if (n > 0) {
    int pos = i - 1;
    int count = 0;
    while (pos < len) {
      if (++count == n) return V_int((long long)pos + 1);
      uint32_t cp;
      int adv = utf8_decode_one(s, len, pos, &cp);
      if (adv <= 0) break;
      pos += adv;
    }
    return V_nil();
  } else { // n < 0
    int pos = i - 1;
    if (pos >= len) pos = len - 1;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) pos--;

    int want = (int)(-n);
    int cap = 16, cnt = 0;
    int *starts = (int*)malloc((size_t)cap * sizeof(int));
    if (!starts) return V_nil();

    int p = 0;
    while (p < len) {
      int start = p;
      uint32_t cp;
      int adv = utf8_decode_one(s, len, p, &cp);
      if (adv <= 0) adv = 1;
      if (start <= pos) {
        if (cnt == cap) {
          cap *= 2;
          int *nb = (int*)realloc(starts, (size_t)cap * sizeof(int));
          if (!nb) { free(starts); return V_nil(); }
          starts = nb;
        }
        starts[cnt++] = start;
      }
      p += adv;
    }
    Value out = V_nil();
    int idx = cnt - want - 1; /* want=1 => previous start */
    if (idx >= 0 && idx < cnt) out = V_int((long long)starts[idx] + 1);
    free(starts);
    return out;
  }
}

/* ===== register into _G ===== */
void register_utf8_lib(struct VM *vm) {
  Value U = V_table();
  tbl_set_public(U.as.t, V_str_from_c("len"),       (Value){.tag=VAL_CFUNC, .as.cfunc=utf8_len});
  tbl_set_public(U.as.t, V_str_from_c("codepoint"), (Value){.tag=VAL_CFUNC, .as.cfunc=utf8_codepoint});
  tbl_set_public(U.as.t, V_str_from_c("char"),      (Value){.tag=VAL_CFUNC, .as.cfunc=utf8_char});
  tbl_set_public(U.as.t, V_str_from_c("codes"),     (Value){.tag=VAL_CFUNC, .as.cfunc=utf8_codes});
  tbl_set_public(U.as.t, V_str_from_c("offset"),    (Value){.tag=VAL_CFUNC, .as.cfunc=utf8_offset});

  /* Lua 5.3+ provides a ready-to-use pattern for one UTF-8 sequence. */
  tbl_set_public(U.as.t, V_str_from_c("charpattern"),
                 V_str_from_c("[\x00-\x7F\xC2-\xF4][\x80-\xBF]*"));

  env_add_public(vm->env, "utf8", U, false);
}
