// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function square(x) {
  x = x | 0;
  return x * x;
}

%PrepareFunctionForOptimization(square);
assertEquals(9, square(3));
assertEquals(9, square(-3));
%OptimizeFunctionOnNextCall(square);
assertTrue(Object.is(0, square(0)));
assertTrue(Object.is(0, square(-0)));
assertEquals(1, square(-1));
assertEquals(2147395600, square(46340));
assertEquals(2147395600, square(-46340));
// Overflow cases must still deopt and return the exact HeapNumber.
assertEquals(2147488281, square(46341));

function squareNegOverflow(x) {
  x = x | 0;
  return x * x;
}

%PrepareFunctionForOptimization(squareNegOverflow);
assertEquals(4, squareNegOverflow(2));
%OptimizeFunctionOnNextCall(squareNegOverflow);
assertEquals(2147488281, squareNegOverflow(-46341));

function squareMinIntOverflow(x) {
  x = x | 0;
  return x * x;
}

%PrepareFunctionForOptimization(squareMinIntOverflow);
assertEquals(4, squareMinIntOverflow(2));
%OptimizeFunctionOnNextCall(squareMinIntOverflow);
assertEquals(4611686018427387904, squareMinIntOverflow(-2147483648));

// Also exercise the non-squaring path (including -0 deopt) and verify squaring
// does not clobber a still-live operand.
function squareKeepOperand(x, y, return_prod) {
  x = x | 0;
  y = y | 0;
  let sq = x * x;
  let prod = x * y;
  return return_prod ? prod : sq + prod;
}

%PrepareFunctionForOptimization(squareKeepOperand);
assertEquals(15, squareKeepOperand(3, 2, false));
assertEquals(6, squareKeepOperand(3, 2, true));
%OptimizeFunctionOnNextCall(squareKeepOperand);
assertEquals(15, squareKeepOperand(3, 2, false));
assertTrue(Object.is(-0, squareKeepOperand(0, -1, true)));
