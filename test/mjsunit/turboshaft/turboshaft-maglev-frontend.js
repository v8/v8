// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function math_smi(x) {
  return x + x;
}

%PrepareFunctionForOptimization(math_smi);
assertEquals(6, math_smi(3));
%OptimizeFunctionOnNextCall(math_smi);
assertEquals(6, math_smi(3));
assertOptimized(math_smi);
assertEquals("aa", math_smi("a"));
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
