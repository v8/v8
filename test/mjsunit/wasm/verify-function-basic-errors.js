// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function Foo() { }

assertThrows(function() { WASM.verifyFunction(); })
assertThrows(function() { WASM.verifyFunction(0); })
assertThrows(function() { WASM.verifyFunction("s"); })
assertThrows(function() { WASM.verifyFunction(undefined); })
assertThrows(function() { WASM.verifyFunction(1.1); })
assertThrows(function() { WASM.verifyFunction(1/0); })
assertThrows(function() { WASM.verifyFunction(null); })
assertThrows(function() { WASM.verifyFunction(new Foo()); })
assertThrows(function() { WASM.verifyFunction(new ArrayBuffer(0)); })
assertThrows(function() { WASM.verifyFunction(new ArrayBuffer(140000)); })
