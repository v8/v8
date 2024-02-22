// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan --no-stress-concurrent-inlining

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
assertEquals(3, math_smi(4, 3));
assertEquals(3, math_smi(4, 3));
%OptimizeFunctionOnNextCall(math_smi);
assertEquals(3, math_smi(4, 3));
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
assertEquals(-42.56563728706824, math_float(4.21, 3.56));
assertEquals(-42.56563728706824, math_float(4.21, 3.56));
%OptimizeFunctionOnNextCall(math_float);
assertEquals(-42.56563728706824, math_float(4.21, 3.56));
assertOptimized(math_float);

function cmp(which, a, b) {
  if (which == 0) { return a > b; }
  if (which == 1) { return a >= b; }
  if (which == 2) { return a < b; }
  if (which == 3) { return a <= b; }
}
%PrepareFunctionForOptimization(cmp);
// >
assertEquals(false, cmp(0, 10, 20));
assertEquals(true, cmp(0, 20, 10));
assertEquals(false, cmp(0, 15, 15));
// >=
assertEquals(false, cmp(1, 10, 20));
assertEquals(true, cmp(1, 20, 10));
assertEquals(true, cmp(1, 15, 15));
// <
assertEquals(true, cmp(2, 10, 20));
assertEquals(false, cmp(2, 20, 10));
assertEquals(false, cmp(2, 15, 15));
// <=
assertEquals(true, cmp(3, 10, 20));
assertEquals(false, cmp(3, 20, 10));
assertEquals(true, cmp(3, 15, 15));

%OptimizeFunctionOnNextCall(cmp);
// >
assertEquals(false, cmp(0, 10, 20));
assertEquals(true, cmp(0, 20, 10));
assertEquals(false, cmp(0, 15, 15));
// >=
assertEquals(false, cmp(1, 10, 20));
assertEquals(true, cmp(1, 20, 10));
assertEquals(true, cmp(1, 15, 15));
// <
assertEquals(true, cmp(2, 10, 20));
assertEquals(false, cmp(2, 20, 10));
assertEquals(false, cmp(2, 15, 15));
// <=
assertEquals(true, cmp(3, 10, 20));
assertEquals(false, cmp(3, 20, 10));
assertEquals(true, cmp(3, 15, 15));
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
assertEquals(-23041, bitwise_smi(1548, 45235));
assertEquals(-23041, bitwise_smi(1548, 45235));
%OptimizeFunctionOnNextCall(bitwise_smi);
assertEquals(-23041, bitwise_smi(1548, 45235));
assertOptimized(bitwise_smi);

function simple_loop(x) {
  let s = 0;
  for (let i = 0; i < 4; i++) {
    s += i + x;
  }
  return s;
}
%PrepareFunctionForOptimization(simple_loop);
assertEquals(74, simple_loop(17));
assertEquals(74, simple_loop(17));
%OptimizeFunctionOnNextCall(simple_loop);
assertEquals(74, simple_loop(17));
assertOptimized(simple_loop);

// Testing field loads
{
  function load_field(o) {
    let x = o.x;
    let y = o.y;
    return x + y;
  }

  let o = { x : 42, y : 15.71 };

  %PrepareFunctionForOptimization(load_field);
  assertEquals(57.71, load_field(o));
  %OptimizeFunctionOnNextCall(load_field);
  assertEquals(57.71, load_field(o));
  assertOptimized(load_field);
}

// Testing array loads
{
  function load_smi_arr(arr, idx) {
    return arr[1] + arr[idx];
  }
  let smi_arr = [1, 2, 3, 4, {}];
  %PrepareFunctionForOptimization(load_smi_arr);
  assertEquals(6, load_smi_arr(smi_arr, 3));
  assertEquals(6, load_smi_arr(smi_arr, 3));
  %OptimizeFunctionOnNextCall(load_smi_arr);
  assertEquals(6, load_smi_arr(smi_arr, 3));
  assertOptimized(load_smi_arr);

  // String indices currently work without requiring deopt.
  assertEquals(5, load_smi_arr(smi_arr, '2'));
  assertOptimized(load_smi_arr);

  function load_double_arr(arr, idx) {
    return arr[2] + arr[idx];
  }
  let double_arr = [1.552, 2.425, 3.526, 4.596, 5.986, 6.321];
  %PrepareFunctionForOptimization(load_double_arr);
  assertEquals(8.122, load_double_arr(double_arr, 3));
  assertEquals(8.122, load_double_arr(double_arr, 3));
  %OptimizeFunctionOnNextCall(load_double_arr);
  assertEquals(8.122, load_double_arr(double_arr, 3));
  assertOptimized(load_double_arr);

  // String indices currently work without requiring deopt.
  assertEquals(5.951, load_double_arr(double_arr, '1'));
  assertOptimized(load_double_arr);

  function load_holey_fixed_double(arr, idx) {
    return arr[idx];
  }
  let holey_double_arr = [2.58,3.41,,4.55];

  %PrepareFunctionForOptimization(load_holey_fixed_double);
  assertEquals(3.41, load_holey_fixed_double(holey_double_arr, 1));
  %OptimizeFunctionOnNextCall(load_holey_fixed_double);
  assertEquals(3.41, load_holey_fixed_double(holey_double_arr, 1));
  assertOptimized(load_holey_fixed_double);
  // Loading a hole should trigger a deopt
  assertEquals(undefined, load_holey_fixed_double(holey_double_arr, 2));
  assertUnoptimized(load_holey_fixed_double);

  // Reoptimizing, holes should now be handled
  %OptimizeMaglevOnNextCall(load_holey_fixed_double);
  assertEquals(3.41, load_holey_fixed_double(holey_double_arr, 1));
  %OptimizeFunctionOnNextCall(load_holey_fixed_double);
  assertEquals(3.41, load_holey_fixed_double(holey_double_arr, 1));
  assertEquals(undefined, load_holey_fixed_double(holey_double_arr, 2));
  assertOptimized(load_holey_fixed_double);
}

// Simple JS function call
{
  %NeverOptimizeFunction(h);
  function h(x) { return x; }
  function g(x) { return h(x); }
  function f(x) { return g(x); }

  %PrepareFunctionForOptimization(g);
  %PrepareFunctionForOptimization(f);
  assertEquals(42, f(42));
  assertEquals(42, f(42));
  %OptimizeFunctionOnNextCall(f);
  assertEquals(42, f(42));
  assertOptimized(f);
}

// Simple JS call with receiver
{
  function f(o) { return o.x(17); }

  let o = { y : 42, x : function(a) { return a + this.y; } };
  %NeverOptimizeFunction(o.x);

  %PrepareFunctionForOptimization(f);
  assertEquals(59, f(o));
  assertEquals(59, f(o));
  %OptimizeFunctionOnNextCall(f);
  assertEquals(59, f(o));
  assertOptimized(f);
}

// Lazy deopt during JS function call
{
  %NeverOptimizeFunction(h);
  function h(x, d) {
    if (d == 2) { return f(x, d-1); }
    if (d == 1) {
      // Calling `f` with a string as input will trigger an eager deopt of `f`,
      // which will also trigger a lazy deopt of all instances `f` on the caller
      // stack.
      return f("str", d-1);
    }
    return x;
  }

  function g(x, d) {
    let tmp = x * 12;
    let v = h(x, d);
    return tmp + v;
  }

  function f(x, d) {
    let a = x + 2;
    return g(a, d);
  }

  %PrepareFunctionForOptimization(f);
  %PrepareFunctionForOptimization(g);
  assertEquals(572, f(42, 0));
  assertEquals(572, f(42, 0));

  %OptimizeFunctionOnNextCall(f);
  assertEquals(572, f(42, 0));
  assertOptimized(f);
  assertEquals("528552NaNstr2", f(42, 2));
  assertUnoptimized(f);
}

// Testing deopt with raw floats and raw integers in the frame state.
{
  %NeverOptimizeFunction(sum);
  function sum(...args) {
    return args.reduce((a,b) => a + b, 0);
  }

  function f(a, b, c) {
    let x = a * 4.25;
    let y = b * 17;
    // This call to `sum` causes `x` and `y` to be part of the frame state.
    let s = sum(a, b);
    let z = b + c;
    // This call is just to use the values we computed before.
    return sum(s, x, y, z);
  }

  %PrepareFunctionForOptimization(f);
  assertEquals(113.39, f(2.36, 5, 6));
  assertEquals(113.39, f(2.36, 5, 6));

  %OptimizeFunctionOnNextCall(f);
  assertEquals(113.39, f(2.36, 5, 6));
  assertOptimized(f);
  assertEquals(113.93, f(2.36, 5, 6.54));
  assertUnoptimized(f);
}

// Testing exceptions.
{
  function h(x) {
    if (x) { willThrow(); }
    else { return 17; }
  }
  %NeverOptimizeFunction(h);

  function f(a, b) {
    let r = a;
    try {
      r = h(a);
      return h(b) + r;
    }
    catch {
      return r * b;
    }
  }

  %PrepareFunctionForOptimization(f);
  assertEquals(34, f(0, 0)); // Won't cause an exception
  assertEquals(187, f(0, 11)); // Will cause an exception on the 2nd call to h
  assertEquals(0, f(7, 0)); // Will cause an exception on the 1st call to h
  %OptimizeFunctionOnNextCall(f);
  assertEquals(34, f(0, 0));
  assertEquals(187, f(0, 11));
  assertEquals(0, f(7, 0));
  assertOptimized(f);
}

// Testing builtin calls
{
  // String comparison (which are currently done with builtin calls in Maglev).
  function cmp_str(a, b) {
    return a < b;
  }

  %PrepareFunctionForOptimization(cmp_str);
  assertEquals(true, cmp_str("abc", "def"));
  %OptimizeFunctionOnNextCall(cmp_str);
  assertEquals(true, cmp_str("abc", "def"));
  assertOptimized(cmp_str);

  // Megamorphic load.
  function load(o) {
    return o.x;
  }

  let o1 = { x : 42 };
  let o2 = { a : {}, x : 2.5 };
  let o3 = 42;
  let o4 = { b : 42, c: {}, x : 5.35 };
  let o5 = { u : 14, c : 2.28, d: 4.2, x : 5 };

  %PrepareFunctionForOptimization(load);
  assertEquals(42, load(o1));
  assertEquals(2.5, load(o2));
  assertEquals(undefined, load(o3));
  assertEquals(5.35, load(o4));
  assertEquals(5, load(o5));
  %OptimizeFunctionOnNextCall(load);
  assertEquals(42, load(o1));
  assertEquals(2.5, load(o2));
  assertEquals(undefined, load(o3));
  assertEquals(5.35, load(o4));
  assertEquals(5, load(o5));
  assertOptimized(load);

  // charAt is a builtin call but is done with a CallKnowJSFunctionCall
  function string_char_at(s) {
    return s.charAt(2);
  }

  %PrepareFunctionForOptimization(string_char_at);
  assertEquals("c", string_char_at("abcdef"));
  %OptimizeFunctionOnNextCall(string_char_at);
  assertEquals("c", string_char_at("abcdef"));
  assertOptimized(string_char_at);
}

// Testing stores
{
  function store_field(o, v) {
    o.x = 17; // Tagged field, no write barrier
    o.y = v; // Tagged field, with write barrier
    o.z = 12.29; // Double field
    return o;
  }

  let o = { x : 42, y : 10, z : 14.58 };

  %PrepareFunctionForOptimization(store_field);
  assertEquals({ x : 17, y : undefined, z : 12.29 }, store_field(o));
  o = { x : 42, y : 10, z : 14.58 }; // Resetting {o}
  %OptimizeFunctionOnNextCall(store_field);
  assertEquals({ x : 17, y : undefined, z : 12.29 }, store_field(o));
  assertOptimized(store_field);

  function store_arr(obj_arr, double_arr) {
    obj_arr[0] = 42; // FixedArray, no write barrier
    obj_arr[1] = double_arr; // FixedArray, with write barrier
    double_arr[1] = 42.25; // DoubleFixedArray
  }

  let obj_arr = [0, {}, 2];
  let double_arr = [1.56, 2.68, 3.51];

  %PrepareFunctionForOptimization(store_arr);
  store_arr(obj_arr, double_arr);
  assertEquals([42, double_arr, 2], obj_arr);
  assertEquals([1.56, 42.25, 3.51], double_arr);

  // Resetting {obj_arr} and {double_arr}
  obj_arr[0] = 0;
  obj_arr[1] = {};
  double_arr[1] = 2.68;

  %OptimizeFunctionOnNextCall(store_arr);
  store_arr(obj_arr, double_arr);
  assertEquals([42, double_arr, 2], obj_arr);
  assertEquals([1.56, 42.25, 3.51], double_arr);
  assertOptimized(store_arr);
}
