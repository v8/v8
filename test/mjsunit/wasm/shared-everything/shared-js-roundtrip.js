// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-shared

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

let struct =
    builder.addStruct({fields: [makeField(kWasmI32, true)], shared: false});
let shared_struct =
    builder.addStruct({fields: [makeField(kWasmI32, true)], shared: true});

builder.addFunction("producer", makeSig([], [wasmRefType(struct)]))
  .addBody([kGCPrefix, kExprStructNewDefault, struct])
  .exportFunc();

builder.addFunction(
    "shared_producer", makeSig([], [wasmRefType(shared_struct)]))
  .addBody([kGCPrefix, kExprStructNewDefault, shared_struct])
  .exportFunc();

let struct_type = wasmRefNullType(kWasmStructRef);
builder.addFunction("id", makeSig([struct_type], [struct_type]))
  .addBody([kExprLocalGet, 0])
  .exportFunc();

let shared_struct_type = wasmRefNullType(kWasmStructRef).shared();
builder.addFunction(
    "shared_id", makeSig([shared_struct_type], [shared_struct_type]))
  .addBody([kExprLocalGet, 0])
  .exportFunc();

let wasm = builder.instantiate().exports;

// We can roundtrip a non-shared struct as structref and a shared struct as
// (shared structref)...
wasm.id(wasm.producer());
wasm.shared_id(wasm.shared_producer());

// ... but not vice versa.
assertThrows(
    () => wasm.id(wasm.shared_producer()),
    TypeError,
    'type incompatibility when transforming from/to JS');
assertThrows(
    () => wasm.shared_id(wasm.producer()),
    TypeError,
    'type incompatibility when transforming from/to JS');
