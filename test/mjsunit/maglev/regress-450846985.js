// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function foo(x) {
  let y = x == 1 ? 1 : 2;

  try {
    while (y < 10) {
      y += 2;
      throwBecauseCallingUndefinedFunction();
    }
  } catch (e) {}
}

%PrepareFunctionForOptimization(foo);
foo(1);

%OptimizeMaglevOnNextCall(foo);
foo();
