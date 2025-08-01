// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax
// Flags: --maglev-poly-calls

function foo(o) {
  try {
    for (let p of o) {}
  } catch (e) {}
}
%PrepareFunctionForOptimization(foo);

foo(() => {});
foo([1, 2, 3]);

%OptimizeMaglevOnNextCall(foo);
foo();
