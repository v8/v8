// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags:  --allow-natives-syntax

function F0() {}

new F0();

function foo() {
  for (let i = 0; i < 5; i++) {
    for (let j = 0; j < 5; j++) { }
    i instanceof F0;
    i += 14;
    const o = { set e(x) {} };
  }
}

%PrepareFunctionForOptimization(foo);
foo();

%OptimizeFunctionOnNextCall(foo);
foo();
