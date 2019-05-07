// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

const ITERATIONS = 10000;

// This dummy ensures that the feedback for benchmark.run() in the Measure function
// from base.js is not monomorphic, thereby preventing the benchmarks below from being inlined.
// This ensures consistent behavior and comparable results.
new BenchmarkSuite('Prevent-Inline-Dummy', [1000], [
  new Benchmark('Prevent-Inline-Dummy', true, false, 0, () => {})
]);

new BenchmarkSuite('BigInt-ToBoolean', [1000], [
  new Benchmark('BigInt-ToBoolean', true, false, 0, TestToBoolean),
]);

new BenchmarkSuite('BigInt-BooleanConstructor', [1000], [
  new Benchmark('BigInt-BooleanConstructor', true, false, 0, TestBooleanConstructor),
]);


function TestBooleanConstructor() {
  let kl = true;
  for (let i = 0; i < ITERATIONS; ++i) {
    // Store to a variable to prevent elimination.
    // Keep a depedency on the loop counter to prevent hoisting.
    kl = Boolean(i % 2 == 0 ? 42n : 32n);
  }
  return kl;
}


function TestToBoolean() {
  let kl = true;
  for (let i = 0; i < ITERATIONS; ++i) {
    // Store to a variable to prevent elimination.
    // Keep a depedency on the loop counter to prevent hoisting.
    kl = (i % 2 == 0 ? 42n : 32n) ? true : false;
  }
  return kl;
}
