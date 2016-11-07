// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function foo() { return arguments[0]; }

// Warm up with monomorphic access.
foo(0);
foo(1);
%BaselineFunctionOnNextCall(foo);
foo(2);
foo(3);
%OptimizeFunctionOnNextCall(foo);

// Mess with out-of-bounds accesses.
for (var i = 0; i < 50000; ++i) {
  foo();
}

// Optimization shall stabilize now.
var count = %GetOptimizationCount(foo);
for (var i = 0; i < 50000; ++i) {
  foo();
}
assertEquals(count, %GetOptimizationCount(foo));
