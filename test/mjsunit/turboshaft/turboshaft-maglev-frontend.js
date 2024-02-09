// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

// TODO(dmercadier): re-allow optimization of these functions once the
// maglev-to-turboshaft graph builder supports everything they need.
%NeverOptimizeFunction(assertEquals);
%NeverOptimizeFunction(assertOptimized);
%NeverOptimizeFunction(assertUnoptimized);

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
assertOptimized(cmp);

function bitwise_smi(a, b) {
  let x = a | b;
  x = x & 52358961;
  x = x ^ b;
  x = x >> 2;
  x = x << 5;
  x = x >>> 1;
  return ~x;
}
%PrepareFunctionForOptimization(bitwise_smi);
assertEquals(bitwise_smi(1548, 45235), -23041);
%OptimizeFunctionOnNextCall(bitwise_smi);
assertEquals(bitwise_smi(1548, 45235), -23041);
assertOptimized(bitwise_smi);

function simple_loop(x) {
  let s = 0;
  for (let i = 0; i < 4; i++) {
    s += i + x;
  }
  return s;
}
%PrepareFunctionForOptimization(simple_loop);
assertEquals(simple_loop(17), 74);
%OptimizeFunctionOnNextCall(simple_loop);
assertEquals(simple_loop(17), 74);
assertOptimized(simple_loop);

function load_smi_arr(arr, idx) {
  return arr[1] + arr[idx];
}
{
  let smi_arr = [1, 2, 3, 4, {}];
  %PrepareFunctionForOptimization(load_smi_arr);
  assertEquals(load_smi_arr(smi_arr, 3), 6);
  %OptimizeFunctionOnNextCall(load_smi_arr);
  assertEquals(load_smi_arr(smi_arr, 3), 6);
  assertOptimized(load_smi_arr);

  // String indices currently work without requiring deopt.
  assertEquals(load_smi_arr(smi_arr, '2'), 5);
  assertOptimized(load_smi_arr);
}

function load_double_arr(arr, idx) {
  return arr[2] + arr[idx];
}
{
  let double_arr = [1.552, 2.425, 3.526, 4.596, 5.986, 6.321];
  %PrepareFunctionForOptimization(load_double_arr);
  assertEquals(load_double_arr(double_arr, 3), 8.122);
  %OptimizeFunctionOnNextCall(load_double_arr);
  assertEquals(load_double_arr(double_arr, 3), 8.122);
  assertOptimized(load_double_arr);

  // String indices currently work without requiring deopt.
  assertEquals(load_double_arr(double_arr, '1'), 5.951);
  assertOptimized(load_double_arr);
}
