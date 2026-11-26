// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --maglev-range-verification

// Range analysis narrows the input of a deopting check for the code that
// follows the check. Each test uses the checked value in a successor block
// and then passes a value that must fail the check.

const kSmiMax = (1 << 30) - 1;

(function testTypedArrayBounds() {
  function sumFrom(ta, start, n) {
    let idx = start | 0;
    let s = 0;
    for (let i = 0; i < n; i++) {
      s += ta[++idx];
    }
    return s;
  }

  const ta = new Uint8Array([1, 2, 3, 4, 5]);
  %PrepareFunctionForOptimization(sumFrom);
  assertEquals(9, sumFrom(ta, 0, 3));
  assertEquals(9, sumFrom(ta, 0, 3));
  %OptimizeFunctionOnNextCall(sumFrom);
  assertEquals(9, sumFrom(ta, 0, 3));
  assertOptimized(sumFrom);

  assertTrue(Number.isNaN(sumFrom(ta, -5, 2)));
})();

(function testInt32IsSmi() {
  function storeAndAdd(smiArr, start, n, add) {
    let idx = start | 0;
    let s = 0;
    for (let i = 0; i < n; i++) {
      smiArr[0] = idx;
      if (add) s += idx + 1;
      idx = (idx + 3) | 0;
    }
    return s;
  }

  const smiArr = [0];
  %PrepareFunctionForOptimization(storeAndAdd);
  assertEquals(15, storeAndAdd(smiArr, 1, 3, true));
  assertEquals(0, storeAndAdd(smiArr, 1, 3, false));
  %OptimizeFunctionOnNextCall(storeAndAdd);
  assertEquals(15, storeAndAdd(smiArr, 1, 3, true));
  assertOptimized(storeAndAdd);

  assertEquals(kSmiMax + 1, storeAndAdd(smiArr, kSmiMax, 1, true));
  assertEquals(kSmiMax, smiArr[0]);
  assertEquals(kSmiMax + 2, storeAndAdd(smiArr, kSmiMax + 1, 1, true));
  assertEquals(kSmiMax + 1, smiArr[0]);
})();

(function testUint32IsSmi() {
  function startsWithAt(s, v, add) {
    const u = v >>> 0;
    const r = s.startsWith('b', u);
    if (add) return u + 1;
    return r;
  }

  %PrepareFunctionForOptimization(startsWithAt);
  assertEquals(true, startsWithAt('abc', 1, false));
  assertEquals(2, startsWithAt('abc', 1, true));
  %OptimizeFunctionOnNextCall(startsWithAt);
  assertEquals(2, startsWithAt('abc', 1, true));
  assertOptimized(startsWithAt);

  assertEquals(kSmiMax + 1, startsWithAt('abc', kSmiMax, true));
  assertEquals(2 ** 32, startsWithAt('abc', -1, true));
})();

(function testIntPtrIsSmi() {
  function startsWithAtLength(s, ta, add) {
    const l = ta.length;
    const r = s.startsWith('b', l);
    if (add) return l + 1;
    return r;
  }

  const ta = new Uint8Array(1);
  %PrepareFunctionForOptimization(startsWithAtLength);
  assertEquals(true, startsWithAtLength('abc', ta, false));
  assertEquals(2, startsWithAtLength('abc', ta, true));
  %OptimizeFunctionOnNextCall(startsWithAtLength);
  assertEquals(2, startsWithAtLength('abc', ta, true));
  assertOptimized(startsWithAtLength);
})();

var constGlobal = 5;
(function testValueEqualsInt32() {
  function storeAndAdd(v, add) {
    v = v | 0;
    constGlobal = v;
    if (add) return v + 1;
    return 0;
  }

  %PrepareFunctionForOptimization(storeAndAdd);
  assertEquals(6, storeAndAdd(5, true));
  assertEquals(0, storeAndAdd(5, false));
  %OptimizeFunctionOnNextCall(storeAndAdd);
  assertEquals(6, storeAndAdd(5, true));
  assertOptimized(storeAndAdd);

  assertEquals(8, storeAndAdd(7, true));
  assertEquals(7, constGlobal);
})();
