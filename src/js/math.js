// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {
"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

// The first two slots are reserved to persist PRNG state.
define kRandomNumberStart = 2;

var GlobalFloat64Array = global.Float64Array;
var GlobalMath = global.Math;
var GlobalObject = global.Object;
var NaN = %GetRootNaN();
var nextRandomIndex = 0;
var randomNumbers = UNDEFINED;
var toStringTagSymbol = utils.ImportNow("to_string_tag_symbol");

//-------------------------------------------------------------------

// ECMA 262 - 15.8.2.1
function MathAbs(x) {
  x = +x;
  return (x > 0) ? x : 0 - x;
}

// ECMA 262 - 15.8.2.13
function MathPowJS(x, y) {
  return %_MathPow(TO_NUMBER(x), TO_NUMBER(y));
}

// ECMA 262 - 15.8.2.14
function MathRandom() {
  // While creating a startup snapshot, %GenerateRandomNumbers returns a
  // normal array containing a single random number, and has to be called for
  // every new random number.
  // Otherwise, it returns a pre-populated typed array of random numbers. The
  // first two elements are reserved for the PRNG state.
  if (nextRandomIndex <= kRandomNumberStart) {
    randomNumbers = %GenerateRandomNumbers(randomNumbers);
    if (%_IsTypedArray(randomNumbers)) {
      nextRandomIndex = %_TypedArrayGetLength(randomNumbers);
    } else {
      nextRandomIndex = randomNumbers.length;
    }
  }
  return randomNumbers[--nextRandomIndex];
}

function MathRandomRaw() {
  if (nextRandomIndex <= kRandomNumberStart) {
    randomNumbers = %GenerateRandomNumbers(randomNumbers);
    nextRandomIndex = %_TypedArrayGetLength(randomNumbers);
  }
  return %_DoubleLo(randomNumbers[--nextRandomIndex]) & 0x3FFFFFFF;
}

// ES6 draft 09-27-13, section 20.2.2.28.
function MathSign(x) {
  x = +x;
  if (x > 0) return 1;
  if (x < 0) return -1;
  // -0, 0 or NaN.
  return x;
}

// ES6 draft 09-27-13, section 20.2.2.5.
function MathAsinh(x) {
  x = TO_NUMBER(x);
  // Idempotent for NaN, +/-0 and +/-Infinity.
  if (x === 0 || !NUMBER_IS_FINITE(x)) return x;
  if (x > 0) return %math_log(x + %math_sqrt(x * x + 1));
  // This is to prevent numerical errors caused by large negative x.
  return -%math_log(-x + %math_sqrt(x * x + 1));
}

// ES6 draft 09-27-13, section 20.2.2.3.
function MathAcosh(x) {
  x = TO_NUMBER(x);
  if (x < 1) return NaN;
  // Idempotent for NaN and +Infinity.
  if (!NUMBER_IS_FINITE(x)) return x;
  return %math_log(x + %math_sqrt(x + 1) * %math_sqrt(x - 1));
}

// ES6 draft 09-27-13, section 20.2.2.17.
function MathHypot(x, y) {  // Function length is 2.
  // We may want to introduce fast paths for two arguments and when
  // normalization to avoid overflow is not necessary.  For now, we
  // simply assume the general case.
  var length = arguments.length;
  var max = 0;
  for (var i = 0; i < length; i++) {
    var n = MathAbs(arguments[i]);
    if (n > max) max = n;
    arguments[i] = n;
  }
  if (max === INFINITY) return INFINITY;

  // Kahan summation to avoid rounding errors.
  // Normalize the numbers to the largest one to avoid overflow.
  if (max === 0) max = 1;
  var sum = 0;
  var compensation = 0;
  for (var i = 0; i < length; i++) {
    var n = arguments[i] / max;
    var summand = n * n - compensation;
    var preliminary = sum + summand;
    compensation = (preliminary - sum) - summand;
    sum = preliminary;
  }
  return %math_sqrt(sum) * max;
}

// -------------------------------------------------------------------

%InstallToContext([
  "math_pow", MathPowJS,
]);

%AddNamedProperty(GlobalMath, toStringTagSymbol, "Math", READ_ONLY | DONT_ENUM);

// Set up math constants.
utils.InstallConstants(GlobalMath, [
  "PI", 3.1415926535897932,
  "SQRT1_2", 0.7071067811865476,
  "SQRT2", 1.4142135623730951
]);

// Set up non-enumerable functions of the Math object and
// set their names.
utils.InstallFunctions(GlobalMath, DONT_ENUM, [
  "random", MathRandom,
  "abs", MathAbs,
  "pow", MathPowJS,
  "sign", MathSign,
  "asinh", MathAsinh,
  "acosh", MathAcosh,
  "hypot", MathHypot,
]);

%SetForceInlineFlag(MathRandom);
%SetForceInlineFlag(MathSign);

// -------------------------------------------------------------------
// Exports

utils.Export(function(to) {
  to.MathAbs = MathAbs;
  to.IntRandom = MathRandomRaw;
});

})
