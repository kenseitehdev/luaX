// lib/os.c
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <locale.h>

#if defined(_WIN32)
  #include <io.h>
  #include <fcntl.h>
  #include <windows.h>
  #include <process.h>
  #include <direct.h>
  #include <sys/types.h>
  #include <sys/stat.h>
  #define PATH_MAX 260
#else
  #include <unistd.h>
  #include <limits.h>
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <sys/wait.h>
#endif

#include "../include/interpreter.h"

/* -------------------------------------------------------
 * small helpers
 * ------------------------------------------------------- */

static int get_table_int(Table *t, const char *key, int *out) {
  Value v;
  if (!tbl_get_public(t, V_str_from_c(key), &v)) return 0;
  if (v.tag == VAL_INT) { *out = (int)v.as.i; return 1; }
  if (v.tag == VAL_NUM) { *out = (int)v.as.n; return 1; }
  return 0;
}
static int get_table_bool(Table *t, const char *key, int *out) {
  Value v;
  if (!tbl_get_public(t, V_str_from_c(key), &v)) return 0;
  if (v.tag == VAL_BOOL) { *out = v.as.b ? 1 : 0; return 1; }
  return 0;
}

static Value tuple3(Value a, Value b, Value c){
  Value t = V_table();
  tbl_set_public(t.as.t, V_int(1), a);
  tbl_set_public(t.as.t, V_int(2), b);
  tbl_set_public(t.as.t, V_int(3), c);
  return t;
}
static Value tuple2(Value a, Value b){
  Value t = V_table();
  tbl_set_public(t.as.t, V_int(1), a);
  tbl_set_public(t.as.t, V_int(2), b);
  return t;
}

/* -------------------------------------------------------
 * os.time([t])
 * ------------------------------------------------------- */
/* If t is omitted -> current time (seconds since epoch).
   If t is a table {year, month, day[, hour, min, sec, isdst]}, uses local time via mktime. */
static Value os_time(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag == VAL_NIL) {
    time_t now = time(NULL);
    return V_int((long long)now);
  }
  if (argv[0].tag != VAL_TABLE) {
    if (argv[0].tag == VAL_INT) return argv[0];
    if (argv[0].tag == VAL_NUM) return V_int((long long)argv[0].as.n);
    return V_nil();
  }

  struct tm tmv;
  memset(&tmv, 0, sizeof(tmv));

  int year=0, mon=0, mday=0;
  if (!get_table_int(argv[0].as.t, "year", &year)) return V_nil();
  if (!get_table_int(argv[0].as.t, "month", &mon)) return V_nil();
  if (!get_table_int(argv[0].as.t, "day", &mday))  return V_nil();

  tmv.tm_year = year - 1900;
  tmv.tm_mon  = mon - 1;
  tmv.tm_mday = mday;

  int hour=12, min=0, sec=0, isdst_flag= -1;
  get_table_int(argv[0].as.t, "hour", &hour);
  get_table_int(argv[0].as.t, "min",  &min);
  get_table_int(argv[0].as.t, "sec",  &sec);
  {
    int b;
    if (get_table_bool(argv[0].as.t, "isdst", &b)) isdst_flag = b ? 1 : 0;
  }

  tmv.tm_hour = hour;
  tmv.tm_min  = min;
  tmv.tm_sec  = sec;
  tmv.tm_isdst = isdst_flag;

  time_t tt = mktime(&tmv); /* local time */
  if (tt == (time_t)-1) return V_nil();
  return V_int((long long)tt);
}

static Value os_chdir(struct VM *vm, int argc, Value *argv) {
    (void)vm;
    if (argc < 1 || argv[0].tag != VAL_STR)
        return V_bool(0);  // <-- returns a proper Value false

    int rc = chdir(argv[0].as.s->data);
    return V_bool(rc == 0);  // <-- wrap int result in V_bool()
}

static Value os_getcwd(struct VM *vm, int argc, Value *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf)))
        return V_nil();
    return V_str_from_c(buf);
}
/* -------------------------------------------------------
 * os.difftime(t2, t1) -> seconds (double)
 * ------------------------------------------------------- */
static Value os_difftime(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2) return V_num(0.0);
  double t2 = 0.0, t1 = 0.0;
  if (argv[0].tag == VAL_INT) t2 = (double)argv[0].as.i; else if (argv[0].tag == VAL_NUM) t2 = argv[0].as.n;
  if (argv[1].tag == VAL_INT) t1 = (double)argv[1].as.i; else if (argv[1].tag == VAL_NUM) t1 = argv[1].as.n;
  return V_num(t2 - t1);
}

/* -------------------------------------------------------
 * os.clock() -> CPU seconds (double)
 * ------------------------------------------------------- */
static Value os_clock(struct VM *vm, int argc, Value *argv) {
  (void)vm; (void)argc; (void)argv;
  clock_t c = clock();
  if (c == (clock_t)-1) return V_num(0.0);
  return V_num((double)c / (double)CLOCKS_PER_SEC);
}

/* -------------------------------------------------------
 * os.getenv(var)
 * ------------------------------------------------------- */
static Value os_getenv(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();
  const char *name = argv[0].as.s->data;
  const char *val = getenv(name);
  if (!val) return V_nil();
  return V_str_from_c(val);
}

/* -------------------------------------------------------
 * os.execute([cmd])
 * Lua 5.4 semantics (adapted to tuple-table):
 *  - no arg: returns {true} if a shell is available, else {false}
 *  - success (normal exit): {true, "exit", code}
 *  - signaled (POSIX): {nil, "signal", signo}
 *  - failure to run: {nil, "execute failed", errno}
 * ------------------------------------------------------- */
static Value os_execute(struct VM *vm, int argc, Value *argv) {
  (void)vm;

  if (argc < 1 || argv[0].tag == VAL_NIL) {
    int ok = system(NULL);
    Value t = V_table();
    tbl_set_public(t.as.t, V_int(1), V_bool(ok ? 1 : 0));
    return t;
  }
  if (argv[0].tag != VAL_STR) return tuple2(V_nil(), V_str_from_c("invalid command"));

  errno = 0;
  int st = system(argv[0].as.s->data);

#if defined(_WIN32)
  if (st == -1) {
    return tuple3(V_nil(), V_str_from_c("execute failed"), V_int(errno));
  }
  /* On Windows, system returns exit code (implementation-defined).
     We treat >=0 as normal exit. */
  return tuple3(V_bool(1), V_str_from_c("exit"), V_int((long long)st));
#else
  if (st == -1) {
    return tuple3(V_nil(), V_str_from_c("execute failed"), V_int(errno));
  }
  if (WIFEXITED(st)) {
    int code = WEXITSTATUS(st);
    return tuple3(V_bool(1), V_str_from_c("exit"), V_int(code));
  }
  if (WIFSIGNALED(st)) {
    int sig = WTERMSIG(st);
    return tuple3(V_nil(), V_str_from_c("signal"), V_int(sig));
  }
  /* Fallback: unknown status */
  return tuple3(V_nil(), V_str_from_c("unknown"), V_int(st));
#endif
}

/* -------------------------------------------------------
 * os.exit([code])
 * ------------------------------------------------------- */
static Value os_exit(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  int code = 0;
  if (argc >= 1) {
    if (argv[0].tag == VAL_INT) code = (int)argv[0].as.i;
    else if (argv[0].tag == VAL_NUM) code = (int)argv[0].as.n;
  }
  fflush(NULL);
  exit(code);
  return V_nil();
}

/* -------------------------------------------------------
 * os.remove(path)
 * Success: {true}
 * Failure: {nil, errmsg, errno}
 * ------------------------------------------------------- */
static Value os_remove(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return tuple2(V_nil(), V_str_from_c("invalid path"));
  errno = 0;
  int rc = remove(argv[0].as.s->data);
  if (rc == 0) {
    Value t = V_table();
    tbl_set_public(t.as.t, V_int(1), V_bool(1));
    return t;
  }
  return tuple3(V_nil(), V_str_from_c(strerror(errno)), V_int(errno));
}

/* -------------------------------------------------------
 * os.rename(old, new)
 * Success: {true}
 * Failure: {nil, errmsg, errno}
 * ------------------------------------------------------- */
static Value os_rename(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2 || argv[0].tag != VAL_STR || argv[1].tag != VAL_STR)
    return tuple2(V_nil(), V_str_from_c("invalid arguments"));
  errno = 0;
  int rc = rename(argv[0].as.s->data, argv[1].as.s->data);
  if (rc == 0) {
    Value t = V_table();
    tbl_set_public(t.as.t, V_int(1), V_bool(1));
    return t;
  }
  return tuple3(V_nil(), V_str_from_c(strerror(errno)), V_int(errno));
}

/* -------------------------------------------------------
 * os.setlocale(locale [, category])
 * Categories: "all","collate","ctype","monetary","numeric","time"
 * Returns: string (new locale name) or nil on failure
 * ------------------------------------------------------- */
static int map_category(const char *s){
  if (!s || !*s) return LC_ALL;
  if (strcmp(s,"all")==0) return LC_ALL;
  if (strcmp(s,"collate")==0) return LC_COLLATE;
  if (strcmp(s,"ctype")==0) return LC_CTYPE;
#if defined(LC_MESSAGES)
  /* Lua does not expose messages; ignore */
#endif
  if (strcmp(s,"monetary")==0) return LC_MONETARY;
  if (strcmp(s,"numeric")==0)  return LC_NUMERIC;
  if (strcmp(s,"time")==0)     return LC_TIME;
  return LC_ALL;
}

static Value os_setlocale(struct VM *vm, int argc, Value *argv){
  (void)vm;
  const char *loc = NULL;
  const char *catname = "all";
  if (argc >= 1 && argv[0].tag != VAL_NIL) {
    if (argv[0].tag != VAL_STR) return V_nil();
    loc = argv[0].as.s->data;
  }
  if (argc >= 2 && argv[1].tag == VAL_STR) catname = argv[1].as.s->data;

  int cat = map_category(catname);
  char *res = setlocale(cat, loc);
  if (!res) return V_nil();
  return V_str_from_c(res);
}

/* -------------------------------------------------------
 * os.tmpname()
 * Best-effort & safer-ish:
 *  - POSIX: mkstemp on /tmp/luax_XXXXXX, then close+unlink, return the name
 *  - Windows: tmpnam as fallback
 * ------------------------------------------------------- */
static Value os_tmpname(struct VM *vm, int argc, Value *argv) {
  (void)vm; (void)argc; (void)argv;
#if !defined(_WIN32)
  char templ[] = "/tmp/luax_XXXXXX";
  int fd = mkstemp(templ);
  if (fd >= 0) {
    close(fd);
    unlink(templ);
    return V_str_from_c(templ);
  }
  /* fallback */
  {
    char *tn = tmpnam(NULL);
    if (!tn) return V_nil();
    return V_str_from_c(tn);
  }
#else
  char buf[L_tmpnam];
  if (tmpnam(buf) == NULL) return V_nil();
  return V_str_from_c(buf);
#endif
}

/* -------------------------------------------------------
 * os.date([format [, time]])
 *  - format nil        -> "%c"
 *  - format "*t"       -> table of fields (localtime)
 *  - format "!*t"      -> table (UTC)
 *  - format starting '!' -> UTC formatting via strftime
 *  - otherwise strftime(format, time)
 * Fields when "*t"/"!*t":
 *   year, month, day, hour, min, sec, wday(1..7), yday(1..366), isdst(boolean)
 * ------------------------------------------------------- */
static void tm_to_table(struct tm *tp, Value *out_tbl){
  Value t = V_table();
  tbl_set_public(t.as.t, V_str_from_c("year"),  V_int(tp->tm_year + 1900));
  tbl_set_public(t.as.t, V_str_from_c("month"), V_int(tp->tm_mon + 1));
  tbl_set_public(t.as.t, V_str_from_c("day"),   V_int(tp->tm_mday));
  tbl_set_public(t.as.t, V_str_from_c("hour"),  V_int(tp->tm_hour));
  tbl_set_public(t.as.t, V_str_from_c("min"),   V_int(tp->tm_min));
  tbl_set_public(t.as.t, V_str_from_c("sec"),   V_int(tp->tm_sec));
  tbl_set_public(t.as.t, V_str_from_c("wday"),  V_int(tp->tm_wday == 0 ? 7 : tp->tm_wday)); /* Lua: 1=Sunday; C: 0=Sunday */
  tbl_set_public(t.as.t, V_str_from_c("yday"),  V_int(tp->tm_yday + 1));
  tbl_set_public(t.as.t, V_str_from_c("isdst"), V_bool(tp->tm_isdst > 0));
  *out_tbl = t;
}

static Value os_date(struct VM *vm, int argc, Value *argv){
  (void)vm;
  const char *fmt = "%c";
  time_t tt = time(NULL);

  if (argc >= 1 && argv[0].tag != VAL_NIL) {
    if (argv[0].tag != VAL_STR) return V_nil();
    fmt = argv[0].as.s->data;
  }
  if (argc >= 2) {
    if (argv[1].tag == VAL_INT) tt = (time_t)argv[1].as.i;
    else if (argv[1].tag == VAL_NUM) tt = (time_t)argv[1].as.n;
  }

  int use_utc = 0;
  if (fmt[0] == '!') { use_utc = 1; fmt++; } /* "!format" */

  if (strcmp(fmt, "*t") == 0) {
    struct tm tmbuf;
    struct tm *tp = use_utc ? gmtime_r(&tt, &tmbuf) : localtime_r(&tt, &tmbuf);
#if defined(_WIN32)
    /* Windows lacks *_r; emulate with non-thread-safe versions (acceptable for this embedding). */
    struct tm *tmp = use_utc ? gmtime(&tt) : localtime(&tt);
    if (!tmp) return V_nil();
    tmbuf = *tmp;
    tp = &tmbuf;
#endif
    if (!tp) return V_nil();
    Value tbl; tm_to_table(tp, &tbl);
    return tbl;
  }

  /* strftime path */
  {
    size_t cap = 128;
    char *buf = (char*)malloc(cap);
    if (!buf) return V_nil();

    struct tm tmbuf;
#if !defined(_WIN32)
    struct tm *tp = use_utc ? gmtime_r(&tt, &tmbuf) : localtime_r(&tt, &tmbuf);
#else
    struct tm *tmp = use_utc ? gmtime(&tt) : localtime(&tt);
    if (!tmp) { free(buf); return V_nil(); }
    tmbuf = *tmp;
    struct tm *tp = &tmbuf;
#endif
    if (!tp) { free(buf); return V_nil(); }

    for (;;) {
      size_t n = strftime(buf, cap, fmt, tp);
      if (n > 0) {
        Value out = V_str_from_c(buf);
        free(buf);
        return out;
      }
      /* grow buffer and retry */
      cap *= 2;
      char *nb = (char*)realloc(buf, cap);
      if (!nb) { free(buf); return V_nil(); }
      buf = nb;
    }
  }
}

/* -------------------------------------------------------
 * register
 * ------------------------------------------------------- */
void register_os_lib(struct VM *vm) {
  Value os = V_table();
  tbl_set_public(os.as.t, V_str_from_c("time"),      (Value){.tag=VAL_CFUNC, .as.cfunc=os_time});
  tbl_set_public(os.as.t, V_str_from_c("difftime"),  (Value){.tag=VAL_CFUNC, .as.cfunc=os_difftime});
  tbl_set_public(os.as.t, V_str_from_c("clock"),     (Value){.tag=VAL_CFUNC, .as.cfunc=os_clock});
  tbl_set_public(os.as.t, V_str_from_c("getenv"),    (Value){.tag=VAL_CFUNC, .as.cfunc=os_getenv});
  tbl_set_public(os.as.t, V_str_from_c("execute"),   (Value){.tag=VAL_CFUNC, .as.cfunc=os_execute});
  tbl_set_public(os.as.t, V_str_from_c("exit"),      (Value){.tag=VAL_CFUNC, .as.cfunc=os_exit});
  tbl_set_public(os.as.t, V_str_from_c("remove"),    (Value){.tag=VAL_CFUNC, .as.cfunc=os_remove});
  tbl_set_public(os.as.t, V_str_from_c("rename"),    (Value){.tag=VAL_CFUNC, .as.cfunc=os_rename});
  tbl_set_public(os.as.t, V_str_from_c("setlocale"), (Value){.tag=VAL_CFUNC, .as.cfunc=os_setlocale});
  tbl_set_public(os.as.t, V_str_from_c("tmpname"),   (Value){.tag=VAL_CFUNC, .as.cfunc=os_tmpname});
  tbl_set_public(os.as.t, V_str_from_c("date"),      (Value){.tag=VAL_CFUNC, .as.cfunc=os_date});

tbl_set_public(os.as.t, V_str_from_c("chdir"), (Value){.tag=VAL_CFUNC, .as.cfunc=os_chdir});
tbl_set_public(os.as.t, V_str_from_c("getcwd"), (Value){.tag=VAL_CFUNC, .as.cfunc=os_getcwd});
  env_add_public(vm->env, "os", os, false);
}
