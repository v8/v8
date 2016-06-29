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

function testBuiltinInStackTrace(script, nativeFuncName) {
  try {
    eval(script);
    assertUnreachable(nativeFuncName);
  } catch (e) {
    assertTrue(e.stack.indexOf(nativeFuncName) >= 0, nativeFuncName);
  }
}

// Use the full name ('String.getDate') in order to avoid false pass
// results when the method name is mentioned in the error message itself.
// This occurs, e.g., for Date.prototype.getYear, which uses a different code
// path and never hits the Generate_DatePrototype_GetField builtin.
testBuiltinInStackTrace("Date.prototype.getDate.call('')", "String.getDate");
testBuiltinInStackTrace("Date.prototype.getUTCDate.call('')",
                        "String.getUTCDate");
testBuiltinInStackTrace("Date.prototype.getTime.call('')", "String.getTime");
