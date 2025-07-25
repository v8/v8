// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const x = Math.atanh();

function foo() {
    const arr = Array();
    arr[153] = x;
    %PrepareFunctionForOptimization(foo);
    %OptimizeMaglevOnNextCall(foo);
}

for(let i = 0; i < 3; ++i) {
  foo();
}
