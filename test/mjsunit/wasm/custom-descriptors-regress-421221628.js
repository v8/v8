// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --noconcurrent-osr

function f6(a10) {
    const v11 = %WasmStruct();
    v11[a10];
    return v11;
}
function outer() {
  for (let i = 0; i < 10; i++) {
    f6(4);
  }
  for (let i = 0; i < 10; i++) {
    f6("string");
    if (i == 2) %OptimizeOsr(0);
  }
}
%PrepareFunctionForOptimization(outer);
outer();
