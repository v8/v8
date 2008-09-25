// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


// Keep reference to original values of some global properties.  This
// has the added benefit that the code in this file is isolated from
// changes to these properties.
const $Infinity = global.Infinity;
const $floor = $Math_floor;
const $random = $Math_random;
const $abs = $Math_abs;

// Instance class name can only be set on functions. That is the only
// purpose for MathConstructor.
function MathConstructor() {};
%FunctionSetInstanceClassName(MathConstructor, 'Math');  
const $Math = new MathConstructor();
$Math.__proto__ = global.Object.prototype;
%AddProperty(global, "Math", $Math, DONT_ENUM);


function $Math_random() { return %Math_random(); }
%AddProperty($Math, "random", $Math_random, DONT_ENUM);

function $Math_abs(x) {
  if (%_IsSmi(x)) {
    return x >= 0 ? x : -x;
  } else {
    return %Math_abs(ToNumber(x));
  }
}
%AddProperty($Math, "abs", $Math_abs, DONT_ENUM);

function $Math_acos(x) { return %Math_acos(ToNumber(x)); }
%AddProperty($Math, "acos", $Math_acos, DONT_ENUM);

function $Math_asin(x) { return %Math_asin(ToNumber(x)); }
%AddProperty($Math, "asin", $Math_asin, DONT_ENUM);

function $Math_atan(x) { return %Math_atan(ToNumber(x)); }
%AddProperty($Math, "atan", $Math_atan, DONT_ENUM);

function $Math_ceil(x) { return %Math_ceil(ToNumber(x)); }
%AddProperty($Math, "ceil", $Math_ceil, DONT_ENUM);

function $Math_cos(x) { return %Math_cos(ToNumber(x)); }
%AddProperty($Math, "cos", $Math_cos, DONT_ENUM);

function $Math_exp(x) { return %Math_exp(ToNumber(x)); }
%AddProperty($Math, "exp", $Math_exp, DONT_ENUM);

function $Math_floor(x) { return %Math_floor(ToNumber(x)); }
%AddProperty($Math, "floor", $Math_floor, DONT_ENUM);

function $Math_log(x) { return %Math_log(ToNumber(x)); }
%AddProperty($Math, "log", $Math_log, DONT_ENUM);

function $Math_round(x) { return %Math_round(ToNumber(x)); }
%AddProperty($Math, "round", $Math_round, DONT_ENUM);

function $Math_sin(x) { return %Math_sin(ToNumber(x)); }
%AddProperty($Math, "sin", $Math_sin, DONT_ENUM);

function $Math_sqrt(x) { return %Math_sqrt(ToNumber(x)); }
%AddProperty($Math, "sqrt", $Math_sqrt, DONT_ENUM);

function $Math_tan(x) { return %Math_tan(ToNumber(x)); }
%AddProperty($Math, "tan", $Math_tan, DONT_ENUM);

function $Math_atan2(x, y) { return %Math_atan2(ToNumber(x), ToNumber(y)); }
%AddProperty($Math, "atan2", $Math_atan2, DONT_ENUM);

function $Math_pow(x, y) { return %Math_pow(ToNumber(x), ToNumber(y)); }
%AddProperty($Math, "pow", $Math_pow, DONT_ENUM);

function $Math_max(arg1, arg2) {  // length == 2
  var r = -$Infinity;
  for (var i = %_ArgumentsLength() - 1; i >= 0; --i) {
    var n = ToNumber(%_Arguments(i));
    if (NUMBER_IS_NAN(n)) return n;
    // Make sure +0 is consider greater than -0.
    if (n > r || (n === 0 && r === 0 && (1 / n) > (1 / r))) r = n;
  }
  return r;
}
%AddProperty($Math, "max", $Math_max, DONT_ENUM);

function $Math_min(arg1, arg2) {  // length == 2
  var r = $Infinity;
  for (var i = %_ArgumentsLength() - 1; i >= 0; --i) {
    var n = ToNumber(%_Arguments(i));
    if (NUMBER_IS_NAN(n)) return n;
    // Make sure -0 is consider less than +0.
    if (n < r || (n === 0 && r === 0 && (1 / n) < (1 / r))) r = n;
  }
  return r;
}
%AddProperty($Math, "min", $Math_min, DONT_ENUM);


// ECMA-262, section 15.8.1.1.
%AddProperty($Math, "E", 2.7182818284590452354, DONT_ENUM |  DONT_DELETE | READ_ONLY);

// ECMA-262, section 15.8.1.2.
%AddProperty($Math, "LN10", 2.302585092994046, DONT_ENUM |  DONT_DELETE | READ_ONLY);

// ECMA-262, section 15.8.1.3.
%AddProperty($Math, "LN2", 0.6931471805599453, DONT_ENUM |  DONT_DELETE | READ_ONLY);

// ECMA-262, section 15.8.1.4.
%AddProperty($Math, "LOG2E", 1.4426950408889634, DONT_ENUM |  DONT_DELETE | READ_ONLY);
%AddProperty($Math, "LOG10E", 0.43429448190325176, DONT_ENUM |  DONT_DELETE | READ_ONLY);
%AddProperty($Math, "PI", 3.1415926535897932, DONT_ENUM |  DONT_DELETE | READ_ONLY);
%AddProperty($Math, "SQRT1_2", 0.7071067811865476, DONT_ENUM |  DONT_DELETE | READ_ONLY);
%AddProperty($Math, "SQRT2", 1.4142135623730951, DONT_ENUM |  DONT_DELETE | READ_ONLY);
