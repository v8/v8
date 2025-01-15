// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --typed-array-length-loading

function f(size) {
  let a = new Uint8Array(size);
  let b = new Uint8Array(size + 1);
  b[a.length] = 1;
  return b;
}
%PrepareFunctionForOptimization(f);

f(100);

%OptimizeMaglevOnNextCall(f);
const a1 = f(100);
assertEquals(1, a1[100]);
assertTrue(isMaglevved(f));

// TODO(389019544): This might or might not fail once the deopt loop is fixed.
if (%Is64Bit()) {
  const largeLength = 8589934592;
  const a2 = f(largeLength);
  assertEquals(1, a2[largeLength]);
  assertFalse(isMaglevved(f));
}
