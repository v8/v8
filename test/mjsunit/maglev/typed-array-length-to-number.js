// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --typed-array-length-loading

function f(size) {
  let a = new Uint8Array(size);
  return (+a.length > 0);
}
%PrepareFunctionForOptimization(f);

f(100);
%OptimizeMaglevOnNextCall(f);
const v1 = f(100);
assertTrue(v1);
assertTrue(isMaglevved(f));

// But large JSTypedArray lengths cause a deopt, because the length doesn't
// match the feedback (SignedSmall).
if (%Is64Bit()) {
  const largeLength = 8589934592;
  const v2 = f(largeLength);
  assertTrue(v2);
  assertFalse(isMaglevved(f));
}
