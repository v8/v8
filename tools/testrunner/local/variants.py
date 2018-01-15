# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Use this to run several variants of the tests.
ALL_VARIANT_FLAGS = {
  "default": [[]],
  "future": [["--future"]],
  "liftoff": [["--liftoff"]],
  "minor_mc": [["--minor-mc"]],
  "slow_path": [["--force-slow-path"]],
  "stress": [["--stress-opt", "--always-opt"]],
  "stress_incremental_marking":  [["--stress-incremental-marking"]],
  # No optimization means disable all optimizations. OptimizeFunctionOnNextCall
  # would not force optimization too. It turns into a Nop. Please see
  # https://chromium-review.googlesource.com/c/452620/ for more discussion.
  "nooptimization": [["--noopt"]],
  "stress_background_compile": [["--background-compile", "--stress-background-compile"]],
  # Trigger stress sampling allocation profiler with sample interval = 2^14
  "stress_sampling": [["--stress-sampling-allocation-profiler=16384"]],
  "trusted": [["--no-untrusted-code-mitigations"]],
  "wasm_traps": [["--wasm_trap_handler", "--invoke-weak-callbacks", "--wasm-jit-to-native"]],

  # Alias of exhaustive variants, but triggering new test framework features.
  "infra_staging": [[]],
}

ALL_VARIANTS = set(ALL_VARIANT_FLAGS.keys())
