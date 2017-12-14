// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt

(function test() {
  function foo(a) { a.push(a.length = 2); }

  foo([1]);
  foo([1]);
  %OptimizeFunctionOnNextCall(foo);
  foo([1]);
  %OptimizeFunctionOnNextCall(foo);
  foo([1]);
  assertOptimized(foo);
})();

(function test() {
  function bar(a) { a.x = 2 };
  %NeverOptimizeFunction(bar);
  function foo(a) { a.push(bar(a)); }

  foo(["1"]);
  foo(["1"]);
  %OptimizeFunctionOnNextCall(foo);
  foo(["1"]);
  %OptimizeFunctionOnNextCall(foo);
  foo(["1"]);
  assertOptimized(foo);
})();

(function test() {
  function foo(a) { a.push(a.length = 2); }

  foo([0.34234]);
  foo([0.34234]);
  %OptimizeFunctionOnNextCall(foo);
  foo([0.34234]);
  %OptimizeFunctionOnNextCall(foo);
  foo([0.34234]);
  assertOptimized(foo);
})();
