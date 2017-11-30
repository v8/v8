// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-bigint


function foo(x) {
  return Number(x);
}
foo(42n);
%OptimizeFunctionOnNextCall(foo);
assertEquals(42, foo(42n));


function bar(x) {
  return !x;
}
bar(0n);
%OptimizeFunctionOnNextCall(bar);
assertSame(true, bar(0n));
assertSame(false, bar(42n));
assertSame(false, bar(73786976294838206464n));
