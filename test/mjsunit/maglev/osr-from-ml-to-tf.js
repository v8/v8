// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-stress-opt
// Flags: --no-baseline-batch-compilation --use-osr --turbofan

let keep_going = 10000000;  // A counter to avoid test hangs on failure.
let i;  // The loop counter for the test function.

function f() {
  let sum = i;
  while (--i > 0 && !%CurrentFrameIsTurbofan() && --keep_going) {
    // This loop should trigger OSR.
    sum += i;
  }
  return sum;
}

function g() {
  assertTrue(%IsMaglevEnabled());
  while (!%ActiveTierIsMaglev(f) && --keep_going) {
    i = 5.2;
    f();
  }

  console.log('osr to tf', keep_going);
  assertTrue(%IsTurbofanEnabled());
  i = 66666666666;
  f();
  assertTrue(keep_going > 0);
}
%NeverOptimizeFunction(g);

g();
