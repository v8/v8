// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --typed-array-length-loading

function f(size, b) {
  let a = new Uint8Array(size);
  let c = 0;
  if (b) {
    c = a.length;
  }
  return c;
}
%PrepareFunctionForOptimization(f);

f(100, true);
f(100, false);

%OptimizeMaglevOnNextCall(f);
const v1 = f(100, true);
assertEquals(100, v1);
assertTrue(isMaglevved(f));

// Also large JSTypedArray lengths are supported.
if (%Is64Bit()) {
  const largeLength = 8589934592;
  const v2 = f(largeLength, true);
  assertEquals(largeLength, v2);
  assertTrue(isMaglevved(f));
}
