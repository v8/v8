// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var k = "x";
var o1 = {x: 10};
var o2 = {x: 11, y: 20};
var o3 = {x: 12, y: 20, z: 100};

function f(o) {
  var result = 0;
  for (var i = 0; i < 100; i++) {
    result += o[k];
  }
  return result;
}

f(o1);
f(o1);
f(o1);
%OptimizeFunctionOnNextCall(f);
assertEquals(1000, f(o1));

f(o2);
f(o2);
f(o2);
%OptimizeFunctionOnNextCall(f);
assertEquals(1100, f(o2));

f(o3);
f(o3);
f(o3);
%OptimizeFunctionOnNextCall(f);
assertEquals(1200, f(o3));
