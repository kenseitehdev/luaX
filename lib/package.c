// lib/package.c
// Package core library: package table, searchers (preload, Lua files, C libs), require(), loadlib().

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../include/interpreter.h"   // brings interpreter.h / Value / CFunc etc.

/* If you have a real file loader, compile with -DHAVE_VM_LOAD_AND_RUN_FILE
   and provide: Value vm_load_and_run_file(VM*, const char*, const char*);
*/
#ifdef HAVE_VM_LOAD_AND_RUN_FILE
extern Value vm_load_and_run_file(VM *vm, const char *path, const char *modname);
#endif

/* ===== Platform dynamic-loading ===== */
#if defined(_WIN32)
  #include <windows.h>
  typedef HMODULE DLHandle;
  static DLHandle dl_open(const char *p){ return LoadLibraryA(p); }
  static void*    dl_sym  (DLHandle h, const char *s){ return (void*)GetProcAddress(h, s); }
  static const char* dl_error(void){ return "LoadLibrary/GetProcAddress failed"; }
  static int      dl_close(DLHandle h){ return FreeLibrary(h) ? 0 : 1; }
  #define DIR_SEP '\\'
  #define DFLT_LUA_PATH  ".\\?.lua;.\\?\\init.lua"
  #define DFLT_C_PATH    ".\\?.dll;.\\lib?.dll"
#else
  #include <dlfcn.h>
  typedef void* DLHandle;
  static DLHandle dl_open(const char *p){ return dlopen(p, RTLD_NOW); }
  static void*    dl_sym  (DLHandle h, const char *s){ return dlsym(h, s); }
  static const char* dl_error(void){ const char *e = dlerror(); return e ? e : "dlopen/dlsym failed"; }
  static int      dl_close(DLHandle h){ return dlclose(h); }
  #define DIR_SEP '/'
  #define DFLT_LUA_PATH  "./?.lua;./?/init.lua"
  #define DFLT_C_PATH    "./?.so;./lib?.so"
#endif

/* ===== Simple handle cache (keeps libs loaded) ===== */
typedef struct DLCache {
  char *path;
  DLHandle h;
  struct DLCache *next;
} DLCache;

static DLCache *g_dlcache = NULL;

static DLHandle cache_lookup_open_handle(const char *path) {
  for (DLCache *p = g_dlcache; p; p = p->next) {
    if (strcmp(p->path, path) == 0) return p->h;
  }
  return NULL;
}

static DLHandle cache_add_handle(const char *path, DLHandle h) {
  DLCache *node = (DLCache*)malloc(sizeof(DLCache));
  if (!node) return NULL;
  node->path = (char*)malloc(strlen(path)+1);
  if (!node->path) { free(node); return NULL; }
  strcpy(node->path, path);
  node->h = h;
  node->next = g_dlcache;
  g_dlcache = node;
  return h;
}

/* ===== VM/Env/Table helpers provided elsewhere ===== */
extern Env*   env_root(Env *e);
extern void   env_add(Env *e, const char *name, Value v, bool is_local);
extern int    tbl_get(Table *t, Value key, Value *out);
extern void   tbl_set(Table *t, Value key, Value val);
extern Value  call_any_public(VM *vm, Value cal, int argc, Value *argv);
extern void   vm_raise(VM *vm, Value err);

/* ===== Local helpers ===== */

static Value get_or_create_table_field(Table *t, const char *name) {
  Value key = V_str_from_c(name);
  Value out;
  if (tbl_get(t, key, &out) && out.tag == VAL_TABLE) return out;
  Value nt = V_table();
  tbl_set(t, key, nt);
  return nt;
}

static Value get_or_set_string(Table *t, const char *name, const char *s) {
  Value key = V_str_from_c(name);
  Value out;
  if (tbl_get(t, key, &out) && out.tag == VAL_STR) return out;
  Value vs = V_str_from_c(s);
  tbl_set(t, key, vs);
  return vs;
}

static Value get_field(Table *t, const char *name) {
  Value out;
  if (tbl_get(t, V_str_from_c(name), &out)) return out;
  return V_nil();
}

/* push into array-like table at next integer index (1..n) */
static void push_array(Table *arr, Value v) {
  long long i = 1; Value tmp;
  while (tbl_get(arr, V_int(i), &tmp)) i++;
  tbl_set(arr, V_int(i), v);
}

/* Replace '.' with DIR_SEP in a copy of modname → e.g., "a.b.c" -> "a/b/c" */
static char* module_name_to_path_component(const char *modname) {
  size_t n = strlen(modname);
  char *buf = (char*)malloc(n + 1);
  for (size_t i=0;i<n;i++){
    char c = modname[i];
    buf[i] = (c=='.') ? DIR_SEP : c;
  }
  buf[n] = 0;
  return buf;
}

/* Expand one template like "/foo/?.lua" with component "a/b" -> "/foo/a/b.lua".
   Returns malloc'ed string. */
static char* expand_template(const char *templ, const char *component) {
  size_t tlen = strlen(templ), clen = strlen(component);
  size_t out_cap = tlen + clen * 4 + 4;
  char *out = (char*)malloc(out_cap);
  size_t j=0;
  for (size_t i=0;i<tlen;i++){
    char c = templ[i];
    if (c == '?') {
      size_t need = j + clen + 1;
      if (need > out_cap){ out_cap = need*2; out = (char*)realloc(out, out_cap); }
      memcpy(out + j, component, clen); j += clen;
    } else {
      if (j+2 > out_cap){ out_cap *= 2; out = (char*)realloc(out, out_cap); }
      out[j++] = c;
    }
  }
  out[j] = 0;
  return out;
}

/* Iterate over ; separated path string; for each template, call cb(template, user). */
typedef void (*each_path_cb)(const char *templ, void *user);
static void for_each_path(const char *path, each_path_cb cb, void *user) {
  const char *p = path;
  while (*p) {
    const char *semi = strchr(p, ';');
    size_t len = semi ? (size_t)(semi - p) : strlen(p);
    char *chunk = (char*)malloc(len + 1);
    memcpy(chunk, p, len); chunk[len]=0;
    cb(chunk, user);
    free(chunk);
    p = semi ? (semi + 1) : (p + len);
    if (!*p) break;
  }
}

/* ===== Global package instance ===== */
static Table *g_pkg = NULL;
static void ensure_package_initialized(VM *vm);

/* ===== Searchers =====
   searcher(name) -> { loader_func, extra... }  OR  "error string"
*/

/* searcher 1: package.preload[name] */
static Value pkg_preload_searcher(VM *vm, int argc, Value *argv) {
  (void)argc;
  if (argc < 1 || argv[0].tag != VAL_STR) {
    return V_str_from_c("preload searcher: module name must be a string");
  }

  ensure_package_initialized(vm);
  Value preload = get_or_create_table_field(g_pkg, "preload");
  if (preload.tag != VAL_TABLE) return V_str_from_c("preload searcher: package.preload is not a table");

  Value loader;
  if (tbl_get(preload.as.t, argv[0], &loader) && loader.tag == VAL_CFUNC) {
    Value tup = V_table();               /* { loader, modname } */
    tbl_set(tup.as.t, V_int(1), loader);
    tbl_set(tup.as.t, V_int(2), argv[0]);
    return tup;
  }

  return V_str_from_c("preload searcher: not found in package.preload");
}

/* ---- Filesystem Lua loader ---- */

/* When invoked, this loader runs the Lua file at 'path' and returns module value. */
static Value lua_file_loader(VM *vm, int argc, Value *argv) {
  if (argc < 2 || argv[0].tag != VAL_STR || argv[1].tag != VAL_STR) {
    return V_str_from_c("lua file loader: expected (modname, path)");
  }
  const char *modname = argv[0].as.s->data;
  const char *path    = argv[1].as.s->data;
#ifdef HAVE_VM_LOAD_AND_RUN_FILE
  return vm_load_and_run_file(vm, path, modname);
#else
  return V_str_from_c("lua file loader: vm_load_and_run_file() not available");
#endif
}

/* context for filesystem search */
struct FsAcc {
  const char *component;
  char *found;
};
static void fs_try_one(const char *templ, void *u) {
  struct FsAcc *A = (struct FsAcc*)u;
  char *cand = expand_template(templ, A->component);
  FILE *f = fopen(cand, "rb");
  if (f) {
    fclose(f);
    if (!A->found) A->found = cand; else free(cand);
  } else {
    free(cand);
  }
}

/* searcher 2: Lua filesystem searcher using package.path */
static Value pkg_filesystem_searcher(VM *vm, int argc, Value *argv) {
  (void)argc;
  if (argc < 1 || argv[0].tag != VAL_STR) {
    return V_str_from_c("filesystem searcher: module name must be a string");
  }
  ensure_package_initialized(vm);

  const char *name = argv[0].as.s->data;
  char *component  = module_name_to_path_component(name);
  Value vpath = get_field(g_pkg, "path");
  const char *path = (vpath.tag == VAL_STR) ? vpath.as.s->data : DFLT_LUA_PATH;

  struct FsAcc acc = { component, NULL };
  for_each_path(path, fs_try_one, &acc);
  free(component);

  if (!acc.found) {
    return V_str_from_c("filesystem searcher: not found in package.path");
  }

  /* Return { loader_func, path } */
  Value tup = V_table();
  Value loader; loader.tag = VAL_CFUNC; loader.as.cfunc = lua_file_loader;
  tbl_set(tup.as.t, V_int(1), loader);
  tbl_set(tup.as.t, V_int(2), V_str_from_c(acc.found));
  free(acc.found);
  return tup;
}

/* ---- C loader via loadlib ---- */

#define MODULE_INIT_PREFIX "luaopen_"  /* adjust if your naming differs */

/* package.loadlib(path, initname) -> cfunc | string(error) */
static Value builtin_loadlib(VM *vm, int argc, Value *argv) {
  (void)vm;
  if (argc < 2 || argv[0].tag != VAL_STR || argv[1].tag != VAL_STR) {
    return V_str_from_c("loadlib: expected (path, initname) strings");
  }
  const char *path = argv[0].as.s->data;
  const char *init = argv[1].as.s->data;

  /* 1) get or open the handle */
  DLHandle h = cache_lookup_open_handle(path);
  if (!h) {
    h = dl_open(path);
    if (!h) {
      const char *err = dl_error();
      size_t L = strlen("loadlib: dlopen failed: ") + strlen(err) + 1;
      char *buf = (char*)malloc(L);
      if (!buf) return V_str_from_c("loadlib: OOM");
      snprintf(buf, L, "loadlib: dlopen failed: %s", err);
      Value r = V_str_from_c(buf); free(buf);
      return r;
    }
    if (!cache_add_handle(path, h)) {
      /* caching failed; we still keep h open so the symbol stays valid */
    }
  }

  /* 2) resolve the init symbol */
  typedef Value (*InitFn)(VM*, int, Value*);
  void *sym = dl_sym(h, init);
  if (!sym) {
    const char *err = dl_error();
    size_t L = strlen("loadlib: symbol not found: ") + strlen(err) + 1;
    char *buf = (char*)malloc(L);
    if (!buf) return V_str_from_c("loadlib: OOM");
    snprintf(buf, L, "loadlib: symbol not found: %s", err);
    Value r = V_str_from_c(buf); free(buf);
    return r;
  }

  InitFn fn = (InitFn)sym;

  /* 3) wrap as a CFunc Value */
  Value c; c.tag = VAL_CFUNC;
  c.as.cfunc = (CFunc)fn;  /* signatures match your VM expectation */
  return c;
}

/* context for clib search */
struct ClibAcc {
  const char *component;
  const char *initname;
  char *found;
};
static void clib_try_one(const char *templ, void *u) {
  struct ClibAcc *A = (struct ClibAcc*)u;
  char *cand = expand_template(templ, A->component);
  DLHandle h = dl_open(cand);
  if (h) {
    dl_close(h);
    if (!A->found) A->found = cand; else free(cand);
  } else {
    free(cand);
  }
}

/* forward for module loader */
static Value c_module_loader(VM *vm2, int cargc, Value *cargv);

/* C loader: called as searcher(name) */
static Value pkg_clib_searcher(VM *vm, int argc, Value *argv) {
  (void)argc;
  if (argc < 1 || argv[0].tag != VAL_STR) {
    return V_str_from_c("clib searcher: module name must be a string");
  }
  ensure_package_initialized(vm);

  const char *name = argv[0].as.s->data;

  /* init symbol: luaopen_<modname with dots replaced by underscores> */
  size_t nlen = strlen(name);
  size_t base = strlen(MODULE_INIT_PREFIX);
  char *initname = (char*)malloc(base + nlen + 1);
  memcpy(initname, MODULE_INIT_PREFIX, base);
  for (size_t i=0;i<nlen;i++){
    char c = name[i];
    initname[base + i] = (c=='.' ? '_' : c);
  }
  initname[base + nlen] = 0;

  /* Build path candidates from package.cpath */
  Value vcpath = get_field(g_pkg, "cpath");
  const char *cpath = (vcpath.tag == VAL_STR) ? vcpath.as.s->data : DFLT_C_PATH;

  char *component = module_name_to_path_component(name);
  struct ClibAcc acc = { component, initname, NULL };
  for_each_path(cpath, clib_try_one, &acc);
  free(component);

  if (!acc.found) {
    free(initname);
    return V_str_from_c("clib searcher: not found in package.cpath");
  }

  /* Return { loader_func, path, initname } (we’ll pass both to loader) */
  Value tup = V_table();
  Value loader; loader.tag = VAL_CFUNC; loader.as.cfunc = c_module_loader;
  tbl_set(tup.as.t, V_int(1), loader);
  tbl_set(tup.as.t, V_int(2), V_str_from_c(acc.found));
  tbl_set(tup.as.t, V_int(3), V_str_from_c(initname));

  free(acc.found);
  free(initname);
  return tup;
}

/* loader that calls package.loadlib(found, initname), then calls the cfunc */
static Value c_module_loader(VM *vm2, int cargc, Value *cargv) {
  if (cargc < 3 || cargv[0].tag != VAL_STR || cargv[1].tag != VAL_STR || cargv[2].tag != VAL_STR)
    return V_str_from_c("c module loader: expected (modname, path, initname)");

  const char *modname = cargv[0].as.s->data;
  const char *path    = cargv[1].as.s->data;
  const char *init    = cargv[2].as.s->data;

  /* call loadlib(path, initname) to get cfunc */
  Value loadlibV;
  Value pkg = (Value){ .tag = VAL_TABLE, .as.t = g_pkg };
  if (!tbl_get(pkg.as.t, V_str_from_c("loadlib"), &loadlibV) || loadlibV.tag != VAL_CFUNC)
    return V_str_from_c("c module loader: package.loadlib not available");

  Value args[2];
  args[0] = V_str_from_c(path);
  args[1] = V_str_from_c(init);

  Value cf = call_any_public(vm2, loadlibV, 2, args);
  if (cf.tag != VAL_CFUNC) return cf; /* likely an error string */

  /* Now call the cfunc as the module loader: cf(modname) */
  Value modnameV = V_str_from_c(modname);
  return call_any_public(vm2, cf, 1, &modnameV);
}

/* ===== require(name) ===== */

Value builtin_require(VM *vm, int argc, Value *argv) {
  if (argc < 1 || argv[0].tag != VAL_STR) {
    return V_str_from_c("require: module name must be a string");
  }
  Value modname = argv[0];
  ensure_package_initialized(vm);

  /* cache */
  Value loaded = get_or_create_table_field(g_pkg, "loaded");
  Value cached;
  if (tbl_get(loaded.as.t, modname, &cached)) {
    return cached;  /* return cached module (or true) */
  }

  /* iterate searchers */
  Value searchers = get_or_create_table_field(g_pkg, "searchers");
  long long idx = 1;
  Value messages = V_str_from_c(""); /* aggregate errors */
  while (1) {
    Value s;
    if (!tbl_get(searchers.as.t, V_int(idx), &s)) break;
    idx++;
    if (s.tag != VAL_CFUNC) continue;

    Value res = call_any_public(vm, s, 1, &modname);

    if (res.tag == VAL_TABLE) {
      /* { loader, extra1, extra2? } */
      Value loader, extra1, extra2;
      (void)tbl_get(res.as.t, V_int(1), &loader);
      (void)tbl_get(res.as.t, V_int(2), &extra1);
      (void)tbl_get(res.as.t, V_int(3), &extra2);
      if (loader.tag == VAL_CFUNC) {
        Value args[3]; int cargc = 1;
        args[0] = modname;
        if (extra1.tag != VAL_NIL) args[cargc++] = extra1;
        if (extra2.tag != VAL_NIL) args[cargc++] = extra2;

        Value module_val = call_any_public(vm, loader, cargc, args);

        /* If loader returns nil, store true */
        if (module_val.tag == VAL_NIL) {
          tbl_set(loaded.as.t, modname, V_bool(true));
          return V_bool(true);
        } else {
          tbl_set(loaded.as.t, modname, module_val);
          return module_val;
        }
      }
    } else if (res.tag == VAL_STR) {
      /* append message */
      const char *old = messages.as.s->data;
      const char *add = res.as.s->data;
      size_t L = strlen(old), A = strlen(add);
      char *buf = (char*)malloc(L + A + 2);
      memcpy(buf, old, L);
      buf[L] = '\n';
      memcpy(buf + L + 1, add, A + 1);
      messages = V_str_from_c(buf);
      free(buf);
    }
  }

  /* not found */
  {
    const char *prefix = "module not found: ";
    const char *name = modname.as.s->data;
    size_t P = strlen(prefix), N = strlen(name);
    size_t M = (messages.tag == VAL_STR) ? (size_t)messages.as.s->len : 0;
    char *buf = (char*)malloc(P + N + 2 + M + 1);
    memcpy(buf, prefix, P);
    memcpy(buf + P, name, N);
    buf[P+N] = '\n';
    if (M && messages.tag == VAL_STR) {
      memcpy(buf + P + N + 1, messages.as.s->data, M + 1);
    } else {
      buf[P+N+1] = 0;
    }
    Value err = V_str_from_c(buf);
    free(buf);
    vm_raise(vm, err);
    return V_nil();
  }
}

/* ===== init & public API ===== */

static void ensure_package_initialized(VM *vm) {
  if (g_pkg) return;

  Value pkgV = V_table();
  g_pkg = pkgV.as.t;

  /* seed fields */
  tbl_set(g_pkg, V_str_from_c("loaded"),    V_table());
  tbl_set(g_pkg, V_str_from_c("preload"),   V_table());
  tbl_set(g_pkg, V_str_from_c("searchers"), V_table());
  /* defaults (can be overridden by embedding app) */
  tbl_set(g_pkg, V_str_from_c("path"),  V_str_from_c(DFLT_LUA_PATH));
  tbl_set(g_pkg, V_str_from_c("cpath"), V_str_from_c(DFLT_C_PATH));

  /* install searchers: 1) preload, 2) lua files, 3) C libs */
  Value searchers; (void)tbl_get(g_pkg, V_str_from_c("searchers"), &searchers);
  Value s1; s1.tag = VAL_CFUNC; s1.as.cfunc = pkg_preload_searcher;
  Value s2; s2.tag = VAL_CFUNC; s2.as.cfunc = pkg_filesystem_searcher;
  Value s3; s3.tag = VAL_CFUNC; s3.as.cfunc = pkg_clib_searcher;
  push_array(searchers.as.t, s1);
  push_array(searchers.as.t, s2);
  push_array(searchers.as.t, s3);

  /* expose require and loadlib */
  Env *root = env_root(vm->env);
  Value req; req.tag = VAL_CFUNC; req.as.cfunc = builtin_require;
  env_add(root, "require", req, false);

  Value loadlibV; loadlibV.tag = VAL_CFUNC; loadlibV.as.cfunc = builtin_loadlib;
  tbl_set(g_pkg, V_str_from_c("loadlib"), loadlibV);

  /* expose package table */
  env_add(root, "package", (Value){ .tag = VAL_TABLE, .as.t = g_pkg }, false);
}

void register_package_lib(VM *vm) {
  ensure_package_initialized(vm);
}

/* Compat CFunc expected by some bootstrap code in interpreter.c */
Value builtin_package(VM *vm, int argc, Value *argv) {
  (void)argc; (void)argv;
  ensure_package_initialized(vm);
  return (Value){ .tag = VAL_TABLE, .as.t = g_pkg };
}
