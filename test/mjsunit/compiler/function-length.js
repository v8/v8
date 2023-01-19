// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

function f(g) {
  return g.length;
}
function g(x, y) {}
function h(x, y, z) {}
function OptimizeAndTest(fn) {
  %PrepareFunctionForOptimization(fn);
  assertEquals(1, fn(f));
  assertEquals(2, fn(g));
  assertEquals(3, fn(h));

  %OptimizeFunctionOnNextCall(fn);
  fn(g);
  assertOptimized(fn);

  assertEquals(1, fn(f));
  assertEquals(2, fn(g));
  assertEquals(3, fn(h));
  assertOptimized(fn);

  assertEquals(3, fn('abc'));
  assertUnoptimized(fn);
}

OptimizeAndTest(f);
