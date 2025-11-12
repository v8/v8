// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function __wrapTC(f = true) {
    return f();
}
(function () {
    switch (typeof value) {
    }
  __prettyPrint = function (value = false) {
    let str = value;
    print(str);
  };
  __prettyPrintExtra = function (value) {
    __prettyPrint(value);
  };
})();
function __f_8(__v_0) {
  try {
    delete __v_0[__getRandomProperty()]();
  } catch (e) {
    __prettyPrintExtra();
  }
  try {
    __v_0();
    __v_0();
  } catch (e) {
  }
    __prettyPrintExtra(__prettyPrintExtra());
}
function __f_9() {
/* FunctionCallMutator: Replaced __f_0 with __f_8 */__f_8();
}
  __f_8(__f_9);
print("v8-foozzie source: v8/test/mjsunit/maglev/store-oddball-to-double-elements.js");
let __v_1 = __wrapTC(() => [0.5]);
function __f_11(__v_2) {
    __v_2 + 0.5;
      __v_1[0] = __v_2;
      __prettyPrintExtra(__v_1);
}
  %PrepareFunctionForOptimization(__f_11);
  __f_11();
  %OptimizeFunctionOnNextCall(__f_11);
__f_11();
