// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev --stress-lazy --stack-size=100

function runNearStackLimit(f) {
  let recursing = true;
  let f_succeeded = false;
  function fine() {
    try {
      fine();
      if (f_succeeded) return;
      f();
      f_succeeded = true;
    } catch (e) {
      if (recursing) {
        recursing = false;
        try {
          f();
          f_succeeded = true;
        } catch (e2) {}
      }
    }
  }

  function coarse(
    a0, a1, a2, a3, a4, a5, a6, a7, a8, a9,
    b0, b1, b2, b3, b4, b5, b6, b7, b8, b9,
    c0, c1, c2, c3, c4, c5, c6, c7, c8, c9,
    d0, d1, d2, d3, d4, d5, d6, d7, d8, d9,
    e0, e1, e2, e3, e4, e5, e6, e7, e8, e9,
    f0, f1, f2, f3, f4, f5, f6, f7, f8, f9
  ) {
    try {
      coarse(
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9
      );
    } catch (e) {
      fine();
    }
  }

  try { coarse(); } catch (e) {}
}

function* inner() {
  yield "inner 1";
  yield "inner 2";
}

function* outer(
  p00, p01, p02, p03, p04, p05, p06, p07, p08, p09,
  p10, p11, p12, p13, p14, p15, p16, p17, p18, p19,
  p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
  p30, p31, p32, p33, p34, p35, p36, p37, p38, p39,
  p40, p41, p42, p43, p44, p45, p46, p47, p48, p49,
  p50, p51, p52, p53, p54, p55, p56, p57, p58, p59,
  p60, p61, p62, p63, p64, p65, p66, p67, p68, p69,
  p70, p71, p72, p73, p74, p75, p76, p77, p78, p79,
  p80, p81, p82, p83, p84, p85, p86, p87, p88, p89,
  p90, p91, p92, p93, p94, p95, p96, p97, p98, p99
) {
  yield "outer 1";
  let v = yield* inner();
  yield v;
}

function test(g) {
  try {
    return g.next();
  } catch (e) {}
}

%PrepareFunctionForOptimization(test);
let g = outer();
assertEquals("outer 1", test(g).value);

%OptimizeMaglevOnNextCall(test);
assertEquals("inner 1", test(g).value);
assertOptimized(test);

function run() {
  test(g);
}
%PrepareFunctionForOptimization(run);

// Throw an exception near the stack limit inside the inlined resume site:
runNearStackLimit(run);

// The generator must be closed, and must not leak TheHole on subsequent resumes:
let res1 = test(g);
assertEquals(undefined, res1.value);
assertEquals(true, res1.done);

let res2 = test(g);
assertEquals(undefined, res2.value);
assertEquals(true, res2.done);
