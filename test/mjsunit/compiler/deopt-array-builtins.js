// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt

(function testForEach() {
    function f(v,n,o) {
        Object.freeze(o);
    }
    function g() {
        [1,2,3].forEach(f);
    }
    g();
    g();
    %OptimizeFunctionOnNextCall(g);
    g();
    %OptimizeFunctionOnNextCall(g);
    g();
    assertOptimized(g);
})();


(function testFind() {
    function f(v,n,o) {
        Object.freeze(o);
        return false;
    }
    function g() {
        [1,2,3].find(f);
    }
    g();
    g();
    %OptimizeFunctionOnNextCall(g);
    g();
    %OptimizeFunctionOnNextCall(g);
    g();
    assertOptimized(g);
})();

(function testMap() {
    function f(v,n,o) {
        Object.freeze(o);
        return false;
    }
    function g() {
        [1,2,3].map(f);
    }
    g();
    g();
    %OptimizeFunctionOnNextCall(g);
    g();
    %OptimizeFunctionOnNextCall(g);
    g();
    assertOptimized(g);
})();

(function testFilter() {
    function f(v,n,o) {
        Object.freeze(o);
        return true;
    }
    function g() {
        [1,2,3].filter(f);
    }
    g();
    g();
    %OptimizeFunctionOnNextCall(g);
    g();
    %OptimizeFunctionOnNextCall(g);
    g();
    assertOptimized(g);
})();
