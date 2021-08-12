// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
'use strict';

const BENCHMARK_NAME = arguments[0];
const TEST_TYPE = arguments[1];
const optimize_param = arguments[2];
let optimize;
if (optimize_param == "opt") {
  optimize = true;
} else if (optimize_param == "noopt"){
  optimize = false;
} else {
  throw new Error("Unknown optimization configuration " + arguments.join(' '));
}

let klass;
let i = 0;
let array;

class SinglePublicFieldClass {
  x = i;

  check() {
    return this.x === i;
  }
}

class MultiPublicFieldClass {
  x = i;
  y = i+1;
  z = i+2;
  q = i+3;
  r = i+4;
  a = i+5;

  check() {
    return this.x + 1 === this.y && this.y + 1 === this.z &&
           this.z + 1 === this.q && this.q + 1 === this.r &&
           this.r + 1 === this.a;
  }
}

switch (TEST_TYPE) {
  case "single":
    klass = SinglePublicFieldClass;
    break;
  case "multiple":
    klass = MultiPublicFieldClass;
    break;
  default:
    throw new Error("Unknown optimization configuration " + arguments.join(' '));
}

if (optimize) {
  %PrepareFunctionForOptimization(klass);
} else {
  %NeverOptimizeFunction(klass);
}

function setUp() {
  array = [new klass(), new klass()];
  // Populate the array first to reduce the impact of
  // array allocations.
  for (let i = 0; i < LOCAL_ITERATIONS - 2; ++i) {
    array.push(array[0]);
  }
  if (optimize) {
    %OptimizeFunctionOnNextCall(klass);
  }
}

function runBenchmark() {
  for (let i = 0; i < LOCAL_ITERATIONS; ++i) {
    array[i] = new klass();
  }
}

function tearDown() {
  if (array.length < 3) {
    throw new Error(`Check failed, array length ${array.length}`);
  }

  for (const instance of array) {
    if (!instance.check())
      throw new Error(`instance.check() failed`);
  }
}

const DETERMINISTIC_RUNS = 1;
const LOCAL_ITERATIONS = 10000;
new BenchmarkSuite(`${BENCHMARK_NAME}`, [1000], [
  new Benchmark(
    `${BENCHMARK_NAME}-${TEST_TYPE}-${optimize_param}`,
    false, false, DETERMINISTIC_RUNS, runBenchmark, setUp, tearDown)
]);
