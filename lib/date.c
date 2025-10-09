
// lib/date.c
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "../include/interpreter.h"

/* ---------- helpers ---------- */

static int to_ll(Value v, long long *out) {
  if (v.tag == VAL_INT)  { *out = v.as.i; return 1; }
  if (v.tag == VAL_NUM)  { *out = (long long)v.as.n; return 1; }
  return 0;
}

static int tbl_get_int(Table *t, const char *k, long long *out) {
  Value v;
  if (tbl_get_public(t, V_str_from_c(k), &v)) return to_ll(v, out);
  return 0;
}

static Value tm_to_table(const struct tm *tmv) {
  Value t = V_table();
  tbl_set_public(t.as.t, V_str_from_c("year"),  V_int(tmv->tm_year + 1900));
  tbl_set_public(t.as.t, V_str_from_c("month"), V_int(tmv->tm_mon + 1));
  tbl_set_public(t.as.t, V_str_from_c("day"),   V_int(tmv->tm_mday));
  tbl_set_public(t.as.t, V_str_from_c("hour"),  V_int(tmv->tm_hour));
  tbl_set_public(t.as.t, V_str_from_c("min"),   V_int(tmv->tm_min));
  tbl_set_public(t.as.t, V_str_from_c("sec"),   V_int(tmv->tm_sec));
  tbl_set_public(t.as.t, V_str_from_c("wday"),  V_int(tmv->tm_wday + 1)); /* 1..7 */
  tbl_set_public(t.as.t, V_str_from_c("yday"),  V_int(tmv->tm_yday + 1)); /* 1..366 */
  tbl_set_public(t.as.t, V_str_from_c("isdst"), V_int(tmv->tm_isdst > 0 ? 1 : 0));
  return t;
}

static int table_to_tm(Table *t, struct tm *out_tm) {
  memset(out_tm, 0, sizeof(*out_tm));
  long long year=0, month=0, day=0;
  if (!tbl_get_int(t, "year", &year))  return 0;
  if (!tbl_get_int(t, "month",&month)) return 0;
  if (!tbl_get_int(t, "day",  &day))   return 0;

  long long hour=12, min=0, sec=0, isdst_ll= -1;
  (void)tbl_get_int(t, "hour",  &hour);
  (void)tbl_get_int(t, "min",   &min);
  (void)tbl_get_int(t, "sec",   &sec);
  if (tbl_get_int(t, "isdst", &isdst_ll)) {
    if (isdst_ll < 0) isdst_ll = -1;
    if (isdst_ll > 0) isdst_ll = 1;
  } else {
    isdst_ll = -1; /* unknown */
  }

  out_tm->tm_year = (int)(year - 1900);
  out_tm->tm_mon  = (int)(month - 1);
  out_tm->tm_mday = (int)day;
  out_tm->tm_hour = (int)hour;
  out_tm->tm_min  = (int)min;
  out_tm->tm_sec  = (int)sec;
  out_tm->tm_isdst= (int)isdst_ll;
  return 1;
}

static Value date_now(struct VM *vm, int argc, Value *argv) {
  (void)vm; (void)argc; (void)argv;
  time_t t = time(NULL);
  return V_num((double)t);   /* <-- use double to avoid BigInt */
}

/* ---------- date.time([t]) ---------- */
static Value date_time(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc == 0 || argv[0].tag == VAL_NIL) {
    return date_now(vm, 0, NULL);
  }
  if (argv[0].tag != VAL_TABLE) return V_nil();

  struct tm tmv;
  if (!table_to_tm(argv[0].as.t, &tmv)) return V_nil();

  /* mktime interprets tm as local time */
  time_t tt = mktime(&tmv);
  if (tt == (time_t)-1) return V_nil();
  return V_int((long long)tt);
}

/* ---------- date.localtime([t]) ---------- */
static Value date_localtime(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  time_t tt;
  if (argc >= 1 && (argv[0].tag == VAL_INT || argv[0].tag == VAL_NUM)) {
    long long x; if (!to_ll(argv[0], &x)) return V_nil();
    tt = (time_t)x;
  } else {
    tt = time(NULL);
  }
  struct tm tmv;
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || defined(__unix__) || defined(__APPLE__)
  localtime_r(&tt, &tmv);
#else
  struct tm *p = localtime(&tt);
  if (!p) return V_nil();
  tmv = *p;
#endif
  return tm_to_table(&tmv);
}

/* ---------- date.gmtime([t]) ---------- */
static Value date_gmtime(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  time_t tt;
  if (argc >= 1 && (argv[0].tag == VAL_INT || argv[0].tag == VAL_NUM)) {
    long long x; if (!to_ll(argv[0], &x)) return V_nil();
    tt = (time_t)x;
  } else {
    tt = time(NULL);
  }
  struct tm tmv;
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || defined(__unix__) || defined(__APPLE__)
  gmtime_r(&tt, &tmv);
#else
  struct tm *p = gmtime(&tt);
  if (!p) return V_nil();
  tmv = *p;
#endif
  return tm_to_table(&tmv);
}

/* ---------- date.format(fmt[, t]) ----------
 * If fmt == "*t"  -> localtime table
 * If fmt == "!*t" -> gmtime table
 * If fmt starts with '!' -> use gmtime for strftime
 * Otherwise localtime strftime(fmt, time)
 */
static Value date_format(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();
  const char *fmt = argv[0].as.s->data;

  /* pick time */
  time_t tt;
  if (argc >= 2 && (argv[1].tag == VAL_INT || argv[1].tag == VAL_NUM)) {
    long long x; if (!to_ll(argv[1], &x)) return V_nil();
    tt = (time_t)x;
  } else {
    tt = time(NULL);
  }

  if (strcmp(fmt, "*t") == 0) {
    Value t = date_localtime(vm, (argc>=2)?1:0, (argc>=2)?&argv[1]:NULL);
    return t;
  }
  if (strcmp(fmt, "!*t") == 0) {
    Value t = date_gmtime(vm, (argc>=2)?1:0, (argc>=2)?&argv[1]:NULL);
    return t;
  }

  int use_gmt = (fmt[0] == '!');
  const char *real_fmt = use_gmt ? fmt + 1 : fmt;

  struct tm tmv;
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || defined(__unix__) || defined(__APPLE__)
  if (use_gmt) gmtime_r(&tt, &tmv); else localtime_r(&tt, &tmv);
#else
  struct tm *p = use_gmt ? gmtime(&tt) : localtime(&tt);
  if (!p) return V_nil();
  tmv = *p;
#endif

  char buf[256];
  size_t n = strftime(buf, sizeof(buf), real_fmt, &tmv);
  if (n == 0) return V_str_from_c("");
  return V_str_from_c(buf);
}

/* ---------- date.iso8601([t]) -> "YYYY-MM-DDTHH:MM:SSZ" (UTC) ---------- */
static Value date_iso8601(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  time_t tt;
  if (argc >= 1 && (argv[0].tag == VAL_INT || argv[0].tag == VAL_NUM)) {
    long long x; if (!to_ll(argv[0], &x)) return V_nil();
    tt = (time_t)x;
  } else {
    tt = time(NULL);
  }
  struct tm tmv;
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || defined(__unix__) || defined(__APPLE__)
  gmtime_r(&tt, &tmv);
#else
  struct tm *p = gmtime(&tt);
  if (!p) return V_nil();
  tmv = *p;
#endif
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return V_str_from_c(buf);
}

/* ---------- simple arithmetic ---------- */
static Value date_diff(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2) return V_nil();
  long long t2, t1;
  if (!to_ll(argv[0], &t2) || !to_ll(argv[1], &t1)) return V_nil();
  return V_int(t2 - t1);
}
static Value date_add(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2) return V_nil();
  long long t, s;
  if (!to_ll(argv[0], &t) || !to_ll(argv[1], &s)) return V_nil();
  return V_int(t + s);
}

/* ---------- registration ---------- */
void register_date_lib(struct VM *vm) {
  Value D = V_table();
  tbl_set_public(D.as.t, V_str_from_c("now"),      (Value){.tag=VAL_CFUNC, .as.cfunc=date_now});
  tbl_set_public(D.as.t, V_str_from_c("time"),     (Value){.tag=VAL_CFUNC, .as.cfunc=date_time});
  tbl_set_public(D.as.t, V_str_from_c("localtime"),(Value){.tag=VAL_CFUNC, .as.cfunc=date_localtime});
  tbl_set_public(D.as.t, V_str_from_c("gmtime"),   (Value){.tag=VAL_CFUNC, .as.cfunc=date_gmtime});
  tbl_set_public(D.as.t, V_str_from_c("format"),   (Value){.tag=VAL_CFUNC, .as.cfunc=date_format});
  tbl_set_public(D.as.t, V_str_from_c("iso8601"),  (Value){.tag=VAL_CFUNC, .as.cfunc=date_iso8601});
  tbl_set_public(D.as.t, V_str_from_c("diff"),     (Value){.tag=VAL_CFUNC, .as.cfunc=date_diff});
  tbl_set_public(D.as.t, V_str_from_c("add"),      (Value){.tag=VAL_CFUNC, .as.cfunc=date_add});
  env_add_public(vm->env, "date", D, false);
}
