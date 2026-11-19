// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

// Int32 compares, branches and checks read a constant operand as an
// immediate, on whichever side it appears.

// The extremes of the Smi range, so that the feedback stays SignedSmall.
const kMinSmi = -1073741824;
const kMaxSmi = 1073741823;

function check(f, cases) {
  %PrepareFunctionForOptimization(f);
  for (const [arg, expected] of cases) assertEquals(expected, f(arg));
  %OptimizeMaglevOnNextCall(f);
  for (const [arg, expected] of cases) assertEquals(expected, f(arg));
  assertMaglevved(f);
}

// Branches with the constant on the right.
check(x => x < 10 ? 1 : 2,
      [[9, 1], [10, 2], [-3, 1], [kMinSmi, 1], [kMaxSmi, 2]]);
check(x => x > -1 ? 1 : 2,
      [[0, 1], [-1, 2], [-2, 2], [kMinSmi, 2], [kMaxSmi, 1]]);
check(x => x === 0 ? 1 : 2, [[0, 1], [1, 2], [-1, 2], [kMinSmi, 2]]);
check(x => x !== 0 ? 1 : 2, [[0, 2], [1, 1], [-1, 1], [kMaxSmi, 1]]);
// 0x3cb000 is an add/sub immediate shifted by 12; 0x12345 is not encodable.
check(x => x <= 3977216 ? 1 : 2,
      [[3977216, 1], [3977217, 2], [-1, 1], [kMaxSmi, 2]]);
check(x => x >= 74565 ? 1 : 2,
      [[74565, 1], [74564, 2], [kMinSmi, 2], [kMaxSmi, 1]]);

// Branches with the constant on the left.
check(x => 0 < x ? 1 : 2, [[1, 1], [0, 2], [-1, 2], [kMaxSmi, 1]]);
check(x => 10 >= x ? 1 : 2, [[10, 1], [11, 2], [kMinSmi, 1]]);
check(x => 0 === x ? 1 : 2, [[0, 1], [1, 2], [kMinSmi, 2]]);
check(x => -1 > x ? 1 : 2, [[-2, 1], [-1, 2], [0, 2], [kMinSmi, 1]]);
check(x => 3977216 < x ? 1 : 2, [[3977217, 1], [3977216, 2], [-1, 2]]);
check(x => 74565 !== x ? 1 : 2, [[74565, 2], [74564, 1], [0, 1]]);

// Compares that produce a value.
check(x => x > 7, [[8, true], [7, false], [kMinSmi, false]]);
check(x => 0 < x, [[1, true], [0, false], [-1, false]]);
check(x => 5 == x, [[5, true], [4, false], [-5, false]]);
check(x => 3977216 <= x, [[3977216, true], [3977215, false]]);

// Loop exits against a constant.
check(x => { let s = 0; for (let i = 0; i < 100; i++) s += x; return s; },
      [[1, 100], [-2, -200]]);
check(x => { let s = 0; for (let i = 50; 0 <= i; i--) s += x; return s; },
      [[1, 51], [3, 153]]);

// Checks: x * 0 deoptimizes on a negative x (the result would be -0).
function mul0(x) { return x * 0; }
%PrepareFunctionForOptimization(mul0);
assertEquals(0, mul0(5));
assertEquals(0, mul0(0));
%OptimizeMaglevOnNextCall(mul0);
assertEquals(0, mul0(5));
assertEquals(0, mul0(kMaxSmi));
assertMaglevved(mul0);
assertEquals(-0, mul0(-5));
assertUnoptimized(mul0);

// Checks: a constant index is bounds-checked against the length, with the
// constant on the left of the unsigned compare.
function at3(a) { return a[3]; }
%PrepareFunctionForOptimization(at3);
assertEquals(4, at3([1, 2, 3, 4, 5]));
assertEquals(4, at3([1, 2, 3, 4]));
%OptimizeMaglevOnNextCall(at3);
assertEquals(4, at3([1, 2, 3, 4, 5]));
assertEquals(4, at3([1, 2, 3, 4]));
assertMaglevved(at3);
assertEquals(undefined, at3([1, 2, 3]));
assertUnoptimized(at3);

function at0(a) { return a[0]; }
%PrepareFunctionForOptimization(at0);
assertEquals(1, at0([1, 2]));
assertEquals(7, at0([7]));
%OptimizeMaglevOnNextCall(at0);
assertEquals(1, at0([1, 2]));
assertEquals(7, at0([7]));
assertMaglevved(at0);
assertEquals(undefined, at0([]));
assertUnoptimized(at0);

// A load that has returned undefined for an out-of-bounds index compares
// the constant index unsigned against the length, with the uint32 constant
// on the left, instead of deoptimizing.
function at5(a) { return a[5]; }
%PrepareFunctionForOptimization(at5);
assertEquals(undefined, at5([1, 2, 3]));
assertEquals(6, at5([1, 2, 3, 4, 5, 6]));
%OptimizeMaglevOnNextCall(at5);
assertEquals(undefined, at5([1, 2, 3]));
assertEquals(6, at5([1, 2, 3, 4, 5, 6]));
assertMaglevved(at5);
