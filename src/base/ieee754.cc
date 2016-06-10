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

#include <limits>

#include "src/base/build_config.h"
#include "src/base/macros.h"

namespace v8 {
namespace base {
namespace ieee754 {

namespace {

union Float64 {
  double v;
  uint64_t w;
  struct {
#if V8_TARGET_LITTLE_ENDIAN
    uint32_t lw;
    uint32_t hw;
#else
    uint32_t hw;
    uint32_t lw;
#endif
  } words;
};

// Extract the less significant 32-bit word from a double.
V8_INLINE uint32_t extractLowWord32(double v) {
  Float64 f;
  f.v = v;
  return f.words.lw;
}

// Extract the most significant 32-bit word from a double.
V8_INLINE uint32_t extractHighWord32(double v) {
  Float64 f;
  f.v = v;
  return f.words.hw;
}

// Insert the most significant 32-bit word into a double.
V8_INLINE double insertHighWord32(double v, uint32_t hw) {
  Float64 f;
  f.v = v;
  f.words.hw = hw;
  return f.v;
}

double const kLn2Hi = 6.93147180369123816490e-01;  // 3fe62e42 fee00000
double const kLn2Lo = 1.90821492927058770002e-10;  // 3dea39ef 35793c76
double const kTwo54 = 1.80143985094819840000e+16;  // 43500000 00000000
double const kLg1 = 6.666666666666735130e-01;      // 3FE55555 55555593
double const kLg2 = 3.999999999940941908e-01;      // 3FD99999 9997FA04
double const kLg3 = 2.857142874366239149e-01;      // 3FD24924 94229359
double const kLg4 = 2.222219843214978396e-01;      // 3FCC71C5 1D8E78AF
double const kLg5 = 1.818357216161805012e-01;      // 3FC74664 96CB03DE
double const kLg6 = 1.531383769920937332e-01;      // 3FC39A09 D078C69F
double const kLg7 = 1.479819860511658591e-01;      // 3FC2F112 DF3E5244

}  // namespace

/* log(x)
 * Return the logrithm of x
 *
 * Method :
 *   1. Argument Reduction: find k and f such that
 *      x = 2^k * (1+f),
 *     where  sqrt(2)/2 < 1+f < sqrt(2) .
 *
 *   2. Approximation of log(1+f).
 *  Let s = f/(2+f) ; based on log(1+f) = log(1+s) - log(1-s)
 *     = 2s + 2/3 s**3 + 2/5 s**5 + .....,
 *         = 2s + s*R
 *      We use a special Reme algorithm on [0,0.1716] to generate
 *  a polynomial of degree 14 to approximate R The maximum error
 *  of this polynomial approximation is bounded by 2**-58.45. In
 *  other words,
 *            2      4      6      8      10      12      14
 *      R(z) ~ Lg1*s +Lg2*s +Lg3*s +Lg4*s +Lg5*s  +Lg6*s  +Lg7*s
 *    (the values of Lg1 to Lg7 are listed in the program)
 *  and
 *      |      2          14          |     -58.45
 *      | Lg1*s +...+Lg7*s    -  R(z) | <= 2
 *      |                             |
 *  Note that 2s = f - s*f = f - hfsq + s*hfsq, where hfsq = f*f/2.
 *  In order to guarantee error in log below 1ulp, we compute log
 *  by
 *    log(1+f) = f - s*(f - R)  (if f is not too large)
 *    log(1+f) = f - (hfsq - s*(hfsq+R)). (better accuracy)
 *
 *  3. Finally,  log(x) = k*ln2 + log(1+f).
 *          = k*ln2_hi+(f-(hfsq-(s*(hfsq+R)+k*ln2_lo)))
 *     Here ln2 is split into two floating point number:
 *      ln2_hi + ln2_lo,
 *     where n*ln2_hi is always exact for |n| < 2000.
 *
 * Special cases:
 *  log(x) is NaN with signal if x < 0 (including -INF) ;
 *  log(+INF) is +INF; log(0) is -INF with signal;
 *  log(NaN) is that NaN with no signal.
 *
 * Accuracy:
 *  according to an error analysis, the error is always less than
 *  1 ulp (unit in the last place).
 *
 * Constants:
 * The hexadecimal values are the intended ones for the following
 * constants. The decimal values may be used, provided that the
 * compiler will convert from decimal to binary accurately enough
 * to produce the hexadecimal values shown.
 */
double log(double x) {
  double hfsq, f, s, z, r, w, t1, t2, dk;
  int32_t k = 0, i, j;
  int32_t hx = extractHighWord32(x);
  uint32_t lx = extractLowWord32(x);

  if (hx < 0x00100000) { /* x < 2**-1022  */
    if (((hx & 0x7fffffff) | lx) == 0) {
      return -std::numeric_limits<double>::infinity();
    }
    if (hx < 0) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    k -= 54;
    x *= kTwo54; /* subnormal number, scale up x */
    hx = extractHighWord32(x);
  }
  if (hx >= 0x7ff00000) return x + x;
  k += (hx >> 20) - 1023;
  hx &= 0x000fffff;
  i = (hx + 0x95f64) & 0x100000;
  x = insertHighWord32(x, hx | (i ^ 0x3ff00000)); /* normalize x or x/2 */
  k += (i >> 20);
  f = x - 1.0;
  if ((0x000fffff & (2 + hx)) < 3) { /* -2**-20 <= f < 2**-20 */
    if (f == 0.0) {
      if (k == 0) {
        return 0.0;
      } else {
        dk = static_cast<double>(k);
        return dk * kLn2Hi + dk * kLn2Lo;
      }
    }
    r = f * f * (0.5 - 0.33333333333333333 * f);
    if (k == 0) {
      return f - r;
    } else {
      dk = static_cast<double>(k);
      return dk * kLn2Hi - ((r - dk * kLn2Lo) - f);
    }
  }
  s = f / (2.0 + f);
  dk = static_cast<double>(k);
  z = s * s;
  i = hx - 0x6147a;
  w = z * z;
  j = 0x6b851 - hx;
  t1 = w * (kLg2 + w * (kLg4 + w * kLg6));
  t2 = z * (kLg1 + w * (kLg3 + w * (kLg5 + w * kLg7)));
  i |= j;
  r = t2 + t1;
  if (i > 0) {
    hfsq = 0.5 * f * f;
    if (k == 0) {
      return f - (hfsq - s * (hfsq + r));
    } else {
      return dk * kLn2Hi - ((hfsq - (s * (hfsq + r) + dk * kLn2Lo)) - f);
    }
  } else {
    if (k == 0) {
      return f - s * (f - r);
    } else {
      return dk * kLn2Hi - ((s * (f - r) - dk * kLn2Lo) - f);
    }
  }
}

}  // namespace ieee754
}  // namespace base
}  // namespace v8
