// lib/math.c
#define _USE_MATH_DEFINES
#define _GNU_SOURCE
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
static int is_integral_double(double d){ long long ll=(long long)d; return ((double)ll==d); }
static Value ret_num_like(Value a, double d){
  if (a.tag==VAL_INT){ long long ll=(long long)d; if ((double)ll==d) return V_int(ll); }
  return V_num(d);
}
static Value ret_num_if_int(double d){
  long long ll=(long long)d; if ((double)ll==d) return V_int(ll); return V_num(d);
}

/* ---- complex representation ---- */
int tbl_get_public(struct Table *t, Value key, Value *out); /* fwd from header */
void tbl_set_public(struct Table *t, Value key, Value val);

static int is_complex(Value v, double *re, double *im){
  if (v.tag!=VAL_TABLE) return 0;
  Value vre, vim;
  int ok1 = tbl_get_public(v.as.t, V_str_from_c("re"), &vre) && is_num(vre);
  int ok2 = tbl_get_public(v.as.t, V_str_from_c("im"), &vim) && is_num(vim);
  if (!(ok1 && ok2)) return 0;
  if (re) *re = to_double(vre);
  if (im) *im = to_double(vim);
  return 1;
}
static Value make_complex(double re, double im){
  Value t = V_table();
  tbl_set_public(t.as.t, V_str_from_c("re"), V_num(re));
  tbl_set_public(t.as.t, V_str_from_c("im"), V_num(im));
  return t;
}
static Value to_complex(Value v){
  double re, im;
  if (is_complex(v, &re, &im)) return v;
  if (is_num(v)) return make_complex(to_double(v), 0.0);
  return V_nil();
}

/* complex ops (internal) */
static double c_abs(double a, double b){ return hypot(a,b); }
static double c_arg(double a, double b){ return atan2(b,a); }
static void   c_add(double a1,double b1,double a2,double b2,double *ro,double *io){ *ro=a1+a2; *io=b1+b2; }
static void   c_sub(double a1,double b1,double a2,double b2,double *ro,double *io){ *ro=a1-a2; *io=b1-b2; }
static void   c_mul(double a,double b,double c,double d,double *ro,double *io){ *ro = a*c - b*d; *io = a*d + b*c; }
static void   c_div(double a,double b,double c,double d,double *ro,double *io){
  double den = c*c + d*d; *ro = (a*c + b*d)/den; *io = (b*c - a*d)/den;
}
static void   c_exp(double a,double b,double *ro,double *io){
  double ea = exp(a); *ro = ea*cos(b); *io = ea*sin(b);
}
static int    c_log(double a,double b,double *ro,double *io){
  double r = c_abs(a,b); if (r==0.0) return 0;
  *ro = log(r); *io = c_arg(a,b); return 1;
}
static int    c_pow(double ar,double ai,double br,double bi,double *ro,double *io){
  /* z^w = exp(w * Log z); undefined for z==0 with non-positive Re(w) or w==0 & z==0 */
  if (ar==0.0 && ai==0.0){
    if (br==0.0 && bi==0.0) return 0; /* 0^0 */
    if (br<=0.0 && bi==0.0) return 0; /* 0^negative */
    /* 0^positive -> 0 */
    *ro = 0.0; *io = 0.0; return 1;
  }
  double lr, li; if (!c_log(ar,ai,&lr,&li)) return 0;
  /* w*Log(z) */
  double xr = br*lr - bi*li;
  double xi = br*li + bi*lr;
  c_exp(xr, xi, ro, io);
  return 1;
}
static void   c_sqrt(double a,double b,double *ro,double *io){
  /* principal sqrt */
  double r = c_abs(a,b);
  double t = sqrt((r + fabs(a))/2.0);
  double u = (t==0.0) ? 0.0 : (b/(2.0*t));
  if (a>=0){ *ro = t; *io = u; }
  else     { *ro = fabs(u); *io = (b>=0? t : -t); }
}

/* ---- math.abs(x) ---- */
static Value m_abs(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil();
  double re, im;
  if (is_complex(argv[0], &re, &im)) return V_num(c_abs(re,im));
  if(!is_num(argv[0])) return V_nil();
  if (argv[0].tag==VAL_INT){
    long long x = argv[0].as.i;
    if (x==LLONG_MIN) return V_num(fabs((double)x));
    return V_int(x<0?-x:x);
  }
  return V_num(fabs(argv[0].as.n));
}

/* ---- ceil/floor ---- */
static Value m_ceil(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1 || !is_num(argv[0])) return V_nil();
  double d = ceil(to_double(argv[0])); return ret_num_like(argv[0], d);
}
static Value m_floor(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1 || !is_num(argv[0])) return V_nil();
  double d = floor(to_double(argv[0])); return ret_num_like(argv[0], d);
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
  return ret_num_if_int(best);
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
  return ret_num_if_int(best);
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
  if(argc<1) return V_nil();
  if(argc==1 && is_num(argv[0])) return V_num(atan(to_double(argv[0])));
  if(argc>=2 && is_num(argv[0]) && is_num(argv[1])) return V_num(atan2(to_double(argv[0]), to_double(argv[1])));
  return V_nil();
}
static Value m_exp (struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil();
  double re,im;
  if (is_complex(argv[0],&re,&im)){ double rr,ii; c_exp(re,im,&rr,&ii); return make_complex(rr,ii); }
  if (!is_num(argv[0])) return V_nil();
  return V_num(exp(to_double(argv[0])));
}

/* ---- ln/log (real or complex) ---- */
static Value m_ln(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil();
  double re,im;
  if (is_complex(argv[0],&re,&im)){
    double rr,ii; if(!c_log(re,im,&rr,&ii)) return V_nil(); return make_complex(rr,ii);
  }
  if (!is_num(argv[0])) return V_nil();
  return V_num(log(to_double(argv[0])));
}
static Value m_log (struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil();
  if (argc>=2){
    /* log(x, base) = ln(x)/ln(base) ; supports complex */
    Value a = m_ln(vm,1,argv);
    Value b = m_ln(vm,1,&argv[1]);
    double ar,ai, br,bi;
    if (is_complex(a,&ar,&ai) || is_complex(b,&br,&bi)){
      if (!is_complex(a,&ar,&ai)){ ar=to_double(a); ai=0.0; }
      if (!is_complex(b,&br,&bi)){ br=to_double(b); bi=0.0; }
      double rr,ii; c_div(ar,ai,br,bi,&rr,&ii); return make_complex(rr,ii);
    }
    if (!is_num(a) || !is_num(b)) return V_nil();
    return V_num(to_double(a)/to_double(b));
  }
  return m_ln(vm,1,argv);
}

/* ---- sqrt (principal) ---- */
static Value m_sqrt(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil();
  double re,im;
  if (is_complex(argv[0],&re,&im)){ double rr,ii; c_sqrt(re,im,&rr,&ii); return make_complex(rr,ii); }
  if (!is_num(argv[0])) return V_nil();
  double x = to_double(argv[0]);
  if (x>=0) return V_num(sqrt(x));
  { double rr,ii; c_sqrt(x,0.0,&rr,&ii); return make_complex(rr,ii); }
}

/* ---- pow (real/complex base/exponent) ---- */
static Value m_pow (struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<2) return V_nil();
  double ar,ai=0.0, br,bi=0.0; int ac=is_complex(argv[0],&ar,&ai); int bc=is_complex(argv[1],&br,&bi);
  if (!ac && is_num(argv[0])){ ar=to_double(argv[0]); ai=0.0; ac=1; }
  if (!bc && is_num(argv[1])){ br=to_double(argv[1]); bi=0.0; bc=1; }
  if (!(ac && bc)) return V_nil();
  double rr,ii; if(!c_pow(ar,ai,br,bi,&rr,&ii)) return V_nil();
  if (ii==0.0) return ret_num_if_int(rr);
  return make_complex(rr,ii);
}

/* ---- cbrt and variable root ---- */
static Value m_cbrt(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil();
  double re,im;
  if (is_complex(argv[0],&re,&im)){
    /* principal using pow(z, 1/3) */
    Value one = V_num(1.0), three = V_num(3.0);
    Value frac[2] = { one, three };
    Value inv3 = m_fmod(vm,0,NULL); (void)inv3; /* silence static analyzers */
    double rr,ii; if(!c_pow(re,im, 1.0/3.0, 0.0, &rr,&ii)) return V_nil();
    return make_complex(rr,ii);
  }
  if (!is_num(argv[0])) return V_nil();
  double x = to_double(argv[0]);
  double r = pow(fabs(x), 1.0/3.0); if (x<0) r=-r; return ret_num_like(argv[0], r);
}

/* root(x, n): principal n-th root; n may be integer (±) or real. For complex x, principal branch. */
static Value m_root(struct VM*vm,int argc,Value*argv){
  (void)vm; if (argc<2) return V_nil();
  /* parse x */
  double xr, xi=0.0; int xc = is_complex(argv[0], &xr,&xi);
  if (!xc){
    if (!is_num(argv[0])) return V_nil();
    xr = to_double(argv[0]); xi = 0.0; xc = 1;
  }
  /* parse n (real only here) */
  if (!is_num(argv[1])) return V_nil();
  double nd = to_double(argv[1]); if (nd==0.0) return V_nil();
  /* principal via pow(z, 1/n) */
  double rr,ii; if(!c_pow(xr,xi, 1.0/nd, 0.0, &rr,&ii)) return V_nil();
  if (ii==0.0) return ret_num_if_int(rr);
  return make_complex(rr,ii);
}

/* roots(x, n): all integer n-th roots as array [1..n] (principal ordering). n>=1 integer. */
static Value m_roots(struct VM*vm,int argc,Value*argv){
  (void)vm; if (argc<2) return V_nil();
  /* x */
  double xr, xi=0.0; int xc = is_complex(argv[0], &xr,&xi);
  if (!xc){
    if (!is_num(argv[0])) return V_nil();
    xr = to_double(argv[0]); xi = 0.0; xc = 1;
  }
  /* n */
  if (!is_num(argv[1])) return V_nil();
  double nd = to_double(argv[1]); if (!is_integral_double(nd) || nd<1.0) return V_nil();
  long long n = (long long)nd;
  double r = c_abs(xr,xi), theta = c_arg(xr,xi);
  double root_r = pow(r, 1.0/(double)n);
  Value arr = V_table(); long long idx=1;
  for (long long k=0;k<n;k++){
    double ang = (theta + 2.0*M_PI*(double)k) / (double)n;
    double re = root_r * cos(ang);
    double im = root_r * sin(ang);
    tbl_set_public(arr.as.t, V_int(idx++), make_complex(re,im));
  }
  return arr;
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

/* ---- random / randomseed ---- */
static Value m_random(struct VM*vm,int argc,Value*argv){
  (void)vm;
  if (argc==0){ double r=(double)rand()/((double)RAND_MAX+1.0); return V_num(r); }
  if (argc==1 && is_num(argv[0])){
    long long n=to_ll(argv[0]); if (n<=0) return V_nil();
    long long r = (long long)((double)n * ((double)rand()/((double)RAND_MAX+1.0))) + 1;
    if (r<1) r=1; if (r>n) r=n; return V_int(r);
  }
  if (argc>=2 && is_num(argv[0]) && is_num(argv[1])){
    long long m=to_ll(argv[0]), n=to_ll(argv[1]); if (m>n){ long long t=m; m=n; n=t; }
    long long span = n-m+1; if (span<=0) return V_nil();
    long long r = m + (long long)((double)span * ((double)rand()/((double)RAND_MAX+1.0)));
    if (r<m) r=m; if (r>n) r=n; return V_int(r);
  }
  return V_nil();
}
static Value m_randomseed(struct VM*vm,int argc,Value*argv){
  (void)vm; unsigned int seed=(unsigned int)time(NULL);
  if(argc>=1 && is_num(argv[0])) seed=(unsigned int)to_ll(argv[0]);
  srand(seed); return V_int((long long)seed);
}

/* ---- tointeger / type / ult ---- */
static Value m_tointeger(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1 || !is_num(argv[0])) return V_nil();
  double d=to_double(argv[0]); long long ll=(long long)d; if((double)ll==d) return V_int(ll); return V_nil();
}
static Value m_type(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil();
  if (argv[0].tag==VAL_INT) return V_str_from_c("integer");
  if (argv[0].tag==VAL_NUM) return V_str_from_c("float");
  if (is_complex(argv[0],NULL,NULL)) return V_str_from_c("complex");
  return V_nil();
}
static Value m_ult(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<2 || argv[0].tag!=VAL_INT || argv[1].tag!=VAL_INT) return V_nil();
  unsigned long long a=(unsigned long long)argv[0].as.i, b=(unsigned long long)argv[1].as.i;
  return V_bool(a<b);
}

/* ---- complex API ---- */
static Value m_complex(struct VM*vm,int argc,Value*argv){
  (void)vm; if (argc<1) return V_nil();
  double re = is_num(argv[0]) ? to_double(argv[0]) : 0.0;
  double im = (argc>=2 && is_num(argv[1])) ? to_double(argv[1]) : 0.0;
  return make_complex(re,im);
}
static Value m_iscomplex(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_bool(false);
  return V_bool(is_complex(argv[0],NULL,NULL));
}
static Value m_creal(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil(); double re,im; if (!is_complex(argv[0],&re,&im)) return V_nil(); return V_num(re);
}
static Value m_cimag(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil(); double re,im; if (!is_complex(argv[0],&re,&im)) return V_nil(); return V_num(im);
}
static Value m_conj(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil(); double re,im; if (!is_complex(argv[0],&re,&im)) return V_nil(); return make_complex(re,-im);
}
static Value m_arg(struct VM*vm,int argc,Value*argv){
  (void)vm; if(argc<1) return V_nil(); double re,im; if (!is_complex(argv[0],&re,&im)) return V_nil(); return V_num(c_arg(re,im));
}

/* ---- registration ---- */
void register_math_lib(struct VM *vm){
  Value t = V_table();

  /* constants */
  tbl_set_public(t.as.t, V_str_from_c("pi"),          V_num(M_PI));
  tbl_set_public(t.as.t, V_str_from_c("e"),           V_num(M_E));
  tbl_set_public(t.as.t, V_str_from_c("huge"),        V_num(HUGE_VAL));
  tbl_set_public(t.as.t, V_str_from_c("infinity"),    V_num(INFINITY));
  tbl_set_public(t.as.t, V_str_from_c("maxinteger"),  V_int(LLONG_MAX));
  tbl_set_public(t.as.t, V_str_from_c("mininteger"),  V_int(LLONG_MIN));

  /* real + complex functions */
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
  tbl_set_public(t.as.t, V_str_from_c("ln"),         (Value){.tag=VAL_CFUNC,.as.cfunc=m_ln});
  tbl_set_public(t.as.t, V_str_from_c("log"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_log});
  tbl_set_public(t.as.t, V_str_from_c("sqrt"),       (Value){.tag=VAL_CFUNC,.as.cfunc=m_sqrt});
  tbl_set_public(t.as.t, V_str_from_c("pow"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_pow});

  /* new: roots & complex helpers */
  tbl_set_public(t.as.t, V_str_from_c("cbrt"),       (Value){.tag=VAL_CFUNC,.as.cfunc=m_cbrt});
  tbl_set_public(t.as.t, V_str_from_c("root"),       (Value){.tag=VAL_CFUNC,.as.cfunc=m_root});
  tbl_set_public(t.as.t, V_str_from_c("roots"),      (Value){.tag=VAL_CFUNC,.as.cfunc=m_roots});

  tbl_set_public(t.as.t, V_str_from_c("complex"),    (Value){.tag=VAL_CFUNC,.as.cfunc=m_complex});
  tbl_set_public(t.as.t, V_str_from_c("iscomplex"),  (Value){.tag=VAL_CFUNC,.as.cfunc=m_iscomplex});
  tbl_set_public(t.as.t, V_str_from_c("creal"),      (Value){.tag=VAL_CFUNC,.as.cfunc=m_creal});
  tbl_set_public(t.as.t, V_str_from_c("cimag"),      (Value){.tag=VAL_CFUNC,.as.cfunc=m_cimag});
  tbl_set_public(t.as.t, V_str_from_c("conj"),       (Value){.tag=VAL_CFUNC,.as.cfunc=m_conj});
  tbl_set_public(t.as.t, V_str_from_c("arg"),        (Value){.tag=VAL_CFUNC,.as.cfunc=m_arg});

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
