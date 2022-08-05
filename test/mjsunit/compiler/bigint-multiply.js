// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

function TestMultiply(a, b) {
  return a * b;
}

function OptimizeAndTest(fn) {
  %PrepareFunctionForOptimization(fn);
  assertEquals(fn(3n, 4n), 12n);
  assertEquals(fn(5n, 6n), 30n);
  %OptimizeFunctionOnNextCall(fn);
  assertEquals(fn(7n, 8n), 56n);
  assertOptimized(fn);
  assertEquals(fn(7, 8), 56);
  assertUnoptimized(fn);
}

OptimizeAndTest(TestMultiply);
