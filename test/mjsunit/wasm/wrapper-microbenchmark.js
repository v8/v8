// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-wasm-generic-wrapper

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function MeasureAbstractStructTypeWrapper() {
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  let producer = builder.addFunction("producer", makeSig([], [kWasmStructRef]))
    .addBody([kGCPrefix, kExprStructNewDefault, struct])
    .exportFunc();

  let id = builder.addFunction("id",
                               makeSig([kWasmStructRef], [kWasmStructRef]))
    .addBody([kExprLocalGet, 0])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  let num_repetitions = 10_000_000;

  let struct_obj = wasm.producer();

  let start = Date.now();

  for (let i = 0; i < num_repetitions; i++) {
    wasm.id(struct_obj);
  }

  print("Abstract type result: " + (Date.now() - start) + "ms");
})();

(function MeasureIndexedStructTypeWrapper() {
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  let struct_type = wasmRefNullType(struct);

  let producer = builder.addFunction("producer", makeSig([], [struct_type]))
    .addBody([kGCPrefix, kExprStructNewDefault, struct])
    .exportFunc();

  let id = builder.addFunction("id", makeSig([struct_type], [struct_type]))
    .addBody([kExprLocalGet, 0])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  let num_repetitions = 10_000_000;

  let struct_obj = wasm.producer();

  let start = Date.now();

  for (let i = 0; i < num_repetitions; i++) {
    wasm.id(struct_obj);
  }

  print("Indexed type result: " + (Date.now() - start) + "ms");
})();

(function MeasureIndexedStructSubtypeWrapper() {
  let builder = new WasmModuleBuilder();

  let super_struct = builder.addStruct([makeField(kWasmI32, true)]);
  let sub_struct = builder.addStruct({
    fields: [makeField(kWasmI32, true), makeField(kWasmI32, true)],
    supertype: super_struct
  });

  let producer = builder.addFunction(
      "producer", makeSig([], [wasmRefType(sub_struct)]))
    .addBody([kGCPrefix, kExprStructNewDefault, sub_struct])
    .exportFunc();

  let id = builder.addFunction(
      "id", makeSig([wasmRefNullType(super_struct)],
                    [wasmRefNullType(super_struct)]))
    .addBody([kExprLocalGet, 0])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  let num_repetitions = 10_000_000;

  let struct_obj = wasm.producer();

  let start = Date.now();

  for (let i = 0; i < num_repetitions; i++) {
    wasm.id(struct_obj);
  }

  print("Indexed subtype result: " + (Date.now() - start) + "ms");
})();
