// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function test(e, f, v) {
  assertEquals(e, f(v));
  assertEquals(e, f(v));
  assertEquals(e, f(v));
}

function foo(t) {
  for (var x in t) {
    for (var i = 0; i < 3; i++) {
      %OptimizeOsr(0, "concurrent");
      %PrepareFunctionForOptimization(foo);
    }
  }
  return 5;
}
%PrepareFunctionForOptimization(foo);

test(5, foo, {x:20});

function bar(t) {
  var sum = 0;
  for (var x in t) {
    for (var i = 0; i < 3; i++) {
      %OptimizeOsr(0, "concurrent");
      sum += t[x];
      %PrepareFunctionForOptimization(bar);
    }
  }
  return sum;
}
%PrepareFunctionForOptimization(bar);

test(93, bar, {x:20,y:11});
