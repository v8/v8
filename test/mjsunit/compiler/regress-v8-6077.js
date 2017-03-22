// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var f32 = new Float32Array(20);

function foo(f32, deopt) {
  var f0 = f32[0];
  var f1 = f32[1];
  var f2 = f32[2];
  var f3 = f32[3];
  var f4 = f32[4];
  var f5 = f32[5];
  var f6 = f32[6];
  var f7 = f32[7];
  var f8 = f32[8];
  var f9 = f32[9];
  var f10 = f32[10];
  var f11 = f32[11];
  var f12 = f32[12];
  var f13 = f32[13];
  var f14 = f32[14];
  var f15 = f32[15];
  var f16 = f32[16];
  var f17 = f32[17];
  var f18 = f32[18];
  var f19 = f32[19];
  // Side effect to force the deopt after the store.
  f32[0] = f1 - 1;
  // Here we deopt once we warm up with numbers, but then we
  // pass a string as {deopt}.
  return deopt + f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7 + f8 + f9 + f10 + f11 +
      f12 + f13 + f14 + f15 + f16 + f17 + f18 + f19;
}

var s = "";
for (var i = 0; i < f32.length; i++) {
  f32[i] = i;
  s += i;
}

foo(f32, 0);
foo(f32, 0);
%OptimizeFunctionOnNextCall(foo);
assertEquals("x" + s, foo(f32, "x"));
