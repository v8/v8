// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --use-osr --turbo-osr

"use strict";

function foo() {
  var result;
  {
    let sum = 0;
    for (var i = 0; i < 100; i++) {
      if (i == 50) %OptimizeOsr();
      sum += i;
    }
    result = sum;
  }
  return result;
}

assertEquals(4950, foo());
assertEquals(4950, foo());
assertEquals(4950, foo());

function bar() {
  var result;
  {
    let sum = 0;
    for (let i = 0; i < 90; i++) {
      sum += i;
      if (i == 45) %OptimizeOsr();
    }
    result = sum;
  }
  return result;
}

assertEquals(4005, bar());
assertEquals(4005, bar());
assertEquals(4005, bar());
