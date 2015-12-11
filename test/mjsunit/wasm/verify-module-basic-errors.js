// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function Foo() { }

assertThrows(function() { WASM.verifyModule(); })
assertThrows(function() { WASM.verifyModule(0); })
assertThrows(function() { WASM.verifyModule("s"); })
assertThrows(function() { WASM.verifyModule(undefined); })
assertThrows(function() { WASM.verifyModule(1.1); })
assertThrows(function() { WASM.verifyModule(1/0); })
assertThrows(function() { WASM.verifyModule(null); })
assertThrows(function() { WASM.verifyModule(new Foo()); })
assertThrows(function() { WASM.verifyModule(new ArrayBuffer(0)); })
assertThrows(function() { WASM.verifyModule(new ArrayBuffer(7)); })
