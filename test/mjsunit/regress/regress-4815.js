// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var thrower = { [Symbol.toPrimitive]: () => FAIL };

// Tests that a native conversion function is included in the
// stack trace.
function testTraceNativeConversion(nativeFunc) {
  var nativeFuncName = nativeFunc.name;
  try {
    nativeFunc(thrower);
    assertUnreachable(nativeFuncName);
  } catch (e) {
    assertTrue(e.stack.indexOf(nativeFuncName) >= 0, nativeFuncName);
  }
}

testTraceNativeConversion(Math.max);
testTraceNativeConversion(Math.min);
