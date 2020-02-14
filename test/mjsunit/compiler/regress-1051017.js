// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax


function foo1() {
  var x = -Infinity;
  var i = 0;
  for (; i < 1; i += x) {
    if (i == -Infinity) x = +Infinity;
  }
  return i;
}

%PrepareFunctionForOptimization(foo1);
assertEquals(NaN, foo1());
assertEquals(NaN, foo1());
%OptimizeFunctionOnNextCall(foo1);
assertEquals(NaN, foo1());


function foo2() {
  var i = -Infinity;
  for (; i <= 42; i += Infinity) { }
  return i;
}

%PrepareFunctionForOptimization(foo2);
assertEquals(NaN, foo2());
assertEquals(NaN, foo2());
%OptimizeFunctionOnNextCall(foo2);
assertEquals(NaN, foo2());
