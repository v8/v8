// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --stack-size=100 --no-use-ic

function runNearStackLimit(f) {
  function t() {
    try {
      return t();
    } catch (e) {
      return f();
    }
  }
  return t();
}
function __f_3() {
  for (var __v_2 = 0; __v_2 < 100; __v_2++) {
    if (__v_2 == 50) {
      %OptimizeOsr();
      Object.defineProperty();
    }
  }
}
%PrepareFunctionForOptimization(__f_3);
try {
  runNearStackLimit(() => { return __f_3(); })();
} catch {}
