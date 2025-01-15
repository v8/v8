// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-turbofan
// Flags: --typed-array-length-loading

function f(size) {
  let a = new Uint8Array(size);
  for (let i = 0; i < a.length; ++i) {
    a[i] = 1;
    if (i == 10) {
      // This is needed so that we don't OSR even if a.length is large and also
      // so that the test runs quicker.
      break;
    }
  }
  return a;
}
%PrepareFunctionForOptimization(f);

if (%Is64Bit()) {
  f(100);
  const largeLength = 8589934592;
  f(largeLength);

  %OptimizeMaglevOnNextCall(f);
  const a1 = f(100);
  assertEquals(100, a1.length);
  assertTrue(isMaglevved(f));

  const a2 = f(largeLength);
  assertEquals(largeLength, a2.length);
  assertTrue(isMaglevved(f));
}
