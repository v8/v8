// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt --no-always-opt

(function() {
  function foo(i) { return "a".charAt(i); }

  assertEquals("a", foo(0));
  assertEquals("a", foo(0));
  assertEquals("", foo(1));
  %OptimizeFunctionOnNextCall(foo);
  assertEquals("", foo(1));
  assertOptimized(foo);
})();

(function() {
  function foo(i) { return "a".charCodeAt(i); }

  assertEquals(97, foo(0));
  assertEquals(97, foo(0));
  assertEquals(NaN, foo(1));
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(NaN, foo(1));
  assertOptimized(foo);
})();
