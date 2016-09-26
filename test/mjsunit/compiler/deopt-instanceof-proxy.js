// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function A() {}

function foo(a) {
  return a instanceof A;
}

var a = new A;
var p = new Proxy(a, {});
foo(a);
foo(a);
%OptimizeFunctionOnNextCall(foo);
foo(a);
assertOptimized(foo);
foo(p);
foo(p);
%OptimizeFunctionOnNextCall(foo);
foo(p);
assertOptimized(foo);
