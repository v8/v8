// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

const ab = new ArrayBuffer(100);
var constTypedArray = new Uint16Array(ab);

function fillTypedArray() {
  for (let i = 0; i < constTypedArray.length; ++i) {
    constTypedArray[i] = i;
  }
}
%NeverOptimizeFunction(fillTypedArray);

fillTypedArray();

function foo(i) {
  return constTypedArray[i];
}
%PrepareFunctionForOptimization(foo);

assertEquals(3, foo(3));
assertEquals(10, foo(10));

%OptimizeMaglevOnNextCall(foo);

assertEquals(3, foo(3));
assertEquals(10, foo(10));

assertTrue(isMaglevved(foo));

// Detaching the buffer deopts.
%ArrayBufferDetach(ab);
assertFalse(isMaglevved(foo));
