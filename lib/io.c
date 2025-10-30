// lib/io.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include "../include/interpreter.h"

/* ===========================================================
 *  File boxing & helpers
 * =========================================================== */

static const char *FH_PTR = "_fh_ptr";   /* hidden FILE* (stored in CFunc slot) */
static const char *FH_CLS = "_closed";   /* boolean flag: was closed */
static const char *FH_POPEN = "_popen";  /* boolean flag: opened with popen */

static Value g_stdin_box;
static Value g_stdout_box;
static Value g_stderr_box;

/* current default files for io.read/io.write */
static Value g_in_box;
static Value g_out_box;

static Value V_true(void){ return V_bool(1); }
static Value V_false(void){ return V_bool(0); }

/* Forward decls for file methods (used by attach_file_methods) */
static Value f_close(struct VM *vm, int argc, Value *argv);
static Value f_flush(struct VM *vm, int argc, Value *argv);
static Value f_read (struct VM *vm, int argc, Value *argv);
static Value f_write(struct VM *vm, int argc, Value *argv);
static Value f_lines(struct VM *vm, int argc, Value *argv);
static Value f_seek (struct VM *vm, int argc, Value *argv);

/* Attach file methods onto a boxed file table */
static void attach_file_methods(Value box) {
  if (box.tag != VAL_TABLE) return;
  Value m_read  = (Value){.tag=VAL_CFUNC, .as.cfunc=f_read};
  Value m_write = (Value){.tag=VAL_CFUNC, .as.cfunc=f_write};
  Value m_flush = (Value){.tag=VAL_CFUNC, .as.cfunc=f_flush};
  Value m_close = (Value){.tag=VAL_CFUNC, .as.cfunc=f_close};
  Value m_lines = (Value){.tag=VAL_CFUNC, .as.cfunc=f_lines};
  Value m_seek  = (Value){.tag=VAL_CFUNC, .as.cfunc=f_seek};
  tbl_set_public(box.as.t, V_str_from_c("read"),  m_read);
  tbl_set_public(box.as.t, V_str_from_c("write"), m_write);
  tbl_set_public(box.as.t, V_str_from_c("flush"), m_flush);
  tbl_set_public(box.as.t, V_str_from_c("close"), m_close);
  tbl_set_public(box.as.t, V_str_from_c("lines"), m_lines);
  tbl_set_public(box.as.t, V_str_from_c("seek"),  m_seek);
}

/* Store FILE* inside a table using the CFunc slot as an opaque pointer. */
static Value box_file(FILE *fp) {
  Value t = V_table();
  Value ptr = { .tag = VAL_CFUNC };
  ptr.as.cfunc = (CFunc)fp;
  tbl_set_public(t.as.t, V_str_from_c(FH_PTR), ptr);
  tbl_set_public(t.as.t, V_str_from_c(FH_CLS), V_false());
  tbl_set_public(t.as.t, V_str_from_c(FH_POPEN), V_false());
  attach_file_methods(t);
  return t;
}

static Value box_popen_file(FILE *fp) {
  Value t = box_file(fp);
  tbl_set_public(t.as.t, V_str_from_c(FH_POPEN), V_true());
  return t;
}

static FILE *unbox_file(Value v) {
  if (v.tag != VAL_TABLE) return NULL;
  Value ptr;
  if (!tbl_get_public(v.as.t, V_str_from_c(FH_PTR), &ptr)) return NULL;
  if (ptr.tag != VAL_CFUNC) return NULL;
  return (FILE*)ptr.as.cfunc;
}

static int is_closed_box(Value v) {
  if (v.tag != VAL_TABLE) return 0;
  Value fl; 
  if (!tbl_get_public(v.as.t, V_str_from_c(FH_CLS), &fl)) return 0;
  return (fl.tag == VAL_BOOL && fl.as.b);
}

static int is_popen_box(Value v) {
  if (v.tag != VAL_TABLE) return 0;
  Value fl;
  if (!tbl_get_public(v.as.t, V_str_from_c(FH_POPEN), &fl)) return 0;
  return (fl.tag == VAL_BOOL && fl.as.b);
}

static int is_file_box(Value v) {
  if (v.tag != VAL_TABLE) return 0;
  /* has _fh_ptr field (even if nil after closing) */
  Value _;
  return tbl_get_public(v.as.t, V_str_from_c(FH_PTR), &_);
}

/* tostring-ish fallback for write */
static const char *as_cstring(Value v, char *tmp, size_t tmpsz) {
  if (v.tag == VAL_STR) return v.as.s->data;
  if (v.tag == VAL_INT) { snprintf(tmp, tmpsz, "%lld", v.as.i); return tmp; }
  if (v.tag == VAL_NUM) { snprintf(tmp, tmpsz, "%.17g", v.as.n); return tmp; }
  if (v.tag == VAL_BOOL) return v.as.b ? "true" : "false";
  if (v.tag == VAL_NIL) return "nil";
  snprintf(tmp, tmpsz, "table:%p", (void*)(v.tag==VAL_TABLE? v.as.t : NULL));
  return tmp;
}

/* ===========================================================
 *  read primitives (support *l, *L, *a, *n, number)
 * =========================================================== */

typedef struct {
  int keep_newline; /* for *L vs *l */
} LineMode;

static Value read_line(FILE *fp, LineMode lm) {
  size_t cap = 256, len = 0;
  char *buf = (char*)malloc(cap);
  if (!buf) return V_nil();

  int c = fgetc(fp);
  if (c == EOF) { free(buf); return V_nil(); }

  while (c != EOF) {
    if (len + 2 > cap) {
      size_t newcap = cap * 2;
      char *nb = (char*)realloc(buf, newcap);
      if (!nb) { free(buf); return V_nil(); }
      buf = nb; cap = newcap;
    }
    buf[len++] = (char)c;
    if (c == '\n') break;
    c = fgetc(fp);
  }
  if (!lm.keep_newline && len > 0 && buf[len-1] == '\n') len--; /* strip */
  buf[len] = '\0';
  Value s = V_str_from_c(buf);
  free(buf);
  return s;
}

static Value read_all(FILE *fp) {
  size_t cap = 4096, len = 0;
  char *buf = (char*)malloc(cap);
  if (!buf) return V_nil();
  for (;;) {
    if (len + 2049 > cap) {
      size_t newcap = cap * 2;
      char *nb = (char*)realloc(buf, newcap);
      if (!nb) { free(buf); return V_nil(); }
      buf = nb; cap = newcap;
    }
    size_t n = fread(buf + len, 1, 2048, fp);
    len += n;
    if (n < 2048) break;
  }
  buf[len] = '\0';
  Value s = V_str_from_c(buf);
  free(buf);
  return s;
}

static Value read_number(FILE *fp) {
  char buf[128];
  int idx = 0;
  int c;
  
  /* Skip whitespace */
  while ((c = fgetc(fp)) != EOF && isspace(c));
  if (c == EOF) return V_nil();
  
  /* Read number (simple implementation) */
  int has_dot = 0;
  if (c == '+' || c == '-') {
    buf[idx++] = c;
    c = fgetc(fp);
  }
  
  while (c != EOF && idx < 127) {
    if (isdigit(c)) {
      buf[idx++] = c;
    } else if (c == '.' && !has_dot) {
      has_dot = 1;
      buf[idx++] = c;
    } else if (c == 'e' || c == 'E') {
      buf[idx++] = c;
      c = fgetc(fp);
      if (c == '+' || c == '-') {
        buf[idx++] = c;
      } else {
        ungetc(c, fp);
      }
      c = fgetc(fp);
      continue;
    } else {
      ungetc(c, fp);
      break;
    }
    c = fgetc(fp);
  }
  
  if (idx == 0) return V_nil();
  buf[idx] = '\0';
  
  char *endp;
  if (has_dot) {
    double d = strtod(buf, &endp);
    if (endp == buf) return V_nil();
    return V_num(d);
  } else {
    long long ll = strtoll(buf, &endp, 10);
    if (endp == buf) return V_nil();
    return V_int(ll);
  }
}

static Value read_n(FILE *fp, long nbytes) {
  if (nbytes <= 0) return V_str_from_c("");
  char *buf = (char*)malloc((size_t)nbytes + 1);
  if (!buf) return V_nil();
  size_t rd = fread(buf, 1, (size_t)nbytes, fp);
  /* Return what we got, even if less than requested (Lua behavior) */
  buf[rd] = '\0';
  Value s = V_str_from_c(buf);
  free(buf);
  return s;
}

/* ===========================================================
 *  file methods (self = argv[0])
 * =========================================================== */

static Value f_close(struct VM *vm, int argc, Value *argv) {
  (void)vm; (void)argc;
  if (argc < 1 || !is_file_box(argv[0])) return V_nil();
  FILE *fp = unbox_file(argv[0]);
  if (!fp) return V_nil();

  /* Standard streams: mark success but don't actually close */
  if (fp == stdin || fp == stdout || fp == stderr) {
    return V_true();  /* Lua returns true for standard streams */
  }

  /* Check if this was opened with popen */
  int rc;
  if (is_popen_box(argv[0])) {
    rc = pclose(fp);
  } else {
    rc = fclose(fp);
  }
  
  /* Mark as closed */
  tbl_set_public(argv[0].as.t, V_str_from_c(FH_PTR), V_nil());
  tbl_set_public(argv[0].as.t, V_str_from_c(FH_CLS), V_true());
  
  if (rc != 0) {
    /* In real Lua: return (nil, strerror(errno), errno) */
    return V_nil();
  }
  return V_true();  /* Return true on success */
}

static Value f_flush(struct VM *vm, int argc, Value *argv) {
  (void)vm; (void)argc;
  if (argc < 1 || !is_file_box(argv[0])) return V_nil();
  if (is_closed_box(argv[0])) return V_nil();
  FILE *fp = unbox_file(argv[0]); if (!fp) return V_nil();
  if (fflush(fp) != 0) return V_nil();
  return V_true();  /* Return true on success */
}

/* file:read([fmt ...]) - supports multiple formats */
static Value f_read(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || !is_file_box(argv[0])) return V_nil();
  if (is_closed_box(argv[0])) return V_nil();
  FILE *fp = unbox_file(argv[0]); if (!fp) return V_nil();

  /* If no fmt → default *l */
  if (argc == 1) {
    return read_line(fp, (LineMode){ .keep_newline = 0 });
  }

  /* Single format case (most common) */
  if (argc == 2) {
    Value fmt = argv[1];
    if (fmt.tag == VAL_STR) {
      const char *m = fmt.as.s->data;
      if (strcmp(m, "*l") == 0) {
        return read_line(fp, (LineMode){ .keep_newline = 0 });
      } else if (strcmp(m, "*L") == 0) {
        return read_line(fp, (LineMode){ .keep_newline = 1 });
      } else if (strcmp(m, "*a") == 0) {
        return read_all(fp);
      } else if (strcmp(m, "*n") == 0) {
        return read_number(fp);
      } else {
        return V_nil();
      }
    } else if (fmt.tag == VAL_INT || fmt.tag == VAL_NUM) {
      long n = (fmt.tag == VAL_INT) ? (long)fmt.as.i : (long)fmt.as.n;
      return read_n(fp, n);
    }
    return read_line(fp, (LineMode){ .keep_newline = 0 });
  }

  /* Multiple formats: build a table of results */
  Value result = V_table();
  for (int i = 1; i < argc; ++i) {
    Value v = V_nil();
    Value fmt = argv[i];
    
    if (fmt.tag == VAL_STR) {
      const char *m = fmt.as.s->data;
      if (strcmp(m, "*l") == 0) {
        v = read_line(fp, (LineMode){ .keep_newline = 0 });
      } else if (strcmp(m, "*L") == 0) {
        v = read_line(fp, (LineMode){ .keep_newline = 1 });
      } else if (strcmp(m, "*a") == 0) {
        v = read_all(fp);
      } else if (strcmp(m, "*n") == 0) {
        v = read_number(fp);
      }
    } else if (fmt.tag == VAL_INT || fmt.tag == VAL_NUM) {
      long n = (fmt.tag == VAL_INT) ? (long)fmt.as.i : (long)fmt.as.n;
      v = read_n(fp, n);
    }
    
    /* Stop on nil (EOF) */
    if (v.tag == VAL_NIL) break;
    tbl_set_public(result.as.t, V_int(i), v);
  }
  
  return result;
}

/* file:write(...) → returns the file handle on success (Lua style) */
static Value f_write(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || !is_file_box(argv[0])) return V_nil();
  if (is_closed_box(argv[0])) return V_nil();
  FILE *fp = unbox_file(argv[0]); if (!fp) return V_nil();

  for (int i = 1; i < argc; ++i) {
    char tmp[128];  /* Increased buffer size */
    const char *s = as_cstring(argv[i], tmp, sizeof(tmp));
    size_t want = strlen(s);
    if (want && fwrite(s, 1, want, fp) < want) return V_nil();
  }
  return argv[0];
}

/* file:seek([whence [, offset]]) */
static Value f_seek(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || !is_file_box(argv[0])) return V_nil();
  if (is_closed_box(argv[0])) return V_nil();
  FILE *fp = unbox_file(argv[0]); if (!fp) return V_nil();

  /* Default: "cur", 0 */
  int whence = SEEK_CUR;
  long offset = 0;

  if (argc >= 2 && argv[1].tag == VAL_STR) {
    const char *w = argv[1].as.s->data;
    if (strcmp(w, "set") == 0) whence = SEEK_SET;
    else if (strcmp(w, "cur") == 0) whence = SEEK_CUR;
    else if (strcmp(w, "end") == 0) whence = SEEK_END;
    else return V_nil();  /* Invalid whence */
  }

  if (argc >= 3) {
    if (argv[2].tag == VAL_INT) offset = (long)argv[2].as.i;
    else if (argv[2].tag == VAL_NUM) offset = (long)argv[2].as.n;
  }

  if (fseek(fp, offset, whence) != 0) return V_nil();
  
  long pos = ftell(fp);
  if (pos < 0) return V_nil();
  
  return V_int((long long)pos);
}

/* -----------------------------------------------------------
 * file:lines([fmt...]) → iterator
 * ----------------------------------------------------------- */

typedef struct {
  FILE *fp;
  int keep_newline;
  long nbytes;          /* if >0, read fixed bytes; else -1 */
  int close_on_eof;     /* close when EOF? (io.lines(filename)) */
  int use_number;       /* if 1, read numbers with *n */
} LinesState;

/* state boxing for iterator */
static const char *LS_PTR = "_ls_ptr";

static Value box_lines_state(LinesState *ls) {
  Value t = V_table();
  Value p = { .tag = VAL_CFUNC };
  p.as.cfunc = (CFunc)ls;
  tbl_set_public(t.as.t, V_str_from_c(LS_PTR), p);
  return t;
}
static LinesState* unbox_lines_state(Value v) {
  if (v.tag != VAL_TABLE) return NULL;
  Value p;
  if (!tbl_get_public(v.as.t, V_str_from_c(LS_PTR), &p)) return NULL;
  if (p.tag != VAL_CFUNC) return NULL;
  return (LinesState*)p.as.cfunc;
}

static Value lines_iter(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1) return V_nil();
  LinesState *ls = unbox_lines_state(argv[0]);
  if (!ls || !ls->fp) return V_nil();

  Value v = V_nil();
  if (ls->use_number) {
    v = read_number(ls->fp);
  } else if (ls->nbytes > 0) {
    v = read_n(ls->fp, ls->nbytes);
  } else {
    v = read_line(ls->fp, (LineMode){ .keep_newline = ls->keep_newline });
  }

  if (v.tag == VAL_NIL) {
    if (ls->close_on_eof && ls->fp != stdin && ls->fp != stdout && ls->fp != stderr) {
      fclose(ls->fp);
    }
    ls->fp = NULL;
    free(ls);  /* Clean up memory */
  }
  return v;
}

static Value f_lines(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || !is_file_box(argv[0])) return V_nil();
  if (is_closed_box(argv[0])) return V_nil();
  FILE *fp = unbox_file(argv[0]); if (!fp) return V_nil();

  int keep_newline = 0;
  long nbytes = -1;
  int use_number = 0;
  
  if (argc >= 2) {
    Value fmt = argv[1];
    if (fmt.tag == VAL_STR) {
      const char *m = fmt.as.s->data;
      if (strcmp(m, "*L") == 0) keep_newline = 1;
      else if (strcmp(m, "*l") == 0) keep_newline = 0;
      else if (strcmp(m, "*n") == 0) use_number = 1;
      else if (strcmp(m, "*a") == 0) keep_newline = 0;
    } else if (fmt.tag == VAL_INT || fmt.tag == VAL_NUM) {
      nbytes = (fmt.tag == VAL_INT) ? (long)fmt.as.i : (long)fmt.as.n;
    }
  }

  LinesState *ls = (LinesState*)malloc(sizeof(LinesState));
  if (!ls) return V_nil();
  ls->fp = fp;
  ls->keep_newline = keep_newline;
  ls->nbytes = nbytes;
  ls->use_number = use_number;
  ls->close_on_eof = 0;

  Value state = box_lines_state(ls);
  Value iter; iter.tag = VAL_CFUNC; iter.as.cfunc = lines_iter;
  Value triple = V_table();
  tbl_set_public(triple.as.t, V_int(1), iter);
  tbl_set_public(triple.as.t, V_int(2), state);
  tbl_set_public(triple.as.t, V_int(3), V_nil());
  return triple;
}

/* ===========================================================
 *  top-level io.* functions
 * =========================================================== */

static Value io_type(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1) return V_nil();
  if (!is_file_box(argv[0])) return V_nil();
  if (is_closed_box(argv[0])) return V_str_from_c("closed file");
  return V_str_from_c("file");
}

static Value io_popen(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1) return V_nil();

  const char *mode = "r";
  const char *cmd = NULL;

  /* Coerce potential boxed strings */
  if (argv[0].tag == VAL_STR) {
    cmd = argv[0].as.s->data;
  } else if (argv[0].tag == VAL_TABLE) {
    Value inner;
    if (tbl_get_public(argv[0].as.t, V_str_from_c("data"), &inner) && inner.tag == VAL_STR)
      cmd = inner.as.s->data;
  }
  if (!cmd) return V_nil();

  if (argc >= 2 && argv[1].tag == VAL_STR)
    mode = argv[1].as.s->data;

  /* SECURITY FIX: Let popen handle the command directly without wrapping in shell */
  FILE *fp = popen(cmd, mode);
  if (!fp) {
    fprintf(stderr, "[io.popen] failed for cmd=%s: %s\n", cmd, strerror(errno));
    return V_nil();
  }

  return box_popen_file(fp);
}

/* io.write(...) -> writes to current output (stdout by default) */
static Value io_write(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  Value out = is_file_box(g_out_box) ? g_out_box : g_stdout_box;
  Value args[64];
  Value *pargs = args;
  if (argc + 1 > 64) pargs = (Value*)malloc(sizeof(Value)*(argc+1));
  pargs[0] = out;
  for (int i=0;i<argc;i++) pargs[i+1] = argv[i];
  Value r = f_write(vm, argc+1, pargs);
  if (pargs != args) free(pargs);
  return r;
}

static Value io_flush(struct VM *vm, int argc, Value *argv) {
  (void)argv;
  Value out = is_file_box(g_out_box) ? g_out_box : g_stdout_box;
  Value args[1] = { out };
  return f_flush(vm, 1, args);
}

static Value io_read(struct VM *vm, int argc, Value *argv) {
  Value in = is_file_box(g_in_box) ? g_in_box : g_stdin_box;
  Value *args = (Value*)malloc(sizeof(Value) * (argc + 1));
  if (!args) return V_nil();
  args[0] = in;
  for (int i = 0; i < argc; ++i) args[i+1] = argv[i];
  Value r = f_read(vm, argc + 1, args);
  free(args);
  return r;
}

static Value io_open(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1 || argv[0].tag != VAL_STR) return V_nil();
  const char *path = argv[0].as.s->data;
  const char *mode = "r";
  if (argc >= 2 && argv[1].tag == VAL_STR) mode = argv[1].as.s->data;
  FILE *fp = fopen(path, mode);
  if (!fp) return V_nil();
  return box_file(fp);
}

static Value io_close(struct VM *vm, int argc, Value *argv) {
  /* Lua: io.close() with no args closes default output */
  if (argc < 1) {
    Value out = is_file_box(g_out_box) ? g_out_box : g_stdout_box;
    Value args[1] = { out };
    return f_close(vm, 1, args);
  }
  if (!is_file_box(argv[0])) return V_nil();
  Value args[1] = { argv[0] };
  return f_close(vm, 1, args);
}

static Value io_tmpfile(struct VM *vm, int argc, Value *argv) {
  (void)vm; (void)argc; (void)argv;
  FILE *fp = tmpfile();
  if (!fp) return V_nil();
  return box_file(fp);
}

/* io.input([file|string]) / io.output([file|string]) */
static Value io_input(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1) {
    return is_file_box(g_in_box) ? g_in_box : g_stdin_box;
  }
  if (argv[0].tag == VAL_STR) {
    FILE *fp = fopen(argv[0].as.s->data, "r");
    if (!fp) return V_nil();
    Value f = box_file(fp);
    g_in_box = f;
    return f;
  }
  if (is_file_box(argv[0])) {
    g_in_box = argv[0];
    return argv[0];
  }
  return V_nil();
}

static Value io_output(struct VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 1) {
    return is_file_box(g_out_box) ? g_out_box : g_stdout_box;
  }
  if (argv[0].tag == VAL_STR) {
    FILE *fp = fopen(argv[0].as.s->data, "w");
    if (!fp) return V_nil();
    Value f = box_file(fp);
    g_out_box = f;
    return f;
  }
  if (is_file_box(argv[0])) {
    g_out_box = argv[0];
    return argv[0];
  }
  return V_nil();
}

/* io.lines([filename] [, fmt...]) */
static Value io_lines(struct VM *vm, int argc, Value *argv) {
  (void)vm;

  FILE *fp = NULL;
  int close_on_eof = 0;
  int keep_newline = 0;
  long nbytes = -1;
  int use_number = 0;

  int argi = 0;
  if (argc >= 1 && argv[0].tag == VAL_STR) {
    fp = fopen(argv[0].as.s->data, "r");
    if (!fp) return V_nil();
    close_on_eof = 1;
    argi = 1;
  } else {
    fp = stdin;
  }

  if (argc > argi) {
    Value fmt = argv[argi];
    if (fmt.tag == VAL_STR) {
      const char *m = fmt.as.s->data;
      if (strcmp(m, "*L") == 0) keep_newline = 1;
      else if (strcmp(m, "*l") == 0) keep_newline = 0;
      else if (strcmp(m, "*n") == 0) use_number = 1;
      else if (strcmp(m, "*a") == 0) keep_newline = 0;
    } else if (fmt.tag == VAL_INT || fmt.tag == VAL_NUM) {
      nbytes = (fmt.tag == VAL_INT) ? (long)fmt.as.i : (long)fmt.as.n;
    }
  }

  LinesState *ls = (LinesState*)malloc(sizeof(LinesState));
  if (!ls) { if (close_on_eof && fp && fp!=stdin) fclose(fp); return V_nil(); }
  ls->fp = fp;
  ls->keep_newline = keep_newline;
  ls->nbytes = nbytes;
  ls->use_number = use_number;
  ls->close_on_eof = close_on_eof;

  Value state = box_lines_state(ls);
  Value iter; iter.tag = VAL_CFUNC; iter.as.cfunc = lines_iter;
  Value triple = V_table();
  tbl_set_public(triple.as.t, V_int(1), iter);
  tbl_set_public(triple.as.t, V_int(2), state);
  tbl_set_public(triple.as.t, V_int(3), V_nil());
  return triple;
}

/* ===========================================================
 *  registration
 * =========================================================== */

void register_io_lib(struct VM *vm) {
  /* std streams */
  g_stdin_box  = box_file(stdin);
  g_stdout_box = box_file(stdout);
  g_stderr_box = box_file(stderr);

  /* default in/out */
  g_in_box  = g_stdin_box;
  g_out_box = g_stdout_box;

  /* io table */
  Value io = V_table();
  tbl_set_public(io.as.t, V_str_from_c("write"),   (Value){.tag=VAL_CFUNC, .as.cfunc=io_write});
  tbl_set_public(io.as.t, V_str_from_c("flush"),   (Value){.tag=VAL_CFUNC, .as.cfunc=io_flush});
  tbl_set_public(io.as.t, V_str_from_c("read"),    (Value){.tag=VAL_CFUNC, .as.cfunc=io_read});
  tbl_set_public(io.as.t, V_str_from_c("open"),    (Value){.tag=VAL_CFUNC, .as.cfunc=io_open});
  tbl_set_public(io.as.t, V_str_from_c("close"),   (Value){.tag=VAL_CFUNC, .as.cfunc=io_close});
  tbl_set_public(io.as.t, V_str_from_c("type"),    (Value){.tag=VAL_CFUNC, .as.cfunc=io_type});
  tbl_set_public(io.as.t, V_str_from_c("lines"),   (Value){.tag=VAL_CFUNC, .as.cfunc=io_lines});
  tbl_set_public(io.as.t, V_str_from_c("input"),   (Value){.tag=VAL_CFUNC, .as.cfunc=io_input});
  tbl_set_public(io.as.t, V_str_from_c("output"),  (Value){.tag=VAL_CFUNC, .as.cfunc=io_output});
  tbl_set_public(io.as.t, V_str_from_c("popen"),   (Value){.tag=VAL_CFUNC, .as.cfunc=io_popen});
  tbl_set_public(io.as.t, V_str_from_c("tmpfile"), (Value){.tag=VAL_CFUNC, .as.cfunc=io_tmpfile});

  tbl_set_public(io.as.t, V_str_from_c("stdin"),   g_stdin_box);
  tbl_set_public(io.as.t, V_str_from_c("stdout"),  g_stdout_box);
  tbl_set_public(io.as.t, V_str_from_c("stderr"),  g_stderr_box);

  env_add_public(vm->env, "io", io, false);
}
