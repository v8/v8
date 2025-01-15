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

if (%Is64Bit()) {
  f(100);
  const largeLength = 8589934592;
  f(largeLength);

  %OptimizeMaglevOnNextCall(f);
  const a1 = f(100);
  assertEquals(1, a1[100]);
  assertTrue(isMaglevved(f));

  const a2 = f(largeLength);
  assertEquals(1, a2[largeLength]);

  // TODO(389019544): Fix the deopt loop and enable this:
  // assertTrue(isMaglevved(f));
  assertFalse(isMaglevved(f));  // This will fail when the issue is fixed.
}
