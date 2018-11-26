// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function fun(arg) {
  let x = arguments.length;
  a1 = new Array(0x10);
  a1[0] = 1.1;
  a2 = new Array(0x10);
  a2[0] = 1.1;
  a1[(x >> 16) * 21] = 1.39064994160909e-309;  // 0xffff00000000
  a1[(x >> 16) * 41] = 8.91238232205e-313;  // 0x2a00000000
}

var a1, a2;
var a3 = [1.1,2.2];
a3.length = 0x11000;
a3.fill(3.3);

var a4 = [1.1];

for (let i = 0; i < 3; i++) fun(...a4);
%OptimizeFunctionOnNextCall(fun);
fun(...a4);

assertThrows(() => fun(...a3), RangeError);
assertThrows(() => fun.apply(null, a3), RangeError);

const kMaxArguments = 65534;
let big_array = [];
for (let i = 0; i < kMaxArguments + 1; i++) big_array.push(i);
assertThrows(() => fun(...big_array), RangeError);
assertThrows(() => new fun(...big_array), RangeError);
assertThrows(() => fun.apply(null, big_array), RangeError);
assertThrows(() => Reflect.construct(fun, big_array), RangeError);
assertThrows(() => Reflect.apply(fun, undefined, big_array), RangeError);

big_array = [];
for (let i = 0; i < kMaxArguments + 1; i++) big_array.push(i + 0.1);
assertThrows(() => fun(...big_array), RangeError);
assertThrows(() => new fun(...big_array), RangeError);
assertThrows(() => fun.apply(null, big_array), RangeError);
assertThrows(() => Reflect.construct(fun, big_array), RangeError);
assertThrows(() => Reflect.apply(fun, undefined, big_array), RangeError);

big_array = [];
for (let i = 0; i < kMaxArguments + 1; i++) big_array.push({i: i});
assertThrows(() => fun(...big_array), RangeError);
assertThrows(() => new fun(...big_array), RangeError);
assertThrows(() => fun.apply(null, big_array), RangeError);
assertThrows(() => Reflect.construct(fun, big_array), RangeError);
assertThrows(() => Reflect.apply(fun, undefined, big_array), RangeError);
