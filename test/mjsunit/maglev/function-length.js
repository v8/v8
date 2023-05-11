// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-turbofan --no-always-turbofan --maglev

function f(g) {
  return g.length;
}
function g(x, y) {}
function h(x, y, z) {}
eval("function huge(a" + ",a".repeat(32767) + "){}");
function OptimizeAndTest(fn) {
  %PrepareFunctionForOptimization(fn);
  assertEquals(1, fn(f));
  assertEquals(2, fn(g));
  assertEquals(3, fn(h));

  %OptimizeMaglevOnNextCall(fn);
  fn(g);
  assertOptimized(fn);

  assertEquals(1, fn(f));
  assertEquals(2, fn(g));
  assertEquals(3, fn(h));
  assertEquals(32768, fn(huge));
  assertOptimized(fn);

  assertEquals(3, fn('abc'));
  assertUnoptimized(fn);
}

OptimizeAndTest(f);

function fn() {
}
function assign(v) {
    v[v.length] = v.length;
}
var v = {length: 42};
%PrepareFunctionForOptimization(assign);
assign(fn);
assign(v);
%OptimizeMaglevOnNextCall(assign);
assign(v);
assertOptimized(assign);

class C extends Function {
   f () {
       return super.length;
   }
}
%PrepareFunctionForOptimization(C.prototype.f);
assertEquals(0, (new C).f());
%OptimizeMaglevOnNextCall(C.prototype.f);
assertEquals(0, (new C).f());

function fun(f) { return f.length; }
delete Function.prototype.length;
%PrepareFunctionForOptimization(fun);
assertEquals(undefined, fun(Function.prototype));
%OptimizeFunctionOnNextCall(fun);
assertEquals(undefined, fun(Function.prototype));
