// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev

function test(cond1, cond2, cond3) {
  let a = undefined;
  let b = undefined;
  for (let i = 0; i < 2; i++) {
    a = undefined;
    b = undefined;
  }
  let p1 = cond1 ? a : undefined;
  let p2 = cond2 ? b : undefined;
  let t = (p1 | 0) ^ (p2 | 0);
  let phi2 = cond3 ? p1 : p2;
  // Dead consumer: keeps phi2's use hints empty so p1/p2 stay truncatable, and
  // folds to Abort(Unreachable) in PostOptimizer if phi2's type becomes kNone.
  let unused = typeof phi2 === "undefined";
  return t;
}

%PrepareFunctionForOptimization(test);
assertEquals(0, test(true, true, true));
assertEquals(0, test(false, false, false));
%OptimizeFunctionOnNextCall(test);
assertEquals(0, test(true, true, true));
assertEquals(0, test(false, false, false));
