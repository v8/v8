// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax
// Flags: --turbofan --turboshaft
// --always-turbofan is disabled because we rely on particular feedback for
// optimizing to the fastest path.
// Flags: --no-always-turbofan
// The test relies on optimizing/deoptimizing at predictable moments, so
// it's not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0

load('test/mjsunit/wasm/wasm-module-builder.js');

const fast_c_api = new d8.test.FastCAPI();

(function TestCallCatches() {
  print(arguments.callee.name);
  let numThrow = 0;
  let numNoThrow = 0;

  function callThrowingFunction() {
      fast_c_api.throw_no_fallback();
  }

  %PrepareFunctionForOptimization(callThrowingFunction);
  try {
    callThrowingFunction();
    numNoThrow++;
  } catch (e) {
    numThrow++;
  }
  %OptimizeFunctionOnNextCall(callThrowingFunction);
  fast_c_api.reset_counts();
  try {
    callThrowingFunction();
    numNoThrow++;
  } catch (e) {
    numThrow++;
  }

  assertOptimized(callThrowingFunction);
  assertEquals(1, fast_c_api.fast_call_count());
  assertEquals(0, fast_c_api.slow_call_count());
  assertEquals(2, numThrow);
})();

(function TestFunctionCatches() {
  print(arguments.callee.name);
  let numThrow = 0;

  function checkThrow() {
    try {
      fast_c_api.throw_no_fallback();
    } catch (e) {
      numThrow++;
    }
  }

  %PrepareFunctionForOptimization(checkThrow);
  checkThrow();
  %OptimizeFunctionOnNextCall(checkThrow);
  fast_c_api.reset_counts();
  checkThrow();
  assertOptimized(checkThrow);

  assertEquals(1, fast_c_api.fast_call_count());
  assertEquals(0, fast_c_api.slow_call_count());
  assertEquals(2, numThrow);
})();
