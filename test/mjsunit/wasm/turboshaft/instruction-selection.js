// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --no-wasm-lazy-compilation
// Flags: --turboshaft-wasm --turboshaft-wasm-instruction-selection

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TrivialFunctions() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("id", makeSig([kWasmI32], [kWasmI32]))
    .addBody([kExprLocalGet, 0])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals(42, wasm.id(42));
})();
