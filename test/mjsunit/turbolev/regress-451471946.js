// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags:  --allow-natives-syntax --jit-fuzzing

function foo(a1) {
    for (let i4 = (() => {
            let v2 = 1.7976931348623157e+308;
            v2++;
            return v2;
        })();
        (() => {
            i4++;
            const v6 = i4 >>> i4;
            v6 * v6;
            return i4 < v6;
        })();
        ) {
    }
    return foo;
}

%PrepareFunctionForOptimization(foo);
foo();
new Uint8Array(536870912);
try { Object.keys(); } catch (e) {}

%OptimizeFunctionOnNextCall(foo);
foo();
