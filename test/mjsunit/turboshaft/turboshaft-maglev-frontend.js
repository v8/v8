// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function math_smi(x, y) {
  let a = x * y;
  a = a + 152;
  a = a / x;
  a = a - y;
  a = a % 5;
  return a;
}

%PrepareFunctionForOptimization(math_smi);
assertEquals(math_smi(4, 3), 3);
%OptimizeFunctionOnNextCall(math_smi);
assertEquals(math_smi(4, 3), 3);
assertOptimized(math_smi);
assertEquals(NaN, math_smi("a", "b"));
assertUnoptimized(math_smi);

function math_float(x, y) {
  let a = x * y;
  let b = a + 152.56;
  let c = b / x;
  let e = c - Math.round(y);
  let f = e % 5.56;
  let g = f ** x;
  let h = -g;
  return h;
}

%PrepareFunctionForOptimization(math_float);
assertEquals(math_float(4.21, 3.56), -42.56563728706824);
%OptimizeFunctionOnNextCall(math_float);
assertEquals(math_float(4.21, 3.56), -42.56563728706824);
assertOptimized(math_float);

function cmp(which, a, b) {
  if (which == 0) { return a > b; }
  if (which == 1) { return a >= b; }
  if (which == 2) { return a < b; }
  if (which == 3) { return a <= b; }
}

%PrepareFunctionForOptimization(cmp);
// >
assertEquals(cmp(0, 10, 20), false);
assertEquals(cmp(0, 20, 10), true);
assertEquals(cmp(0, 15, 15), false);
// >=
assertEquals(cmp(1, 10, 20), false);
assertEquals(cmp(1, 20, 10), true);
assertEquals(cmp(1, 15, 15), true);
// <
assertEquals(cmp(2, 10, 20), true);
assertEquals(cmp(2, 20, 10), false);
assertEquals(cmp(2, 15, 15), false);
// <=
assertEquals(cmp(3, 10, 20), true);
assertEquals(cmp(3, 20, 10), false);
assertEquals(cmp(3, 15, 15), true);

%OptimizeFunctionOnNextCall(cmp);
// >
assertEquals(cmp(0, 10, 20), false);
assertEquals(cmp(0, 20, 10), true);
assertEquals(cmp(0, 15, 15), false);
// >=
assertEquals(cmp(1, 10, 20), false);
assertEquals(cmp(1, 20, 10), true);
assertEquals(cmp(1, 15, 15), true);
// <
assertEquals(cmp(2, 10, 20), true);
assertEquals(cmp(2, 20, 10), false);
assertEquals(cmp(2, 15, 15), false);
// <=
assertEquals(cmp(3, 10, 20), true);
assertEquals(cmp(3, 20, 10), false);
assertEquals(cmp(3, 15, 15), true);
