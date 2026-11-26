// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function f(c0, c1) {
  let o1 = { a: 0 };
  o1.a = {};
  let o2 = { b: 1 };
  let res = false;
  for (let k = 0, v = o2; k < 1; k = {}, v = o1) {
    o1.a = k;
    let vb1 = v.b;
    let vb2 = vb1 | 0;
    let phi0 = c0 ? vb1 : vb2;
    let p = c1 ? phi0 : true;
    let q = p ? 1 : 2;
    o1.a = vb1;
    res = q;
  }
  return res;
}

%PrepareFunctionForOptimization(f);
assertEquals(1, f(true, true));
%OptimizeFunctionOnNextCall(f);
assertEquals(1, f(true, true));
assertEquals(1, f(false, true));
assertEquals(1, f(true, false));
assertEquals(1, f(false, false));
