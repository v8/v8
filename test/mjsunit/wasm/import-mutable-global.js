// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-mut-global

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

(function TestBasic() {
  let global = new WebAssembly.Global({type: 'i32', value: 1});
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmI32);
  builder.addFunction("main", kSig_i_v)
    .addBody([kExprGetGlobal, 0])
    .exportAs("main");

  let main = builder.instantiate({mod: {g: global}}).exports.main;
  assertEquals(1, main());
})();

(function TestTypeMismatch() {
  let global = new WebAssembly.Global({type: 'f32', value: 1});
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmI32);
  builder.addFunction("main", kSig_i_v)
    .addBody([kExprGetGlobal, 0])
    .exportAs("main");

  assertThrows(() => builder.instantiate({mod: {g: global}}));
})();

(function TestImportI64AsNumber() {
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmI64);
  assertThrows(() => builder.instantiate({mod: {g: 1234}}));
})();
