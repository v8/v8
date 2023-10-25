// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-exnref

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Test that "exnref" local variables are allowed.
(function TestLocalExnRef() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("push_and_drop_exnref", kSig_v_v)
      .addLocals(kWasmExnRef, 1)
      .addBody([
        kExprLocalGet, 0,
        kExprDrop,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertDoesNotThrow(instance.exports.push_and_drop_exnref);
})();
