// The following is adapted from fdlibm (http://www.netlib.org/fdlibm),
//
// ====================================================
// Copyright (C) 1993-2004 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
//
// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2014 the V8 project authors. All rights reserved.
//
// The following is a straightforward translation of fdlibm routines
// by Raymond Toy (rtoy@google.com).

(function(global, utils) {
  
"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var GlobalMath = global.Math;
var MathAbs;
var MathExpm1;

utils.Import(function(from) {
  MathAbs = from.MathAbs;
  MathExpm1 = from.MathExpm1;
});

// ES6 draft 09-27-13, section 20.2.2.30.
// Math.sinh
// Method :
// mathematically sinh(x) if defined to be (exp(x)-exp(-x))/2
//      1. Replace x by |x| (sinh(-x) = -sinh(x)).
//      2.
//                                                  E + E/(E+1)
//          0        <= x <= 22     :  sinh(x) := --------------, E=expm1(x)
//                                                      2
//
//          22       <= x <= lnovft :  sinh(x) := exp(x)/2 
//          lnovft   <= x <= ln2ovft:  sinh(x) := exp(x/2)/2 * exp(x/2)
//          ln2ovft  <  x           :  sinh(x) := x*shuge (overflow)
//
// Special cases:
//      sinh(x) is |x| if x is +Infinity, -Infinity, or NaN.
//      only sinh(0)=0 is exact for finite x.
//
define KSINH_OVERFLOW = 710.4758600739439;
define TWO_M28 = 3.725290298461914e-9;  // 2^-28, empty lower half
define LOG_MAXD = 709.7822265625;  // 0x40862e42 00000000, empty lower half

function MathSinh(x) {
  x = x * 1;  // Convert to number.
  var h = (x < 0) ? -0.5 : 0.5;
  // |x| in [0, 22]. return sign(x)*0.5*(E+E/(E+1))
  var ax = MathAbs(x);
  if (ax < 22) {
    // For |x| < 2^-28, sinh(x) = x
    if (ax < TWO_M28) return x;
    var t = MathExpm1(ax);
    if (ax < 1) return h * (2 * t - t * t / (t + 1));
    return h * (t + t / (t + 1));
  }
  // |x| in [22, log(maxdouble)], return 0.5 * exp(|x|)
  if (ax < LOG_MAXD) return h * %math_exp(ax);
  // |x| in [log(maxdouble), overflowthreshold]
  // overflowthreshold = 710.4758600739426
  if (ax <= KSINH_OVERFLOW) {
    var w = %math_exp(0.5 * ax);
    var t = h * w;
    return t * w;
  }
  // |x| > overflowthreshold or is NaN.
  // Return Infinity of the appropriate sign or NaN.
  return x * INFINITY;
}


// ES6 draft 09-27-13, section 20.2.2.12.
// Math.cosh
// Method : 
// mathematically cosh(x) if defined to be (exp(x)+exp(-x))/2
//      1. Replace x by |x| (cosh(x) = cosh(-x)). 
//      2.
//                                                      [ exp(x) - 1 ]^2 
//          0        <= x <= ln2/2  :  cosh(x) := 1 + -------------------
//                                                         2*exp(x)
//
//                                                 exp(x) + 1/exp(x)
//          ln2/2    <= x <= 22     :  cosh(x) := -------------------
//                                                        2
//          22       <= x <= lnovft :  cosh(x) := exp(x)/2 
//          lnovft   <= x <= ln2ovft:  cosh(x) := exp(x/2)/2 * exp(x/2)
//          ln2ovft  <  x           :  cosh(x) := huge*huge (overflow)
//
// Special cases:
//      cosh(x) is |x| if x is +INF, -INF, or NaN.
//      only cosh(0)=1 is exact for finite x.
//
define KCOSH_OVERFLOW = 710.4758600739439;

function MathCosh(x) {
  x = x * 1;  // Convert to number.
  var ix = %_DoubleHi(x) & 0x7fffffff;
  // |x| in [0,0.5*log2], return 1+expm1(|x|)^2/(2*exp(|x|))
  if (ix < 0x3fd62e43) {
    var t = MathExpm1(MathAbs(x));
    var w = 1 + t;
    // For |x| < 2^-55, cosh(x) = 1
    if (ix < 0x3c800000) return w;
    return 1 + (t * t) / (w + w);
  }
  // |x| in [0.5*log2, 22], return (exp(|x|)+1/exp(|x|)/2
  if (ix < 0x40360000) {
    var t = %math_exp(MathAbs(x));
    return 0.5 * t + 0.5 / t;
  }
  // |x| in [22, log(maxdouble)], return half*exp(|x|)
  if (ix < 0x40862e42) return 0.5 * %math_exp(MathAbs(x));
  // |x| in [log(maxdouble), overflowthreshold]
  if (MathAbs(x) <= KCOSH_OVERFLOW) {
    var w = %math_exp(0.5 * MathAbs(x));
    var t = 0.5 * w;
    return t * w;
  }
  if (NUMBER_IS_NAN(x)) return x;
  // |x| > overflowthreshold.
  return INFINITY;
}

// ES6 draft 09-27-13, section 20.2.2.33.
// Math.tanh(x)
// Method :
//                                    x    -x
//                                   e  - e
//     0. tanh(x) is defined to be -----------
//                                    x    -x
//                                   e  + e
//     1. reduce x to non-negative by tanh(-x) = -tanh(x).
//     2.  0      <= x <= 2**-55 : tanh(x) := x*(one+x)
//                                             -t
//         2**-55 <  x <=  1     : tanh(x) := -----; t = expm1(-2x)
//                                            t + 2
//                                                  2
//         1      <= x <=  22.0  : tanh(x) := 1-  ----- ; t = expm1(2x)
//                                                t + 2
//         22.0   <  x <= INF    : tanh(x) := 1.
//
// Special cases:
//     tanh(NaN) is NaN;
//     only tanh(0) = 0 is exact for finite argument.
//

define TWO_M55 = 2.77555756156289135105e-17;  // 2^-55, empty lower half

function MathTanh(x) {
  x = x * 1;  // Convert to number.
  // x is Infinity or NaN
  if (!NUMBER_IS_FINITE(x)) {
    if (x > 0) return 1;
    if (x < 0) return -1;
    return x;
  }

  var ax = MathAbs(x);
  var z;
  // |x| < 22
  if (ax < 22) {
    if (ax < TWO_M55) {
      // |x| < 2^-55, tanh(small) = small.
      return x;
    }
    if (ax >= 1) {
      // |x| >= 1
      var t = MathExpm1(2 * ax);
      z = 1 - 2 / (t + 2);
    } else {
      var t = MathExpm1(-2 * ax);
      z = -t / (t + 2);
    }
  } else {
    // |x| > 22, return +/- 1
    z = 1;
  }
  return (x >= 0) ? z : -z;
}

//-------------------------------------------------------------------

utils.InstallFunctions(GlobalMath, DONT_ENUM, [
  "sinh", MathSinh,
  "cosh", MathCosh,
  "tanh", MathTanh
]);

})
