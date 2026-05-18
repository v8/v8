// The following is adapted from fdlibm (http://www.netlib.org/fdlibm).
//
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
//
// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2016 the V8 project authors. All rights reserved.

#include "src/base/ieee754.h"

#include <cmath>
#include <limits>

#include "src/base/build_config.h"
#include "src/base/macros.h"
#include "src/base/overflowing-math.h"
#include "third_party/llvm-libc/src/shared/math.h"

namespace v8 {
namespace base {
namespace ieee754 {

/*
 * The original fdlibm code used statements like:
 *  n0 = ((*(int*)&one)>>29)^1;   * index of high word *
 *  ix0 = *(n0+(int*)&x);     * high word of x *
 *  ix1 = *((1-n0)+(int*)&x);   * low word of x *
 * to dig two 32 bit words out of the 64 bit IEEE floating point
 * value.  That is non-ANSI, and, moreover, the gcc instruction
 * scheduler gets it wrong.  We instead use the following macros.
 * Unlike the original code, we determine the endianness at compile
 * time, not at run time; I don't see much benefit to selecting
 * endianness at run time.
 */

/* Get two 32 bit ints from a double.  */

#define EXTRACT_WORDS(ix0, ix1, d)               \
  do {                                           \
    uint64_t bits = base::bit_cast<uint64_t>(d); \
    (ix0) = bits >> 32;                          \
    (ix1) = bits & 0xFFFFFFFFu;                  \
  } while (false)

/* Get the more significant 32 bit int from a double.  */

#define GET_HIGH_WORD(i, d)                      \
  do {                                           \
    uint64_t bits = base::bit_cast<uint64_t>(d); \
    (i) = bits >> 32;                            \
  } while (false)

/* Get the less significant 32 bit int from a double.  */

#define GET_LOW_WORD(i, d)                       \
  do {                                           \
    uint64_t bits = base::bit_cast<uint64_t>(d); \
    (i) = bits & 0xFFFFFFFFu;                    \
  } while (false)

/* Set a double from two 32 bit ints.  */

#define INSERT_WORDS(d, ix0, ix1)             \
  do {                                        \
    uint64_t bits = 0;                        \
    bits |= static_cast<uint64_t>(ix0) << 32; \
    bits |= static_cast<uint32_t>(ix1);       \
    (d) = base::bit_cast<double>(bits);       \
  } while (false)

/* Set the more significant 32 bits of a double from an int.  */

#define SET_HIGH_WORD(d, v)                      \
  do {                                           \
    uint64_t bits = base::bit_cast<uint64_t>(d); \
    bits &= 0x0000'0000'FFFF'FFFF;               \
    bits |= static_cast<uint64_t>(v) << 32;      \
    (d) = base::bit_cast<double>(bits);          \
  } while (false)

/* Set the less significant 32 bits of a double from an int.  */

#define SET_LOW_WORD(d, v)                       \
  do {                                           \
    uint64_t bits = base::bit_cast<uint64_t>(d); \
    bits &= 0xFFFF'FFFF'0000'0000;               \
    bits |= static_cast<uint32_t>(v);            \
    (d) = base::bit_cast<double>(bits);          \
  } while (false)

double acos(double x) { return LIBC_NAMESPACE::shared::acos(x); }

/* acosh(x)
 * Method :
 *      Based on
 *              acosh(x) = log [ x + sqrt(x*x-1) ]
 *      we have
 *              acosh(x) := log(x)+ln2, if x is large; else
 *              acosh(x) := log(2x-1/(sqrt(x*x-1)+x)) if x>2; else
 *              acosh(x) := log1p(t+sqrt(2.0*t+t*t)); where t=x-1.
 *
 * Special cases:
 *      acosh(x) is NaN with signal if x<1.
 *      acosh(NaN) is NaN without signal.
 */
double acosh(double x) {
  static const double
      one = 1.0,
      ln2 = 6.93147180559945286227e-01; /* 0x3FE62E42, 0xFEFA39EF */
  double t;
  int32_t hx;
  uint32_t lx;
  EXTRACT_WORDS(hx, lx, x);
  if (hx < 0x3FF00000) { /* x < 1 */
    return std::numeric_limits<double>::signaling_NaN();
  } else if (hx >= 0x41B00000) { /* x > 2**28 */
    if (hx >= 0x7FF00000) {      /* x is inf of NaN */
      return x + x;
    } else {
      return log(x) + ln2; /* acosh(huge)=log(2x) */
    }
  } else if (((hx - 0x3FF00000) | lx) == 0) {
    return 0.0;                 /* acosh(1) = 0 */
  } else if (hx > 0x40000000) { /* 2**28 > x > 2 */
    t = x * x;
    return log(2.0 * x - one / (x + sqrt(t - one)));
  } else { /* 1<x<2 */
    t = x - one;
    return log1p(t + sqrt(2.0 * t + t * t));
  }
}

double asin(double x) { return LIBC_NAMESPACE::shared::asin(x); }

/* asinh(x)
 * Method :
 *      Based on
 *              asinh(x) = sign(x) * log [ |x| + sqrt(x*x+1) ]
 *      we have
 *      asinh(x) := x  if  1+x*x=1,
 *               := sign(x)*(log(x)+ln2)) for large |x|, else
 *               := sign(x)*log(2|x|+1/(|x|+sqrt(x*x+1))) if|x|>2, else
 *               := sign(x)*log1p(|x| + x^2/(1 + sqrt(1+x^2)))
 */
double asinh(double x) {
  static const double
      one = 1.00000000000000000000e+00, /* 0x3FF00000, 0x00000000 */
      ln2 = 6.93147180559945286227e-01, /* 0x3FE62E42, 0xFEFA39EF */
      huge = 1.00000000000000000000e+300;

  double t, w;
  int32_t hx, ix;
  GET_HIGH_WORD(hx, x);
  ix = hx & 0x7FFFFFFF;
  if (ix >= 0x7FF00000) return x + x; /* x is inf or NaN */
  if (ix < 0x3E300000) {              /* |x|<2**-28 */
    if (huge + x > one) return x;     /* return x inexact except 0 */
  }
  if (ix > 0x41B00000) { /* |x| > 2**28 */
    w = log(fabs(x)) + ln2;
  } else if (ix > 0x40000000) { /* 2**28 > |x| > 2.0 */
    t = fabs(x);
    w = log(2.0 * t + one / (sqrt(x * x + one) + t));
  } else { /* 2.0 > |x| > 2**-28 */
    t = x * x;
    w = log1p(fabs(x) + t / (one + sqrt(one + t)));
  }
  if (hx > 0) {
    return w;
  } else {
    return -w;
  }
}

/* atan(x)
 * Method
 *   1. Reduce x to positive by atan(x) = -atan(-x).
 *   2. According to the integer k=4t+0.25 chopped, t=x, the argument
 *      is further reduced to one of the following intervals and the
 *      arctangent of t is evaluated by the corresponding formula:
 *
 *      [0,7/16]      atan(x) = t-t^3*(a1+t^2*(a2+...(a10+t^2*a11)...)
 *      [7/16,11/16]  atan(x) = atan(1/2) + atan( (t-0.5)/(1+t/2) )
 *      [11/16.19/16] atan(x) = atan( 1 ) + atan( (t-1)/(1+t) )
 *      [19/16,39/16] atan(x) = atan(3/2) + atan( (t-1.5)/(1+1.5t) )
 *      [39/16,INF]   atan(x) = atan(INF) + atan( -1/t )
 *
 * Constants:
 * The hexadecimal values are the intended ones for the following
 * constants. The decimal values may be used, provided that the
 * compiler will convert from decimal to binary accurately enough
 * to produce the hexadecimal values shown.
 */
double atan(double x) {
  static const double atanhi[] = {
      4.63647609000806093515e-01, /* atan(0.5)hi 0x3FDDAC67, 0x0561BB4F */
      7.85398163397448278999e-01, /* atan(1.0)hi 0x3FE921FB, 0x54442D18 */
      9.82793723247329054082e-01, /* atan(1.5)hi 0x3FEF730B, 0xD281F69B */
      1.57079632679489655800e+00, /* atan(inf)hi 0x3FF921FB, 0x54442D18 */
  };

  static const double atanlo[] = {
      2.26987774529616870924e-17, /* atan(0.5)lo 0x3C7A2B7F, 0x222F65E2 */
      3.06161699786838301793e-17, /* atan(1.0)lo 0x3C81A626, 0x33145C07 */
      1.39033110312309984516e-17, /* atan(1.5)lo 0x3C700788, 0x7AF0CBBD */
      6.12323399573676603587e-17, /* atan(inf)lo 0x3C91A626, 0x33145C07 */
  };

  static const double aT[] = {
      3.33333333333329318027e-01,  /* 0x3FD55555, 0x5555550D */
      -1.99999999998764832476e-01, /* 0xBFC99999, 0x9998EBC4 */
      1.42857142725034663711e-01,  /* 0x3FC24924, 0x920083FF */
      -1.11111104054623557880e-01, /* 0xBFBC71C6, 0xFE231671 */
      9.09088713343650656196e-02,  /* 0x3FB745CD, 0xC54C206E */
      -7.69187620504482999495e-02, /* 0xBFB3B0F2, 0xAF749A6D */
      6.66107313738753120669e-02,  /* 0x3FB10D66, 0xA0D03D51 */
      -5.83357013379057348645e-02, /* 0xBFADDE2D, 0x52DEFD9A */
      4.97687799461593236017e-02,  /* 0x3FA97B4B, 0x24760DEB */
      -3.65315727442169155270e-02, /* 0xBFA2B444, 0x2C6A6C2F */
      1.62858201153657823623e-02,  /* 0x3F90AD3A, 0xE322DA11 */
  };

  static const double one = 1.0, huge = 1.0e300;

  double w, s1, s2, z;
  int32_t ix, hx, id;

  GET_HIGH_WORD(hx, x);
  ix = hx & 0x7FFFFFFF;
  if (ix >= 0x44100000) { /* if |x| >= 2^66 */
    uint32_t low;
    GET_LOW_WORD(low, x);
    if (ix > 0x7FF00000 || (ix == 0x7FF00000 && (low != 0))) {
      return x + x; /* NaN */
    }
    if (hx > 0) {
      return atanhi[3] + *const_cast<volatile double*>(&atanlo[3]);
    } else {
      return -atanhi[3] - *const_cast<volatile double*>(&atanlo[3]);
    }
  }
  if (ix < 0x3FDC0000) {            /* |x| < 0.4375 */
    if (ix < 0x3E400000) {          /* |x| < 2^-27 */
      if (huge + x > one) return x; /* raise inexact */
    }
    id = -1;
  } else {
    x = fabs(x);
    if (ix < 0x3FF30000) {   /* |x| < 1.1875 */
      if (ix < 0x3FE60000) { /* 7/16 <=|x|<11/16 */
        id = 0;
        x = (2.0 * x - one) / (2.0 + x);
      } else { /* 11/16<=|x|< 19/16 */
        id = 1;
        x = (x - one) / (x + one);
      }
    } else {
      if (ix < 0x40038000) { /* |x| < 2.4375 */
        id = 2;
        x = (x - 1.5) / (one + 1.5 * x);
      } else { /* 2.4375 <= |x| < 2^66 */
        id = 3;
        x = -1.0 / x;
      }
    }
  }
  /* end of argument reduction */
  z = x * x;
  w = z * z;
  /* break sum from i=0 to 10 aT[i]z**(i+1) into odd and even poly */
  s1 = z * (aT[0] +
            w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
  s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
  if (id < 0) {
    return x - x * (s1 + s2);
  } else {
    z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
    return (hx < 0) ? -z : z;
  }
}

/* atan2(y,x)
 * Method :
 *  1. Reduce y to positive by atan2(y,x)=-atan2(-y,x).
 *  2. Reduce x to positive by (if x and y are unexceptional):
 *    ARG (x+iy) = arctan(y/x)       ... if x > 0,
 *    ARG (x+iy) = pi - arctan[y/(-x)]   ... if x < 0,
 *
 * Special cases:
 *
 *  ATAN2((anything), NaN ) is NaN;
 *  ATAN2(NAN , (anything) ) is NaN;
 *  ATAN2(+-0, +(anything but NaN)) is +-0  ;
 *  ATAN2(+-0, -(anything but NaN)) is +-pi ;
 *  ATAN2(+-(anything but 0 and NaN), 0) is +-pi/2;
 *  ATAN2(+-(anything but INF and NaN), +INF) is +-0 ;
 *  ATAN2(+-(anything but INF and NaN), -INF) is +-pi;
 *  ATAN2(+-INF,+INF ) is +-pi/4 ;
 *  ATAN2(+-INF,-INF ) is +-3pi/4;
 *  ATAN2(+-INF, (anything but,0,NaN, and INF)) is +-pi/2;
 *
 * Constants:
 * The hexadecimal values are the intended ones for the following
 * constants. The decimal values may be used, provided that the
 * compiler will convert from decimal to binary accurately enough
 * to produce the hexadecimal values shown.
 */
double atan2(double y, double x) {
  static volatile double tiny = 1.0e-300;
  static const double
      zero = 0.0,
      pi_o_4 = 7.8539816339744827900E-01, /* 0x3FE921FB, 0x54442D18 */
      pi_o_2 = 1.5707963267948965580E+00, /* 0x3FF921FB, 0x54442D18 */
      pi = 3.1415926535897931160E+00;     /* 0x400921FB, 0x54442D18 */
  static volatile double pi_lo =
      1.2246467991473531772E-16; /* 0x3CA1A626, 0x33145C07 */

  double z;
  int32_t k, m, hx, hy, ix, iy;
  uint32_t lx, ly;

  EXTRACT_WORDS(hx, lx, x);
  ix = hx & 0x7FFFFFFF;
  EXTRACT_WORDS(hy, ly, y);
  iy = hy & 0x7FFFFFFF;
  if (((ix | ((lx | NegateWithWraparound<int32_t>(lx)) >> 31)) > 0x7FF00000) ||
      ((iy | ((ly | NegateWithWraparound<int32_t>(ly)) >> 31)) > 0x7FF00000)) {
    return x + y; /* x or y is NaN */
  }
  if ((SubWithWraparound(hx, 0x3FF00000) | lx) == 0) {
    return atan(y); /* x=1.0 */
  }
  m = ((hy >> 31) & 1) | ((hx >> 30) & 2);           /* 2*sign(x)+sign(y) */

  /* when y = 0 */
  if ((iy | ly) == 0) {
    switch (m) {
      case 0:
      case 1:
        return y; /* atan(+-0,+anything)=+-0 */
      case 2:
        return pi + tiny; /* atan(+0,-anything) = pi */
      case 3:
        return -pi - tiny; /* atan(-0,-anything) =-pi */
    }
  }
  /* when x = 0 */
  if ((ix | lx) == 0) return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;

  /* when x is INF */
  if (ix == 0x7FF00000) {
    if (iy == 0x7FF00000) {
      switch (m) {
        case 0:
          return pi_o_4 + tiny; /* atan(+INF,+INF) */
        case 1:
          return -pi_o_4 - tiny; /* atan(-INF,+INF) */
        case 2:
          return 3.0 * pi_o_4 + tiny; /*atan(+INF,-INF)*/
        case 3:
          return -3.0 * pi_o_4 - tiny; /*atan(-INF,-INF)*/
      }
    } else {
      switch (m) {
        case 0:
          return zero; /* atan(+...,+INF) */
        case 1:
          return -zero; /* atan(-...,+INF) */
        case 2:
          return pi + tiny; /* atan(+...,-INF) */
        case 3:
          return -pi - tiny; /* atan(-...,-INF) */
      }
    }
  }
  /* when y is INF */
  if (iy == 0x7FF00000) return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;

  /* compute y/x */
  k = (iy - ix) >> 20;
  if (k > 60) { /* |y/x| >  2**60 */
    z = pi_o_2 + 0.5 * pi_lo;
    m &= 1;
  } else if (hx < 0 && k < -60) {
    z = 0.0; /* 0 > |y|/x > -2**-60 */
  } else {
    z = atan(fabs(y / x)); /* safe to do y/x */
  }
  switch (m) {
    case 0:
      return z; /* atan(+,+) */
    case 1:
      return -z; /* atan(-,+) */
    case 2:
      return pi - (z - pi_lo); /* atan(+,-) */
    default:                   /* case 3 */
      return (z - pi_lo) - pi; /* atan(-,-) */
  }
}

#if defined(V8_USE_LIBM_TRIG_FUNCTIONS)
double fdlibm_cos(double x) {
#else
double cos(double x) {
#endif
  return LIBC_NAMESPACE::shared::cos(x);
}

double exp(double x) { return LIBC_NAMESPACE::shared::exp(x); }

/*
 * Method :
 *    1.Reduced x to positive by atanh(-x) = -atanh(x)
 *    2.For x>=0.5
 *              1              2x                          x
 *  atanh(x) = --- * log(1 + -------) = 0.5 * log1p(2 * --------)
 *              2             1 - x                      1 - x
 *
 *   For x<0.5
 *  atanh(x) = 0.5*log1p(2x+2x*x/(1-x))
 *
 * Special cases:
 *  atanh(x) is NaN if |x| > 1 with signal;
 *  atanh(NaN) is that NaN with no signal;
 *  atanh(+-1) is +-INF with signal.
 *
 */
double atanh(double x) {
  static const double one = 1.0, huge = 1e300;
  static const double zero = 0.0;

  double t;
  int32_t hx, ix;
  uint32_t lx;
  EXTRACT_WORDS(hx, lx, x);
  ix = hx & 0x7FFFFFFF;
  if ((ix | ((lx | NegateWithWraparound<int32_t>(lx)) >> 31)) > 0x3FF00000) {
    /* |x|>1 */
    return std::numeric_limits<double>::signaling_NaN();
  }
  if (ix == 0x3FF00000) {
    return x > 0 ? std::numeric_limits<double>::infinity()
                 : -std::numeric_limits<double>::infinity();
  }
  if (ix < 0x3E300000 && (huge + x) > zero) return x; /* x<2**-28 */
  SET_HIGH_WORD(x, ix);
  if (ix < 0x3FE00000) { /* x < 0.5 */
    t = x + x;
    t = 0.5 * log1p(t + t * x / (one - x));
  } else {
    t = 0.5 * log1p((x + x) / (one - x));
  }
  if (hx >= 0) {
    return t;
  } else {
    return -t;
  }
}

double log(double x) { return LIBC_NAMESPACE::shared::log(x); }
double log1p(double x) { return LIBC_NAMESPACE::shared::log1p(x); }
double log2(double x) { return LIBC_NAMESPACE::shared::log2(x); }
double log10(double x) { return LIBC_NAMESPACE::shared::log10(x); }

double expm1(double x) { return LIBC_NAMESPACE::shared::expm1(x); }

double cbrt(double x) { return LIBC_NAMESPACE::shared::cbrt(x); }

#if defined(V8_USE_LIBM_TRIG_FUNCTIONS)
double fdlibm_sin(double x) {
#else
double sin(double x) {
#endif
  return LIBC_NAMESPACE::shared::sin(x);
}

double tan(double x) { return LIBC_NAMESPACE::shared::tan(x); }

/*
 * ES6 draft 09-27-13, section 20.2.2.12.
 * Math.cosh
 * Method :
 * mathematically cosh(x) if defined to be (exp(x)+exp(-x))/2
 *      1. Replace x by |x| (cosh(x) = cosh(-x)).
 *      2.
 *                                                      [ exp(x) - 1 ]^2
 *          0        <= x <= ln2/2  :  cosh(x) := 1 + -------------------
 *                                                         2*exp(x)
 *
 *                                                 exp(x) + 1/exp(x)
 *          ln2/2    <= x <= 22     :  cosh(x) := -------------------
 *                                                        2
 *          22       <= x <= lnovft :  cosh(x) := exp(x)/2
 *          lnovft   <= x <= ln2ovft:  cosh(x) := exp(x/2)/2 * exp(x/2)
 *          ln2ovft  <  x           :  cosh(x) := huge*huge (overflow)
 *
 * Special cases:
 *      cosh(x) is |x| if x is +INF, -INF, or NaN.
 *      only cosh(0)=1 is exact for finite x.
 */
double cosh(double x) {
  static const double KCOSH_OVERFLOW = 710.4758600739439;
  static const double one = 1.0, half = 0.5;
  static volatile double huge = 1.0e+300;

  int32_t ix;

  /* High word of |x|. */
  GET_HIGH_WORD(ix, x);
  ix &= 0x7FFFFFFF;

  // |x| in [0,0.5*log2], return 1+expm1(|x|)^2/(2*exp(|x|))
  if (ix < 0x3FD62E43) {
    double t = expm1(fabs(x));
    double w = one + t;
    // For |x| < 2^-55, cosh(x) = 1
    if (ix < 0x3C800000) return w;
    return one + (t * t) / (w + w);
  }

  // |x| in [0.5*log2, 22], return (exp(|x|)+1/exp(|x|)/2
  if (ix < 0x40360000) {
    double t = exp(fabs(x));
    return half * t + half / t;
  }

  // |x| in [22, log(maxdouble)], return half*exp(|x|)
  if (ix < 0x40862E42) return half * exp(fabs(x));

  // |x| in [log(maxdouble), overflowthreshold]
  if (fabs(x) <= KCOSH_OVERFLOW) {
    double w = exp(half * fabs(x));
    double t = half * w;
    return t * w;
  }

  /* x is INF or NaN */
  if (ix >= 0x7FF00000) return x * x;

  // |x| > overflowthreshold.
  return huge * huge;
}

namespace legacy {
/*
 * ES2019 Draft 2019-01-02 12.6.4
 * Math.pow & Exponentiation Operator
 *
 * Return X raised to the Yth power
 *
 * Method:
 *     Let x =  2   * (1+f)
 *     1. Compute and return log2(x) in two pieces:
 *        log2(x) = w1 + w2,
 *        where w1 has 53-24 = 29 bit trailing zeros.
 *     2. Perform y*log2(x) = n+y' by simulating muti-precision
 *        arithmetic, where |y'|<=0.5.
 *     3. Return x**y = 2**n*exp(y'*log2)
 *
 * Special cases:
 *     1.  (anything) ** 0  is 1
 *     2.  (anything) ** 1  is itself
 *     3.  (anything) ** NAN is NAN
 *     4.  NAN ** (anything except 0) is NAN
 *     5.  +-(|x| > 1) **  +INF is +INF
 *     6.  +-(|x| > 1) **  -INF is +0
 *     7.  +-(|x| < 1) **  +INF is +0
 *     8.  +-(|x| < 1) **  -INF is +INF
 *     9.  +-1         ** +-INF is NAN
 *     10. +0 ** (+anything except 0, NAN)               is +0
 *     11. -0 ** (+anything except 0, NAN, odd integer)  is +0
 *     12. +0 ** (-anything except 0, NAN)               is +INF
 *     13. -0 ** (-anything except 0, NAN, odd integer)  is +INF
 *     14. -0 ** (odd integer) = -( +0 ** (odd integer) )
 *     15. +INF ** (+anything except 0,NAN) is +INF
 *     16. +INF ** (-anything except 0,NAN) is +0
 *     17. -INF ** (anything)  = -0 ** (-anything)
 *     18. (-anything) ** (integer) is (-1)**(integer)*(+anything**integer)
 *     19. (-anything except 0 and inf) ** (non-integer) is NAN
 *
 * Accuracy:
 *      pow(x,y) returns x**y nearly rounded. In particular,
 *      pow(integer, integer) always returns the correct integer provided it is
 *      representable.
 *
 * Constants:
 *     The hexadecimal values are the intended ones for the following
 *     constants. The decimal values may be used, provided that the
 *     compiler will convert from decimal to binary accurately enough
 *     to produce the hexadecimal values shown.
 */

double pow(double x, double y) {
  static const double
      bp[] = {1.0, 1.5},
      dp_h[] = {0.0, 5.84962487220764160156e-01},  // 0x3FE2B803, 0x40000000
      dp_l[] = {0.0, 1.35003920212974897128e-08},  // 0x3E4CFDEB, 0x43CFD006
      zero = 0.0, one = 1.0, two = 2.0,
      two53 = 9007199254740992.0,  // 0x43400000, 0x00000000
      huge = 1.0e300, tiny = 1.0e-300,
      // poly coefs for (3/2)*(log(x)-2s-2/3*s**3
      L1 = 5.99999999999994648725e-01,      // 0x3FE33333, 0x33333303
      L2 = 4.28571428578550184252e-01,      // 0x3FDB6DB6, 0xDB6FABFF
      L3 = 3.33333329818377432918e-01,      // 0x3FD55555, 0x518F264D
      L4 = 2.72728123808534006489e-01,      // 0x3FD17460, 0xA91D4101
      L5 = 2.30660745775561754067e-01,      // 0x3FCD864A, 0x93C9DB65
      L6 = 2.06975017800338417784e-01,      // 0x3FCA7E28, 0x4A454EEF
      P1 = 1.66666666666666019037e-01,      // 0x3FC55555, 0x5555553E
      P2 = -2.77777777770155933842e-03,     // 0xBF66C16C, 0x16BEBD93
      P3 = 6.61375632143793436117e-05,      // 0x3F11566A, 0xAF25DE2C
      P4 = -1.65339022054652515390e-06,     // 0xBEBBBD41, 0xC5D26BF1
      P5 = 4.13813679705723846039e-08,      // 0x3E663769, 0x72BEA4D0
      lg2 = 6.93147180559945286227e-01,     // 0x3FE62E42, 0xFEFA39EF
      lg2_h = 6.93147182464599609375e-01,   // 0x3FE62E43, 0x00000000
      lg2_l = -1.90465429995776804525e-09,  // 0xBE205C61, 0x0CA86C39
      ovt = 8.0085662595372944372e-0017,    // -(1024-log2(ovfl+.5ulp))
      cp = 9.61796693925975554329e-01,      // 0x3FEEC709, 0xDC3A03FD =2/(3ln2)
      cp_h = 9.61796700954437255859e-01,    // 0x3FEEC709, 0xE0000000 =(float)cp
      cp_l = -7.02846165095275826516e-09,   // 0xBE3E2FE0, 0x145B01F5 =tail cp_h
      ivln2 = 1.44269504088896338700e+00,   // 0x3FF71547, 0x652B82FE =1/ln2
      ivln2_h =
          1.44269502162933349609e+00,  // 0x3FF71547, 0x60000000 =24b 1/ln2
      ivln2_l =
          1.92596299112661746887e-08;  // 0x3E54AE0B, 0xF85DDF44 =1/ln2 tail

  double z, ax, z_h, z_l, p_h, p_l;
  double y1, t1, t2, r, s, t, u, v, w;
  int i, j, k, yisint, n;
  int hx, hy, ix, iy;
  unsigned lx, ly;

  EXTRACT_WORDS(hx, lx, x);
  EXTRACT_WORDS(hy, ly, y);
  ix = hx & 0x7fffffff;
  iy = hy & 0x7fffffff;

  /* y==zero: x**0 = 1 */
  if ((iy | ly) == 0) return one;

  /* +-NaN return x+y */
  if (ix > 0x7ff00000 || ((ix == 0x7ff00000) && (lx != 0)) || iy > 0x7ff00000 ||
      ((iy == 0x7ff00000) && (ly != 0))) {
    return x + y;
  }

  /* determine if y is an odd int when x < 0
   * yisint = 0 ... y is not an integer
   * yisint = 1 ... y is an odd int
   * yisint = 2 ... y is an even int
   */
  yisint = 0;
  if (hx < 0) {
    if (iy >= 0x43400000) {
      yisint = 2; /* even integer y */
    } else if (iy >= 0x3ff00000) {
      k = (iy >> 20) - 0x3ff; /* exponent */
      if (k > 20) {
        j = ly >> (52 - k);
        if ((j << (52 - k)) == static_cast<int>(ly)) yisint = 2 - (j & 1);
      } else if (ly == 0) {
        j = iy >> (20 - k);
        if ((j << (20 - k)) == iy) yisint = 2 - (j & 1);
      }
    }
  }

  /* special value of y */
  if (ly == 0) {
    if (iy == 0x7ff00000) { /* y is +-inf */
      if (((ix - 0x3ff00000) | lx) == 0) {
        return y - y;                /* inf**+-1 is NaN */
      } else if (ix >= 0x3ff00000) { /* (|x|>1)**+-inf = inf,0 */
        return (hy >= 0) ? y : zero;
      } else { /* (|x|<1)**-,+inf = inf,0 */
        return (hy < 0) ? -y : zero;
      }
    }
    if (iy == 0x3ff00000) { /* y is  +-1 */
      if (hy < 0) {
        return base::Divide(one, x);
      } else {
        return x;
      }
    }
    if (hy == 0x40000000) return x * x; /* y is  2 */
    if (hy == 0x3fe00000) {             /* y is  0.5 */
      if (hx >= 0) {                    /* x >= +0 */
        return sqrt(x);
      }
    }
  }

  ax = fabs(x);
  /* special value of x */
  if (lx == 0) {
    if (ix == 0x7ff00000 || ix == 0 || ix == 0x3ff00000) {
      z = ax;                               /*x is +-0,+-inf,+-1*/
      if (hy < 0) z = base::Divide(one, z); /* z = (1/|x|) */
      if (hx < 0) {
        if (((ix - 0x3ff00000) | yisint) == 0) {
          /* (-1)**non-int is NaN */
          z = std::numeric_limits<double>::signaling_NaN();
        } else if (yisint == 1) {
          z = -z; /* (x<0)**odd = -(|x|**odd) */
        }
      }
      return z;
    }
  }

  n = (hx >> 31) + 1;

  /* (x<0)**(non-int) is NaN */
  if ((n | yisint) == 0) {
    return std::numeric_limits<double>::signaling_NaN();
  }

  s = one; /* s (sign of result -ve**odd) = -1 else = 1 */
  if ((n | (yisint - 1)) == 0) s = -one; /* (-ve)**(odd int) */

  /* |y| is huge */
  if (iy > 0x41e00000) {   /* if |y| > 2**31 */
    if (iy > 0x43f00000) { /* if |y| > 2**64, must o/uflow */
      if (ix <= 0x3fefffff) return (hy < 0) ? huge * huge : tiny * tiny;
      if (ix >= 0x3ff00000) return (hy > 0) ? huge * huge : tiny * tiny;
    }
    /* over/underflow if x is not close to one */
    if (ix < 0x3fefffff) return (hy < 0) ? s * huge * huge : s * tiny * tiny;
    if (ix > 0x3ff00000) return (hy > 0) ? s * huge * huge : s * tiny * tiny;
    /* now |1-x| is tiny <= 2**-20, suffice to compute
       log(x) by x-x^2/2+x^3/3-x^4/4 */
    t = ax - one; /* t has 20 trailing zeros */
    w = (t * t) * (0.5 - t * (0.3333333333333333333333 - t * 0.25));
    u = ivln2_h * t; /* ivln2_h has 21 sig. bits */
    v = t * ivln2_l - w * ivln2;
    t1 = u + v;
    SET_LOW_WORD(t1, 0);
    t2 = v - (t1 - u);
  } else {
    double ss, s2, s_h, s_l, t_h, t_l;
    n = 0;
    /* take care subnormal number */
    if (ix < 0x00100000) {
      ax *= two53;
      n -= 53;
      GET_HIGH_WORD(ix, ax);
    }
    n += ((ix) >> 20) - 0x3ff;
    j = ix & 0x000fffff;
    /* determine interval */
    ix = j | 0x3ff00000; /* normalize ix */
    if (j <= 0x3988E) {
      k = 0; /* |x|<sqrt(3/2) */
    } else if (j < 0xBB67A) {
      k = 1; /* |x|<sqrt(3)   */
    } else {
      k = 0;
      n += 1;
      ix -= 0x00100000;
    }
    SET_HIGH_WORD(ax, ix);

    /* compute ss = s_h+s_l = (x-1)/(x+1) or (x-1.5)/(x+1.5) */
    u = ax - bp[k]; /* bp[0]=1.0, bp[1]=1.5 */
    v = base::Divide(one, ax + bp[k]);
    ss = u * v;
    s_h = ss;
    SET_LOW_WORD(s_h, 0);
    /* t_h=ax+bp[k] High */
    t_h = zero;
    SET_HIGH_WORD(t_h, ((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18));
    t_l = ax - (t_h - bp[k]);
    s_l = v * ((u - s_h * t_h) - s_h * t_l);
    /* compute log(ax) */
    s2 = ss * ss;
    r = s2 * s2 *
        (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
    r += s_l * (s_h + ss);
    s2 = s_h * s_h;
    t_h = 3.0 + s2 + r;
    SET_LOW_WORD(t_h, 0);
    t_l = r - ((t_h - 3.0) - s2);
    /* u+v = ss*(1+...) */
    u = s_h * t_h;
    v = s_l * t_h + t_l * ss;
    /* 2/(3log2)*(ss+...) */
    p_h = u + v;
    SET_LOW_WORD(p_h, 0);
    p_l = v - (p_h - u);
    z_h = cp_h * p_h; /* cp_h+cp_l = 2/(3*log2) */
    z_l = cp_l * p_h + p_l * cp + dp_l[k];
    /* log2(ax) = (ss+..)*2/(3*log2) = n + dp_h + z_h + z_l */
    t = static_cast<double>(n);
    t1 = (((z_h + z_l) + dp_h[k]) + t);
    SET_LOW_WORD(t1, 0);
    t2 = z_l - (((t1 - t) - dp_h[k]) - z_h);
  }

  /* split up y into y1+y2 and compute (y1+y2)*(t1+t2) */
  y1 = y;
  SET_LOW_WORD(y1, 0);
  p_l = (y - y1) * t1 + y * t2;
  p_h = y1 * t1;
  z = p_l + p_h;
  EXTRACT_WORDS(j, i, z);
  if (j >= 0x40900000) {               /* z >= 1024 */
    if (((j - 0x40900000) | i) != 0) { /* if z > 1024 */
      return s * huge * huge;          /* overflow */
    } else {
      if (p_l + ovt > z - p_h) return s * huge * huge; /* overflow */
    }
  } else if ((j & 0x7fffffff) >= 0x4090cc00) { /* z <= -1075 */
    if (((j - 0xc090cc00) | i) != 0) {         /* z < -1075 */
      return s * tiny * tiny;                  /* underflow */
    } else {
      if (p_l <= z - p_h) return s * tiny * tiny; /* underflow */
    }
  }
  /*
   * compute 2**(p_h+p_l)
   */
  i = j & 0x7fffffff;
  k = (i >> 20) - 0x3ff;
  n = 0;
  if (i > 0x3fe00000) { /* if |z| > 0.5, set n = [z+0.5] */
    n = j + (0x00100000 >> (k + 1));
    k = ((n & 0x7fffffff) >> 20) - 0x3ff; /* new k for n */
    t = zero;
    SET_HIGH_WORD(t, n & ~(0x000fffff >> k));
    n = ((n & 0x000fffff) | 0x00100000) >> (20 - k);
    if (j < 0) n = -n;
    p_h -= t;
  }
  t = p_l + p_h;
  SET_LOW_WORD(t, 0);
  u = t * lg2_h;
  v = (p_l - (t - p_h)) * lg2 + t * lg2_l;
  z = u + v;
  w = v - (z - u);
  t = z * z;
  t1 = z - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
  r = base::Divide(z * t1, (t1 - two) - (w + z * w));
  z = one - (r - z);
  GET_HIGH_WORD(j, z);
  j += static_cast<int>(static_cast<uint32_t>(n) << 20);
  if ((j >> 20) <= 0) {
    z = scalbn(z, n); /* subnormal output */
  } else {
    int tmp;
    GET_HIGH_WORD(tmp, z);
    SET_HIGH_WORD(z, tmp + static_cast<int>(static_cast<uint32_t>(n) << 20));
  }
  return s * z;
}

}  // namespace legacy

/*
 * ES6 draft 09-27-13, section 20.2.2.30.
 * Math.sinh
 * Method :
 * mathematically sinh(x) if defined to be (exp(x)-exp(-x))/2
 *      1. Replace x by |x| (sinh(-x) = -sinh(x)).
 *      2.
 *                                                  E + E/(E+1)
 *          0        <= x <= 22     :  sinh(x) := --------------, E=expm1(x)
 *                                                      2
 *
 *          22       <= x <= lnovft :  sinh(x) := exp(x)/2
 *          lnovft   <= x <= ln2ovft:  sinh(x) := exp(x/2)/2 * exp(x/2)
 *          ln2ovft  <  x           :  sinh(x) := x*shuge (overflow)
 *
 * Special cases:
 *      sinh(x) is |x| if x is +Infinity, -Infinity, or NaN.
 *      only sinh(0)=0 is exact for finite x.
 */
double sinh(double x) {
  static const double KSINH_OVERFLOW = 710.4758600739439,
                      TWO_M28 =
                          3.725290298461914e-9,  // 2^-28, empty lower half
      LOG_MAXD = 709.7822265625;  // 0x40862E42 00000000, empty lower half
  static const double shuge = 1.0e307;

  double h = (x < 0) ? -0.5 : 0.5;
  // |x| in [0, 22]. return sign(x)*0.5*(E+E/(E+1))
  double ax = fabs(x);
  if (ax < 22) {
    // For |x| < 2^-28, sinh(x) = x
    if (ax < TWO_M28) return x;
    double t = expm1(ax);
    if (ax < 1) {
      return h * (2 * t - t * t / (t + 1));
    }
    return h * (t + t / (t + 1));
  }
  // |x| in [22, log(maxdouble)], return 0.5 * exp(|x|)
  if (ax < LOG_MAXD) return h * exp(ax);
  // |x| in [log(maxdouble), overflowthreshold]
  // overflowthreshold = 710.4758600739426
  if (ax <= KSINH_OVERFLOW) {
    double w = exp(0.5 * ax);
    double t = h * w;
    return t * w;
  }
  // |x| > overflowthreshold or is NaN.
  // Return Infinity of the appropriate sign or NaN.
  return x * shuge;
}

#undef EXTRACT_WORDS
#undef GET_HIGH_WORD
#undef GET_LOW_WORD
#undef INSERT_WORDS
#undef SET_HIGH_WORD
#undef SET_LOW_WORD

double tanh(double x) { return std::tanh(x); }

#if defined(V8_USE_LIBM_TRIG_FUNCTIONS) && defined(BUILDING_V8_BASE_SHARED)
double libm_sin(double x) { return glibc_sin(x); }
double libm_cos(double x) { return glibc_cos(x); }
#endif

}  // namespace ieee754
}  // namespace base
}  // namespace v8
