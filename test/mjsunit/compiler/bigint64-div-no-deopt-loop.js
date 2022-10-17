// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

(function OptimizeAndTestDivZero() {
  function f(x, y) {
    return x / y;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(0n, f(0n, 1n));
  assertEquals(-3n, f(-32n, 9n));
  %OptimizeFunctionOnNextCall(f);
  assertEquals(2n, f(14n, 5n));
  assertOptimized(f);
  // CheckedBigInt64Div will trigger deopt due to divide-by-zero.
  assertThrows(() => f(42n, 0n), RangeError);
  if (%Is64Bit()) {
    assertUnoptimized(f);

    %PrepareFunctionForOptimization(f);
    assertEquals(0n, f(0n, 1n));
    assertEquals(-3n, f(-32n, 9n));
    %OptimizeFunctionOnNextCall(f);
    assertEquals(2n, f(14n, 5n));
    assertOptimized(f);
    // Ensure there is no deopt loop.
    assertThrows(() => f(42n, 0n), RangeError);
    assertOptimized(f);
  }
})();

(function OptimizeAndTestOverflow() {
  function f(x, y) {
    // The overflow can only happen when the dividend is -(2n ** 63n) which is
    // out of the range of small BigInts but there is no check in-between
    // CheckedBigInt64Ops.
    return (x - 1n) / y;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(0n, f(1n, 1n));
  assertEquals(-3n, f(-32n, 9n));
  %OptimizeFunctionOnNextCall(f);
  assertEquals(2n, f(15n, 5n));
  assertOptimized(f);
  assertEquals(-(2n ** 63n), f(-(2n ** 63n) + 1n, 1n));
  assertOptimized(f);
  // CheckedBigInt64Div will trigger deopt due to overflow.
  assertEquals(2n ** 63n, f(-(2n ** 63n) + 1n, -1n));
  if (%Is64Bit()) {
    assertUnoptimized(f);

    %PrepareFunctionForOptimization(f);
    assertEquals(0n, f(1n, 1n));
    assertEquals(-3n, f(-32n, 9n));
    %OptimizeFunctionOnNextCall(f);
    assertEquals(2n, f(15n, 5n));
    assertOptimized(f);
    // Ensure there is no deopt loop.
    assertEquals(2n ** 63n, f(-(2n ** 63n) + 1n, -1n));
    assertOptimized(f);
  }
})();
