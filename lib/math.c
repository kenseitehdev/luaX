
// lib/math.c
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include "../include/interpreter.h"

/* ---- helpers ---- */
static int is_num(Value v){ return v.tag==VAL_INT || v.tag==VAL_NUM; }
static double to_double(Value v){ return v.tag==VAL_INT ? (double)v.as.i : (v.tag==VAL_NUM ? v.as.n : 0.0); }
static long long to_ll(Value v){
  if (v.tag==VAL_INT) return v.as.i;
  if (v.tag==VAL_NUM) return (long long)v.as.n;
  return 0;
}
static Value ret_num_like(Value a, double d){
  /* if original was int and d is exactly integral, keep it int */
  if (a.tag==VAL_INT){
    long long ll = (long long)d;
    if ((double)ll == d) return V_int(ll);
  }
  return V_num(d);
}
static Value ret_maxmin_promote(int saw_float, double d){
  if (!saw_float){
    long long ll = (long long)d;
    if ((double)ll == d) return V_int(ll);
  }
  return V_num(d);
}

/* ---- math.abs(x) ---- */
static Value m_abs(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1 || !is_num(argv[0])) return V_nil();
  if (argv[0].tag==VAL_INT){
    long long x = argv[0].as.i;
    /* careful with LLONG_MIN */
    if (x==LLONG_MIN) return V_num(fabs((double)x));
    return V_int(x<0?-x:x);
  }
  return V_num(fabs(argv[0].as.n));
}

/* ---- ceil/floor ---- */
static Value m_ceil(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1 || !is_num(argv[0])) return V_nil();
  double d = ceil(to_double(argv[0]));
  return ret_num_like(argv[0], d);
}
static Value m_floor(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1 || !is_num(argv[0])) return V_nil();
  double d = floor(to_double(argv[0]));
  return ret_num_like(argv[0], d);
}

/* ---- max/min (variadic) ---- */
static Value m_max(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil();
  double best = -INFINITY; int init=0, saw_float=0;
  for(int i=0;i<argc;i++){
    if(!is_num(argv[i])) return V_nil();
    double d = to_double(argv[i]);
    if (!init || d>best){ best=d; init=1; }
    if (argv[i].tag==VAL_NUM) saw_float=1;
  }
  return ret_maxmin_promote(saw_float, best);
}
static Value m_min(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil();
  double best = INFINITY; int init=0, saw_float=0;
  for(int i=0;i<argc;i++){
    if(!is_num(argv[i])) return V_nil();
    double d = to_double(argv[i]);
    if (!init || d<best){ best=d; init=1; }
    if (argv[i].tag==VAL_NUM) saw_float=1;
  }
  return ret_maxmin_promote(saw_float, best);
}

/* ---- fmod(x,y) ---- */
static Value m_fmod(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<2 || !is_num(argv[0]) || !is_num(argv[1])) return V_nil();
  double r = fmod(to_double(argv[0]), to_double(argv[1]));
  return V_num(r);
}

/* ---- trig / exp / log / sqrt / pow ---- */
static Value m_sin (struct VM*vm,int argc,Value*argv){ (void)vm; if(argc<1||!is_num(argv[0]))return V_nil(); return V_num(sin (to_double(argv[0]))); }
static Value m_cos (struct VM*vm,int argc,Value*argv){ (void)vm; if(argc<1||!is_num(argv[0]))return V_nil(); return V_num(cos (to_double(argv[0]))); }
static Value m_tan (struct VM*vm,int argc,Value*argv){ (void)vm; if(argc<1||!is_num(argv[0]))return V_nil(); return V_num(tan (to_double(argv[0]))); }
static Value m_asin(struct VM*vm,int argc,Value*argv){ (void)vm; if(argc<1||!is_num(argv[0]))return V_nil(); return V_num(asin(to_double(argv[0]))); }
static Value m_acos(struct VM*vm,int argc,Value*argv){ (void)vm; if(argc<1||!is_num(argv[0]))return V_nil(); return V_num(acos(to_double(argv[0]))); }
static Value m_atan(struct VM*vm,int argc,Value*argv){
  (void)vm;
  if(argc<1||!is_num(argv[0])) return V_nil();
  if(argc==1) return V_num(atan(to_double(argv[0])));
  if(!is_num(argv[1])) return V_nil();
  return V_num(atan2(to_double(argv[0]), to_double(argv[1]))); /* atan(y,x) */
}
static Value m_exp (struct VM*vm,int argc,Value*argv){ (void)vm; if(argc<1||!is_num(argv[0]))return V_nil(); return V_num(exp (to_double(argv[0]))); }
static Value m_log (struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1||!is_num(argv[0])) return V_nil();
  double x = to_double(argv[0]);
  if(argc>=2 && is_num(argv[1])){
    double base = to_double(argv[1]);
    return V_num(log(x)/log(base));
  }
  return V_num(log(x)); /* natural log */
}
static Value m_sqrt(struct VM*vm,int argc,Value*argv){ (void)vm; if(argc<1||!is_num(argv[0]))return V_nil(); return V_num(sqrt(to_double(argv[0]))); }
static Value m_pow (struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<2||!is_num(argv[0])||!is_num(argv[1]))return V_nil();
  return V_num(pow(to_double(argv[0]), to_double(argv[1])));
}

/* ---- deg/rad ---- */
static Value m_deg(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1||!is_num(argv[0])) return V_nil();
  return V_num(to_double(argv[0]) * (180.0/M_PI));
}
static Value m_rad(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1||!is_num(argv[0])) return V_nil();
  return V_num(to_double(argv[0]) * (M_PI/180.0));
}

/* ---- modf(x) -> {intpart, fracpart} ---- */
static Value m_modf(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1||!is_num(argv[0])) return V_nil();
  double x = to_double(argv[0]);
  double ip; double fp = modf(x, &ip);
  Value t = V_table();
  tbl_set_public(t.as.t, V_int(1), ret_num_like(argv[0], ip));
  tbl_set_public(t.as.t, V_int(2), V_num(fp));
  return t;
}

/* ---- random / randomseed ----
   Lua semantics:
   - random() -> float in [0,1)
   - random(n) -> integer 1..n
   - random(m,n) -> integer m..n
*/
static Value m_random(struct VM*vm,int argc,Value*argv){
  (void)vm;
  if (argc==0){
    double r = (double)rand() / ((double)RAND_MAX + 1.0);
    return V_num(r);
  } else if (argc==1 && is_num(argv[0])){
    long long n = to_ll(argv[0]);
    if (n<=0) return V_nil();
    long long r = (long long)((double)n * ((double)rand()/((double)RAND_MAX+1.0))) + 1;
    if (r<1) r=1; if (r>n) r=n;
    return V_int(r);
  } else if (argc>=2 && is_num(argv[0]) && is_num(argv[1])){
    long long m = to_ll(argv[0]), n = to_ll(argv[1]);
    if (m>n) { long long tmp=m; m=n; n=tmp; }
    long long span = n - m + 1;
    if (span<=0) return V_nil();
    long long r = m + (long long)((double)span * ((double)rand()/((double)RAND_MAX+1.0)));
    if (r<m) r=m; if (r>n) r=n;
    return V_int(r);
  }
  return V_nil();
}
static Value m_randomseed(struct VM*vm,int argc,Value*argv){
  (void)vm;
  unsigned int seed = (unsigned int)time(NULL);
  if (argc>=1 && is_num(argv[0])) seed = (unsigned int)to_ll(argv[0]);
  srand(seed);
  return V_int((long long)seed);
}

/* ---- tointeger / type / ult ---- */
static Value m_tointeger(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1 || !is_num(argv[0])) return V_nil();
  double d = to_double(argv[0]);
  long long ll = (long long)d;
  if ((double)ll == d) return V_int(ll);
  return V_nil();
}
static Value m_type(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil();
  if (argv[0].tag==VAL_INT) return V_str_from_c("integer");
  if (argv[0].tag==VAL_NUM) return V_str_from_c("float");
  return V_nil();
}
static Value m_ult(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<2 || argv[0].tag!=VAL_INT || argv[1].tag!=VAL_INT) return V_nil();
  /* unsigned less-than on integers */
  unsigned long long a = (unsigned long long)argv[0].as.i;
  unsigned long long b = (unsigned long long)argv[1].as.i;
  return V_bool(a < b);
}

/* ---- registration ---- */
void register_math_lib(struct VM *vm){
  Value t = V_table();

  /* constants */
  tbl_set_public(t.as.t, V_str_from_c("pi"),          V_num(M_PI));
  tbl_set_public(t.as.t, V_str_from_c("huge"),        V_num(HUGE_VAL));
  tbl_set_public(t.as.t, V_str_from_c("maxinteger"),  V_int(LLONG_MAX));
  tbl_set_public(t.as.t, V_str_from_c("mininteger"),  V_int(LLONG_MIN));

  /* functions */
  tbl_set_public(t.as.t, V_str_from_c("abs"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_abs});
  tbl_set_public(t.as.t, V_str_from_c("ceil"),       (Value){.tag=VAL_CFUNC,.as.cfunc=m_ceil});
  tbl_set_public(t.as.t, V_str_from_c("floor"),      (Value){.tag=VAL_CFUNC,.as.cfunc=m_floor});
  tbl_set_public(t.as.t, V_str_from_c("max"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_max});
  tbl_set_public(t.as.t, V_str_from_c("min"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_min});
  tbl_set_public(t.as.t, V_str_from_c("fmod"),       (Value){.tag=VAL_CFUNC,.as.cfunc=m_fmod});
  tbl_set_public(t.as.t, V_str_from_c("sin"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_sin});
  tbl_set_public(t.as.t, V_str_from_c("cos"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_cos});
  tbl_set_public(t.as.t, V_str_from_c("tan"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_tan});
  tbl_set_public(t.as.t, V_str_from_c("asin"),       (Value){.tag=VAL_CFUNC,.as.cfunc=m_asin});
  tbl_set_public(t.as.t, V_str_from_c("acos"),       (Value){.tag=VAL_CFUNC,.as.cfunc=m_acos});
  tbl_set_public(t.as.t, V_str_from_c("atan"),       (Value){.tag=VAL_CFUNC,.as.cfunc=m_atan});
  tbl_set_public(t.as.t, V_str_from_c("exp"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_exp});
  tbl_set_public(t.as.t, V_str_from_c("log"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_log});
  tbl_set_public(t.as.t, V_str_from_c("sqrt"),       (Value){.tag=VAL_CFUNC,.as.cfunc=m_sqrt});
  tbl_set_public(t.as.t, V_str_from_c("pow"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_pow});
  tbl_set_public(t.as.t, V_str_from_c("deg"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_deg});
  tbl_set_public(t.as.t, V_str_from_c("rad"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_rad});
  tbl_set_public(t.as.t, V_str_from_c("modf"),       (Value){.tag=VAL_CFUNC,.as.cfunc=m_modf});
  tbl_set_public(t.as.t, V_str_from_c("random"),     (Value){.tag=VAL_CFUNC,.as.cfunc=m_random});
  tbl_set_public(t.as.t, V_str_from_c("randomseed"), (Value){.tag=VAL_CFUNC,.as.cfunc=m_randomseed});
  tbl_set_public(t.as.t, V_str_from_c("tointeger"),  (Value){.tag=VAL_CFUNC,.as.cfunc=m_tointeger});
  tbl_set_public(t.as.t, V_str_from_c("type"),       (Value){.tag=VAL_CFUNC,.as.cfunc=m_type});
  tbl_set_public(t.as.t, V_str_from_c("ult"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_ult});

  env_add_public(vm->env, "math", t, false);
}
