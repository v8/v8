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

var GlobalMath = global.Math;
var NaN = %GetRootNaN();
var nextRandomIndex = 0;
var randomNumbers = UNDEFINED;
var toStringTagSymbol = utils.ImportNow("to_string_tag_symbol");

//-------------------------------------------------------------------
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


// -------------------------------------------------------------------

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
  "sign", MathSign,
  "asinh", MathAsinh,
  "acosh", MathAcosh
]);

%SetForceInlineFlag(MathRandom);
%SetForceInlineFlag(MathSign);

// -------------------------------------------------------------------
// Exports

utils.Export(function(to) {
  to.MathRandom = MathRandom;
});

})
