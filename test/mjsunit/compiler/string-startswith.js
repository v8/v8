// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt

(function() {
  function foo(string) { return string.startsWith('a'); }

  %PrepareFunctionForOptimization(foo);
  assertEquals(false, foo(''));
  assertEquals(true, foo('a'));
  assertEquals(false, foo('ba'));
  assertEquals(true, foo('abc'));
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(false, foo(''));
  assertEquals(true, foo('a'));
  assertEquals(false, foo('ba'));
  assertEquals(true, foo('abc'));
  assertOptimized(foo);
})();
