// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --no-wasm-inlining
// Flags: --no-wasm-speculative-inlining

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestStructGetNullDereference() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  let main = builder.addFunction(
      "main", makeSig([wasmRefNullType(struct)], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  let instance = builder.instantiate();

  assertTraps(kTrapNullDereference, () => instance.exports.main(null));
})();
