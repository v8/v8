// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --turbofan
// Flags: --typed-array-length-loading

function foo(size) {
  let a = new Uint8Array(size);
  let b = new Uint8Array(size + 1);
  b[a.length] = 1;
  return b;
}
%PrepareFunctionForOptimization(foo);

foo(100);

%OptimizeFunctionOnNextCall(foo);
const a1 = foo(100);
assertEquals(1, a1[100]);
assertOptimized(foo);

// TODO(389019544): This might or might not fail once the deopt loop is fixed.
if (%Is64Bit()) {
  try {
    // The test requires a length that exceeds Uint32/Int32/Smi; 4 GiB (2^32)
    // satisfies that while allowing both buffers in `foo` to fit in PA's 16 GiB
    // configurable pool.
    const largeLength = 4294967296;
    const a2 = foo(largeLength);
    assertEquals(1, a2[largeLength]);
    assertUnoptimized(foo);
  } catch (e) {
    // If alloating the TypedArray failed, we'll get a RangeError. Other
    // errors are just normal test failures.
    assertTrue(e instanceof RangeError);
  }
}
