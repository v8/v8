// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboprop --no-use-osr --opt --no-always-opt


var v_0 = {};
function f_0(o, v) {
  o.f = v;
}

function f_1() {
  return v_0.f;
}

%PrepareFunctionForOptimization(f_0);
f_0(v_0, 42);
f_0(v_0, 42);
%OptimizeFunctionOnNextCall(f_0);
f_0(v_0, 42);

// TP tier up
for (let i = 0; i < 100000; i++) {
  f_1();
}

assertOptimized(f_0);
// TODO(mythria): Add an option to assert on the optimization tier and assert
// f_1 is optimized with TurboFan.
assertOptimized(f_1);
// Store in f_0 should trigger a change to the constness of the field.
f_0(v_0, 53);
// f_0 does a eager deopt and lets the interpreter update the field constness.
assertUnoptimized(f_0);
assertEquals(v_0.f, 53);
assertEquals(f_1(), 53);
