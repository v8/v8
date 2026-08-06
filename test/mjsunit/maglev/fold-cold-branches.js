// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev --maglev-fold-cold-branches

// Branches to blocks that only contain an unconditional deopt (cold branches
// with insufficient feedback) are folded into conditional deopt checks. Each
// case below warms up only one side of a branch, optimizes, then takes the
// cold side and checks that the deopt produces the correct result.

// BranchIfToBooleanTrue, cold true side.
(function() {
  function f(a) {
    if (a) {
      return a + 1;
    }
    return -1;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(-1, f(0));
  assertEquals(-1, f(0));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(-1, f(0));
  assertEquals(6, f(5));
})();

// BranchIfToBooleanTrue, cold false side.
(function() {
  function f(a) {
    if (a) {
      return 1;
    }
    return a - 1;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(1, f(3));
  assertEquals(1, f(4));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(1, f(5));
  assertEquals(-1, f(0));
})();

// BranchIfInt32Compare, cold true side.
(function() {
  function f(a, b) {
    if (a < b) {
      return a + b;
    }
    return b - a;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(-2, f(3, 1));
  assertEquals(-2, f(4, 2));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(-3, f(5, 2));
  assertEquals(4, f(1, 3));
})();

// BranchIfInt32Compare, cold false side.
(function() {
  function f(a, b) {
    if (a < b) {
      return b - a;
    }
    return a + b;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(2, f(1, 3));
  assertEquals(2, f(2, 4));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(3, f(2, 5));
  assertEquals(9, f(5, 4));
})();

// BranchIfSmi via a polymorphic-ish value check pattern.
(function() {
  function f(a) {
    if (typeof a === "number") {
      return 2;
    }
    return a.x + 1;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(2, f(1));
  assertEquals(2, f(2));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(2, f(3));
  assertEquals(8, f({x: 7}));
})();

// Cold branch inside a loop.
(function() {
  function f(n, flag) {
    let sum = 0;
    for (let i = 0; i < n; i++) {
      if (flag) {
        sum += i * 2;
      } else {
        sum += i;
      }
    }
    return sum;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(10, f(5, false));
  assertEquals(10, f(5, false));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(10, f(5, false));
  assertEquals(20, f(5, true));
})();

// BranchIfRootConstant on a known-boolean value, cold true side.
(function() {
  function f(a) {
    let c = a > 10;
    let s = a + 1;
    if (c) {
      return s * 3;
    }
    return s - 1;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(1, f(1));
  assertEquals(2, f(2));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(3, f(3));
  assertEquals(63, f(20));
})();

// BranchIfRootConstant on a known-boolean value, cold false side.
(function() {
  function f(a) {
    let c = a > 10;
    let s = a + 1;
    if (c) {
      return s - 1;
    }
    return s * 3;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(19, f(19));
  assertEquals(20, f(20));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(21, f(21));
  assertEquals(9, f(2));
})();

// BranchIfReferenceEqual, cold true side.
(function() {
  function f(a, b) {
    if (a === b) {
      return a.x + 99;
    }
    return b.y + 1;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(2, f({x: 1, y: 5}, {x: 2, y: 1}));
  assertEquals(3, f({x: 1, y: 5}, {x: 2, y: 2}));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(4, f({x: 1, y: 5}, {x: 2, y: 3}));
  const o = {x: 7, y: 8};
  assertEquals(106, f(o, o));
})();

// BranchIfFloat64Compare, cold true side; NaN must not deopt on the hot
// (false) side.
(function() {
  function f(a, b) {
    if (a < b) {
      return a * b;
    }
    return b - a;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(-2, f(3.5, 1.5));
  assertEquals(-2.5, f(4.5, 2));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(-3.25, f(5.5, 2.25));
  assertEquals(NaN, f(NaN, 2.5));
  assertEquals(5.25, f(1.5, 3.5) * 1);
})();

// BranchIfFloat64Compare, cold false side; NaN must deopt (it takes the
// false side) and produce the correct result.
(function() {
  function f(a, b) {
    if (a < b) {
      return b - a;
    }
    return a * b;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(2, f(1.5, 3.5));
  assertEquals(2.5, f(2, 4.5));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(3, f(2.5, 5.5));
  assertEquals(NaN, f(NaN, 2.5));
  assertEquals(5.25, f(3.5, 1.5));
})();

// BranchIfUndetectable (a == null with receiver feedback), cold true side.
(function() {
  function f(a) {
    if (a == null) {
      return 1 + "x";
    }
    return a.z + 1;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(2, f({z: 1}));
  assertEquals(3, f({z: 2}));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(4, f({z: 3}));
  assertEquals("1x", f(null));
  assertEquals("1x", f(undefined));
})();

// BranchIfUndetectable, cold false side.
(function() {
  function f(a) {
    if (a == null) {
      return -1;
    }
    return a.z * 2;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(-1, f(null));
  assertEquals(-1, f(undefined));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(-1, f(null));
  assertEquals(14, f({z: 7}));
})();

// BranchIfRootConstant with the cold true side: a phi-merged boolean defeats
// test/branch fusion, so the branch compares against the true root directly.
(function() {
  function f(a, b) {
    const c = b ? a > 10 : a > 5;
    if (c) {
      return a * 3;
    }
    return a - 1;
  }
  %PrepareFunctionForOptimization(f);
  assertEquals(0, f(1, true));
  assertEquals(1, f(2, false));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(2, f(3, true));
  assertEquals(60, f(20, true));
})();
