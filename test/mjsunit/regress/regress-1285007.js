// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --assert-types --interrupt-budget=1000
// Flags: --no-lazy-feedback-allocation --turboprop

var __v_12 = [];
var __v_30 = {};
function __f_0(x, len) {
  var __v_1 = new Array();
  var __v_2 = x + 0.5;
  var __v_3 = x + 1.5;
  var __v_4 = x + 2.5;
  var __v_5 = x + 3.5;
  var __v_7 = x + 5.5;
  var __v_15 = x + 13.5;
  var __v_16 = x + 14.5;
  var __v_22 = x + 20.5;
  var __v_25 = x + 23.5;
  var __v_8 = x + 24.5;
  __v_12[len] = 0;
  __v_1[0] = __v_2;
  __v_1[1] = __v_3;
  __v_1[2] = __v_4;
  __v_1[3] = __v_5;
  __v_1 = __v_30;
  __v_1[5] = __v_7;
  __v_1[6] = __v_8;
  __v_1[13] = __v_15;
  __v_1[14] = __v_16;
  __v_1[23] = __v_25;
  for (var __v_27 = 0; __v_27 < __v_1.length; __v_27++) {
    x + __v_22 + [__v_37];
  }
}
%PrepareFunctionForOptimization(__f_0);
__f_0();
%OptimizeFunctionOnNextCall(__f_0);
__f_0();
