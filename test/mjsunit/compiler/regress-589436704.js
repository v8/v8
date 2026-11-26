// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

"use strict";

function noop() {}
%NeverOptimizeFunction(noop);

var sinkFn = noop;
var nullProtoObj = Object.setPrototypeOf({z: 1}, null);
function Ctor() {}
Object.setPrototypeOf(Ctor, null);

function add4(x) {
  return x + 4;
}

function makeRestArray(cond, val, ...rest) {
  if (cond) {
    val + 2; val + 2; val + 2; val + 2;
  }
  return rest;
}

function toRestArray(...args) {
  return args;
}

function getNullProtoObj(val) {
  val + 1;
  return nullProtoObj;
}

function test(cond, x) {
  let caught;
  let l0 = 0, l1 = 1, l2 = 2, l3 = 3, l4 = 4,
      l5 = 5, l6 = 6, l7 = 7, l8 = 8, l9 = 9;
  let acc = 0;
  let sink = sinkFn, obj = nullProtoObj, ctor = Ctor;
  let args = toRestArray(x);
  let sinkRes = 0, rest, guard = 7, fetched = 7;

  try {
    return Math.floor(...args);
  } catch {
    caught = Date.now(...args);
  }

  let i = 5;
  do {
    sinkRes = sink(l0, l1, l2, l3, l4, l5, l6, l7, l8, l9);
    fetched = getNullProtoObj(acc);
    if (fetched instanceof ctor) throw args;
    let branchVal = cond ? 1 : (x ? 2 : 3);
    rest = makeRestArray(cond, caught, 1);
    while (obj instanceof ctor) {
      sinkFn();
    }
    if (cond) {
      acc + 1;
      guard = fetched;
    } else {
      guard = obj;
    }
    acc = add4(...rest);
    let isNull;
    if (acc === 5) {
      if (obj instanceof ctor) isNull = rest;
      isNull = sinkRes === null;
    }
  } while (--i > 0);
  return acc;
}

%PrepareFunctionForOptimization(add4);
%PrepareFunctionForOptimization(makeRestArray);
%PrepareFunctionForOptimization(toRestArray);
%PrepareFunctionForOptimization(getNullProtoObj);
%PrepareFunctionForOptimization(test);

const sym = Symbol('x');
assertEquals(1, test(false, 1));
assertEquals(1, test(true, 1));
assertEquals(5, test(false, sym));
assertEquals(5, test(true, sym));

%OptimizeFunctionOnNextCall(test);
assertEquals(1, test(false, 1));
assertEquals(1, test(true, 1));
assertEquals(5, test(false, sym));
assertEquals(5, test(true, sym));
