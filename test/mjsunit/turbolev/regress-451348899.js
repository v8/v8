// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function foo() {
    const v3 = Math / 0;
    let v5 = 0;
    v5++;
    const v7 = v3 ? 1 : v5;
    v7 >> v7;
    return foo;
}

%PrepareFunctionForOptimization(foo);
foo();

%OptimizeFunctionOnNextCall(foo);
foo();
