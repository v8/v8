// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --always-specialize-for-script-context --maglev
// Flags: --allow-natives-syntax

const scriptConst = 42;

function createClosure() {
  return function foo() {
    return scriptConst;
  };
}

// Create the closure twice to avoid function context specialization.
const f1 = createClosure();
const f2 = createClosure();

%PrepareFunctionForOptimization(f1);
assertEquals(42, f1());
%OptimizeMaglevOnNextCall(f1);
assertEquals(42, f1());
assertMaglevved(f1);

%PrepareFunctionForOptimization(f2);
assertEquals(42, f2());
%OptimizeMaglevOnNextCall(f2);
assertEquals(42, f2());
assertMaglevved(f2);

function createClosure2() {
  return function bar() {
    return scriptConst;
  };
}

// Create and optimize the first closure (with function context specialization),
// then create and optimize a second closure (transitioning from
// one_closure_cell to many_closures_cell and recompiling without function
// context specialization).
const g1 = createClosure2();
%PrepareFunctionForOptimization(g1);
assertEquals(42, g1());
%OptimizeMaglevOnNextCall(g1);
assertEquals(42, g1());
assertMaglevved(g1);

const g2 = createClosure2();
%PrepareFunctionForOptimization(g2);
assertEquals(42, g2());
%OptimizeMaglevOnNextCall(g2);
assertEquals(42, g2());
assertMaglevved(g2);
assertEquals(42, g1());
assertMaglevved(g1);

// Test inlining a known constant JSFunction from another script (with its own
// ScriptContext and FunctionContext).
const otherScriptFn = Realm.eval(Realm.current(), `
  const otherScriptConst = 100;
  let otherScriptLet = 200;
  (function makeOther() {
    let otherFuncVar = 300;
    return function otherInlined(x) {
      return otherScriptConst + otherScriptLet + otherFuncVar + x;
    };
  })();
`);

function callOtherScriptFn(x) {
  return otherScriptFn(x) + scriptConst;
}
%PrepareFunctionForOptimization(otherScriptFn);
%PrepareFunctionForOptimization(callOtherScriptFn);
assertEquals(643, callOtherScriptFn(1));
assertEquals(644, callOtherScriptFn(2));
%OptimizeMaglevOnNextCall(callOtherScriptFn);
assertEquals(645, callOtherScriptFn(3));
assertMaglevved(callOtherScriptFn);

// Test inlining a closure created via FastCreateClosure inside a non-FCI caller
// and inside a many-closures caller.
function makeCallerWithFastCreateClosure(base) {
  return function caller(x) {
    const inner = (y) => base + scriptConst + y;
    return inner(x);
  };
}
const c1 = makeCallerWithFastCreateClosure(10);
%PrepareFunctionForOptimization(c1);
assertEquals(53, c1(1));
assertEquals(54, c1(2));
%OptimizeMaglevOnNextCall(c1);
assertEquals(55, c1(3));
assertMaglevved(c1);

const c2 = makeCallerWithFastCreateClosure(20);
%PrepareFunctionForOptimization(c2);
assertEquals(63, c2(1));
assertEquals(64, c2(2));
%OptimizeMaglevOnNextCall(c2);
assertEquals(65, c2(3));
assertMaglevved(c2);
assertEquals(55, c1(3));

// Test inlining via FeedbackCell (dynamic closure with different
// FunctionContext instances, sharing the same ScriptContext).
function makeDynamicInner(secret) {
  return function dynamicInner(x) {
    return secret + scriptConst + x;
  };
}
const dyn1 = makeDynamicInner(100);
const dyn2 = makeDynamicInner(200);
function callDynamicInner(fn, x) {
  return fn(x);
}
%PrepareFunctionForOptimization(dyn1);
%PrepareFunctionForOptimization(dyn2);
%PrepareFunctionForOptimization(callDynamicInner);
assertEquals(143, callDynamicInner(dyn1, 1));
assertEquals(244, callDynamicInner(dyn2, 2));
%OptimizeMaglevOnNextCall(callDynamicInner);
assertEquals(145, callDynamicInner(dyn1, 3));
assertEquals(246, callDynamicInner(dyn2, 4));
assertMaglevved(callDynamicInner);
