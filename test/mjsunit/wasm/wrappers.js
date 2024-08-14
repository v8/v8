// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-wasm-generic-wrapper --turboshaft-wasm

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let wasm = (() => {
  let builder = new WasmModuleBuilder();
  builder.addFunction(`passAny`, makeSig([kWasmAnyRef], [kWasmAnyRef]))
  .addBody([kExprLocalGet, 0])
  .exportFunc();
  builder.addFunction(`passExtern`, makeSig([kWasmExternRef], [kWasmExternRef]))
  .addBody([kExprLocalGet, 0])
  .exportFunc();
  return builder.instantiate().exports;
})();

assertEquals(undefined, wasm.passAny());
assertEquals(null, wasm.passAny(null));
assertEquals(123, wasm.passAny(123));
assertEquals(-0.0, wasm.passAny(-0.0));

assertEquals(undefined, wasm.passExtern());
assertEquals(null, wasm.passExtern(null));
assertEquals(123, wasm.passExtern(123));
assertEquals(-0.0, wasm.passExtern(-0.0));
