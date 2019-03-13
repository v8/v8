// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// String.p.startsWith(x, NaN) shouldn't crash V8 when optimized.
(function () {
  function f() { 'a'.startsWith('a', NaN); }

  %PrepareFunctionForOptimization(f);
  f();
  f();
  %OptimizeFunctionOnNextCall(f);
  f();
})();

// String.p.startsWith should try to coerce non-numbers to numbers.
(function() {
  let wasCalled = false;

  const obj = {
    [Symbol.toPrimitive]: () => wasCalled = true
  };

  function f() { ''.startsWith('a', obj); }

  %PrepareFunctionForOptimization(f);
  f();
  f();
  %OptimizeFunctionOnNextCall(f);
  f();

  assertTrue(wasCalled, "String.p.startsWith didn't attempt to coerce the position argument to a Number.")
})();
