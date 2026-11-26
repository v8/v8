// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --maglev-range-verification

// Range analysis propagates CheckInt32Condition refinements through block
// exit ranges into successor blocks and loop backedges, allowing
// CheckedSmiSizedInt32 on pre-incremented array-index loop phis to fold while
// keeping intra-block pre-check ranges intact.

(function testPreIncrementLoopPhi() {
  function linSolveStep(x, x0, width, rowSize) {
    let lastRow = 0;
    let currentRow = rowSize;
    let nextRow = 2 * rowSize;
    let lastX = x[currentRow];
    ++currentRow;
    for (let i = 1; i <= width; i++) {
      lastX = x[currentRow] =
          (x0[currentRow] +
           (lastX + x[++currentRow] + x[++lastRow] + x[++nextRow])) *
          0.25;
    }
    return lastX;
  }

  const rowSize = 16;
  const width = 14;
  const size = rowSize * 4;
  const x = new Array(size);
  const x0 = new Array(size);
  for (let i = 0; i < size; i++) {
    x[i] = i * 0.5;
    x0[i] = i * 0.25;
  }

  %PrepareFunctionForOptimization(linSolveStep);
  const expected = linSolveStep(x.slice(), x0, width, rowSize);
  assertEquals(expected, linSolveStep(x.slice(), x0, width, rowSize));
  %OptimizeFunctionOnNextCall(linSolveStep);
  assertEquals(expected, linSolveStep(x.slice(), x0, width, rowSize));
  assertOptimized(linSolveStep);
})();

(function testBoundsCheckNotSelfEliminated() {
  // Ensure the exit-range narrowing from CheckInt32Condition never pollutes
  // the block-wide range used to optimize the CheckInt32Condition itself.
  function sumPreInc(a, n) {
    let idx = 0;
    let s = 0;
    for (let i = 0; i < n; i++) {
      s += a[++idx];
    }
    return s;
  }

  const a = [1.5, 2.5, 3.5, 4.5, 5.5];
  %PrepareFunctionForOptimization(sumPreInc);
  assertEquals(16, sumPreInc(a, 4));
  assertEquals(16, sumPreInc(a, 4));
  %OptimizeFunctionOnNextCall(sumPreInc);
  assertEquals(16, sumPreInc(a, 4));
  assertOptimized(sumPreInc);

  // Reading past the end of `a` must still trigger the bounds check and deopt
  // instead of reading out of bounds.
  assertTrue(Number.isNaN(sumPreInc(a, 6)));
})();

(function testConstantIndexBoundsCheckNotSelfEliminated() {
  // A passing `1 < length` check implies `length >= 2`. That must not be used
  // to fold the check itself.
  function sumPreInc(a, n) {
    let idx = 0;
    let s = 0;
    for (let i = 0; i < n; i++) {
      s += a[++idx];
    }
    return s;
  }

  const a = [1.5, 2.5, 3.5, 4.5, 5.5];
  %PrepareFunctionForOptimization(sumPreInc);
  assertEquals(16, sumPreInc(a, 4));
  assertEquals(16, sumPreInc(a, 4));
  %OptimizeFunctionOnNextCall(sumPreInc);
  assertEquals(16, sumPreInc(a, 4));
  assertOptimized(sumPreInc);

  assertTrue(Number.isNaN(sumPreInc([1.5], 1)));
})();

(function testNegativeStartIndexDeopts() {
  function sumFrom(a, start, n) {
    let idx = start | 0;
    let s = 0;
    for (let i = 0; i < n; i++) {
      s += a[++idx];
    }
    return s;
  }

  const a = [1.5, 2.5, 3.5, 4.5, 5.5];
  %PrepareFunctionForOptimization(sumFrom);
  assertEquals(10.5, sumFrom(a, 0, 3));
  assertEquals(10.5, sumFrom(a, 0, 3));
  %OptimizeFunctionOnNextCall(sumFrom);
  assertEquals(10.5, sumFrom(a, 0, 3));
  assertOptimized(sumFrom);

  // Negative index after ++idx (-5 + 1 = -4) must fail unsigned bounds check.
  assertTrue(Number.isNaN(sumFrom(a, -5, 2)));
})();

(function testPreCheckSmiStoreNotNarrowed() {
  // A Smi-size check preceding a bounds check in the same block must not be
  // folded by exit-range narrowing.
  function storeBeforeLoad(smiArr, arr, start, n) {
    let idx = start | 0;
    let sum = 0;
    for (let i = 0; i < n; i++) {
      smiArr[0] = idx;
      sum += arr[idx];
      idx = (idx + 1) | 0;
    }
    return sum;
  }

  const smiArr = [0];
  const arr = [1.5, 2.5, 3.5, 4.5];
  %PrepareFunctionForOptimization(storeBeforeLoad);
  assertEquals(7.5, storeBeforeLoad(smiArr, arr, 0, 3));
  assertEquals(7.5, storeBeforeLoad(smiArr, arr, 0, 3));
  %OptimizeFunctionOnNextCall(storeBeforeLoad);
  assertEquals(7.5, storeBeforeLoad(smiArr, arr, 0, 3));
  assertOptimized(storeBeforeLoad);

  const big = 1 << 30;
  assertTrue(Number.isNaN(storeBeforeLoad(smiArr, arr, big, 1)));
  assertEquals(big, smiArr[0]);
})();

(function testGreaterThanEqual() {
  // `x * 0` checks `x >= 0`, since the result is -0 otherwise.
  function mulZero(x, add) {
    x = x | 0;
    const z = x * 0;
    if (add) return x + 1;
    return z;
  }

  %PrepareFunctionForOptimization(mulZero);
  assertEquals(6, mulZero(5, true));
  assertEquals(0, mulZero(5, false));
  %OptimizeFunctionOnNextCall(mulZero);
  assertEquals(6, mulZero(5, true));
  assertOptimized(mulZero);

  assertTrue(Object.is(-0, mulZero(-1, false)));
  assertEquals(0, mulZero(-1, true));
})();

(function testEqual() {
  // `x / 3` checks that the division is exact.
  function div3(x, add) {
    x = x | 0;
    const q = x / 3;
    if (add) return x + 1;
    return q;
  }

  %PrepareFunctionForOptimization(div3);
  assertEquals(7, div3(6, true));
  assertEquals(2, div3(6, false));
  %OptimizeFunctionOnNextCall(div3);
  assertEquals(7, div3(6, true));
  assertOptimized(div3);

  assertEquals(4 / 3, div3(4, false));
  assertEquals(5, div3(4, true));
})();

(function testUnsignedLessThanEqual() {
  // forEach checks `original length <= current length` after each callback.
  function sumEach(a, shrink) {
    let s = 0;
    a.forEach((v) => {
      if (shrink) a.length = 1;
      s += v;
    });
    return s;
  }

  %PrepareFunctionForOptimization(sumEach);
  assertEquals(6, sumEach([1, 2, 3], false));
  assertEquals(6, sumEach([1, 2, 3], false));
  %OptimizeFunctionOnNextCall(sumEach);
  assertEquals(6, sumEach([1, 2, 3], false));
  assertOptimized(sumEach);

  assertEquals(1, sumEach([1, 2, 3], true));
})();
