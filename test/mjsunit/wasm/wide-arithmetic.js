// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-wide-arithmetic

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// TODO(ryandiaz): these tests are just placeholders for now, until we have
// implemented the wide arithmetic instructions.
let kSig_ll_llll = makeSig(
  [kWasmI64, kWasmI64, kWasmI64, kWasmI64],
  [kWasmI64, kWasmI64]
);
let kSig_ll_ll = makeSig([kWasmI64, kWasmI64], [kWasmI64, kWasmI64]);

function testAdd128() {
  console.log("Testing add128...");

  let builder = new WasmModuleBuilder();
  builder.addFunction("add128", kSig_ll_llll).exportFunc().addBody([
    kExprLocalGet, 0, kExprLocalGet, 1,
    kExprLocalGet, 2, kExprLocalGet, 3,
    kNumericPrefix, kExprI64Add128,
  ]);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /add128.*Wide arithmetic opcodes are not yet implemented./);
}

function testSub128() {
  console.log("Testing sub128...");

  let builder = new WasmModuleBuilder();
  builder.addFunction("sub128", kSig_ll_llll).exportFunc().addBody([
    kExprLocalGet, 0, kExprLocalGet, 1,
    kExprLocalGet, 2, kExprLocalGet, 3,
    kNumericPrefix, kExprI64Sub128,
  ]);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /sub128.*Wide arithmetic opcodes are not yet implemented./);
}

function testMulWideS() {
  console.log("Testing mulWideS...");

  let builder = new WasmModuleBuilder();
  builder.addFunction("mulWideS", kSig_ll_ll)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kNumericPrefix, kExprI64MulWideS,
    ]);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /mulWideS.*Wide arithmetic opcodes are not yet implemented./);
}

function testMulWideU() {
  console.log("Testing mulWideU...");

  let builder = new WasmModuleBuilder();
  builder.addFunction("mulWideU", kSig_ll_ll)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kNumericPrefix, kExprI64MulWideU,
    ]);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /mulWideU.*Wide arithmetic opcodes are not yet implemented./);
}

testAdd128();
testSub128();
testMulWideS();
testMulWideU();
