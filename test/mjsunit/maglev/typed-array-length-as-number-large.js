// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --typed-array-length-loading

function f(size) {
  let a = new Uint8Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(f);

if (%Is64Bit()) {
  f(100);
  const largeLength = 8589934592;
  f(largeLength);

  %OptimizeMaglevOnNextCall(f);
  const val1 = f(100);
  assertEquals(100, val1);
  assertTrue(isMaglevved(f));

  const val2 = f(largeLength);
  assertEquals(largeLength, val2);
}
