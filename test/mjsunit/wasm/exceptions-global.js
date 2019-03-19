// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --experimental-wasm-eh --allow-natives-syntax

// Note that this test does not pass --experimental-wasm-anyref on purpose so
// that we make sure the two flags can be controlled separately/independently.

load("test/mjsunit/wasm/wasm-module-builder.js");

// First we just test that "except_ref" global variables are allowed.
(function TestGlobalExceptRefSupported() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let g = builder.addGlobal(kWasmExceptRef);
  builder.addFunction("push_and_drop_except_ref", kSig_v_v)
      .addBody([
        kExprGetGlobal, g.index,
        kExprDrop,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertDoesNotThrow(instance.exports.push_and_drop_except_ref);
})();

// Test default value that global "except_ref" variables are initialized with.
(function TestGlobalExceptRefDefaultValue() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let g = builder.addGlobal(kWasmExceptRef);
  builder.addFunction('push_and_return_except_ref', kSig_e_v)
      .addBody([kExprGetGlobal, g.index])
      .exportFunc();
  let instance = builder.instantiate();

  assertEquals(null, instance.exports.push_and_return_except_ref());
})();

// TODO(mstarzinger): Add test coverage for the following:
//   - Catching exception in wasm and storing into global.
//   - Taking "except_ref" parameter and storing into global.
//   - Rethrowing "except_ref" from global (or parameter).
//   - Importing a global "except_ref" during instantiation.
