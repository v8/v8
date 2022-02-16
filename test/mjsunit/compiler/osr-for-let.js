// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --use-osr

"use strict";

function test(expected, func) {
  %PrepareFunctionForOptimization(func);
  assertEquals(expected, func());
  %PrepareFunctionForOptimization(func);
  assertEquals(expected, func());
  %PrepareFunctionForOptimization(func);
  assertEquals(expected, func());
}

function bar() {
  var result;
  {
    let sum = 0;
    for (let i = 0; i < 90; i++) {
      sum += i;
      if (i == 45 || i == 46) %OptimizeOsr(0, "concurrent");
    }
    result = sum;
  }
  return result;
}
%PrepareFunctionForOptimization(bar);

test(4005, bar);

function baz() {
  let sum = 0;
  for (let i = 0; i < 3; i++) {
    %PrepareFunctionForOptimization(baz);
    sum = 2;
    %OptimizeOsr(0, "concurrent");
  }
  return sum;
}

test(2, baz);

function qux() {
  var result = 0;
  for (let i = 0; i < 3; i++) {
    %PrepareFunctionForOptimization(qux);
    result = i;
    %OptimizeOsr(0, "concurrent");
  }
  return result;
}

test(2, qux);

function nux() {
  var result = 0;
  for (let i = 0; i < 3; i++) {
    {
      %PrepareFunctionForOptimization(nux);
      let sum = i;
      %OptimizeOsr(0, "concurrent");
      result = sum;
    }
  }
  return result;
}

test(2, nux);

function blo() {
  var result;
  {
    let sum = 0;
    for (let i = 0; i < 90; i++) {
      sum += i;
      if (i == 45 || i == 46) %OptimizeOsr(0, "concurrent");
    }
    result = ret;
    function ret() {
      return sum;
    }
  }
  return result;
}
%PrepareFunctionForOptimization(blo);

test(4005, blo());
