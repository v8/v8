// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const objs = [];
for (let i = 0; i < 8; i++) {
  const o = {prototype: 700 + i};
  o['t' + i] = i;
  objs.push(o);
}

// Single-function case: both `o.prototype` loads share the same feedback slot.
// `fn.prototype` is not materialized yet, so JSNativeContextSpecialization
// leaves it as JSLoadNamed, which reaches JSGenericLowering with cached
// homomorphic feedback.
(function testSingleFunction() {
  function fn() {}
  function f(o, skip) {
    let r = o.prototype;
    if (skip) return r;
    o = fn;
    return o.prototype;
  }

  %PrepareFunctionForOptimization(f);
  for (const o of objs) {
    assertEquals(o.prototype, f(o, true));
  }

  %OptimizeFunctionOnNextCall(f);
  assertEquals(objs[0].prototype, f(objs[0], true));
  const proto = f(objs[0], false);
  assertSame(fn.prototype, proto);
})();

// Inlined case: `ld` is inlined twice with the same FeedbackSource.
(function testInlined() {
  function fn() {}
  function ld(o, skip) {
    if (skip) return 0;
    return o.prototype;
  }
  function caller(o, skip) {
    return [ld(o, false), ld(fn, skip)];
  }

  %PrepareFunctionForOptimization(ld);
  %PrepareFunctionForOptimization(caller);
  for (const o of objs) {
    assertEquals([o.prototype, 0], caller(o, true));
  }

  %OptimizeFunctionOnNextCall(caller);
  assertEquals([objs[0].prototype, 0], caller(objs[0], true));
  const res = caller(objs[0], false);
  assertEquals(objs[0].prototype, res[0]);
  assertSame(fn.prototype, res[1]);
})();
