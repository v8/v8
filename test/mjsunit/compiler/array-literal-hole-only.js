// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan

// TurboFan code that creates arrays from hole-only array literals (for example
// `[,,]`) and stores to them. Up to a length of 8, these literals don't have
// copy-on-write elements, so TurboFan allocates the elements inline. Longer
// literals (here 9 and 16) have copy-on-write elements, so the first store
// copies the elements.

let nextId = 0;
function makeFill(length) {
  // Give each generated function a distinct source so `new Function` doesn't
  // hit the eval compilation cache and share a FeedbackCell / AllocationSite
  // across iterations.
  return new Function('x', `
    const a = [${','.repeat(length)}];  // ${nextId++}
    for (let i = 0; i < ${length}; i++) a[i] = x;
    return a;`);
}

function assertFilled(array, length, value) {
  assertEquals(length, array.length);
  for (let i = 0; i < length; i++) assertSame(value, array[i]);
}

for (const length of [1, 2, 4, 8, 9, 16]) {
  for (const value of [1, {}, 1.5]) {
    const f = makeFill(length);
    %PrepareFunctionForOptimization(f);
    // Defer assertFilled until after optimization: its polymorphic KeyedLoadIC
    // can transition a warmup array's elements kind (and, via the
    // AllocationSiteCache, the literal's AllocationSite) out from under `f`.
    const w1 = f(value);
    const w2 = f(value);
    %OptimizeFunctionOnNextCall(f);
    const a = f(value);
    const b = f(value);
    assertOptimized(f);
    assertNotSame(a, b);
    if (value === 1) {
      assertTrue(%HasSmiElements(a));
      assertTrue(%HasSmiElements(b));
    } else if (value === 1.5) {
      assertTrue(%HasDoubleElements(a));
      assertTrue(%HasDoubleElements(b));
    } else {
      assertTrue(%HasObjectElements(a));
      assertTrue(%HasObjectElements(b));
    }
    assertFilled(w1, length, value);
    assertFilled(w2, length, value);
    assertFilled(a, length, value);
    assertFilled(b, length, value);
    a[0] = 'other';
    assertSame(value, b[0]);
  }
}

// Stores that change the elements kind after the function is optimized.
(function() {
  function f(x) {
    const a = [,,];
    a[0] = x;
    return a;
  }
  %PrepareFunctionForOptimization(f);
  f(1);
  f(2);
  %OptimizeFunctionOnNextCall(f);
  for (const value of [3, 1.5, {}, 'a', 4]) {
    const a = f(value);
    assertSame(value, a[0]);
    assertFalse(1 in a);
    assertEquals(2, a.length);
  }
  %PrepareFunctionForOptimization(f);
  f(1);
  %OptimizeFunctionOnNextCall(f);
  for (const value of [1, 1.5, {}]) {
    const a = f(value);
    assertSame(value, a[0]);
    assertFalse(1 in a);
  }
})();

// Reads of the holes in optimized code.
(function() {
  function f() {
    const a = [,,,];
    return a[1];
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(undefined, f());
  %OptimizeFunctionOnNextCall(f);
  assertEquals(undefined, f());
  assertOptimized(f);
})();

// Accessors on Array.prototype see the holes. Keep this test last, because it
// changes Array.prototype.
(function() {
  let setterCalls = 0;
  Object.defineProperty(Array.prototype, 1, {
    get() { return 'from prototype'; },
    set(value) { setterCalls++; },
    configurable: true,
  });
  function f(x) {
    const a = [,,];
    a[0] = x;
    a[1] = x;
    return a;
  }
  %PrepareFunctionForOptimization(f);
  f(1);
  f(2);
  %OptimizeFunctionOnNextCall(f);
  const a = f(3);
  assertEquals(3, a[0]);
  assertEquals('from prototype', a[1]);
  assertFalse(a.hasOwnProperty(1));
  assertEquals(3, setterCalls);
  delete Array.prototype[1];
})();
