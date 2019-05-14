// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// Check that load elimination on const-marked fields works
(function() {
    function maybe_sideeffect(b) { return 42; }

    function f(k) {
        let b = { value: k };
        maybe_sideeffect(b);
        let v1 = b.value;
        maybe_sideeffect(b);
        let v2 = b.value;
        %TurbofanStaticAssert(v1 == v2);
        // TODO(gsps): Improve analysis to also propagate stored value
        //   Eventually, this should also work:
        // %TurbofanStaticAssert(v2 == k);
    }

    %NeverOptimizeFunction(maybe_sideeffect);
    f(1);
    f(2);
    %OptimizeFunctionOnNextCall(f);
    f(3);
})();
