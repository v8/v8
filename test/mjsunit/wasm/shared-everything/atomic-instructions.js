// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-shared

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestAtomicGetInvalidType() {
  for (let is_shared of [true, false]) {
    const types = [
      kWasmF32,
      kWasmF64,
      kWasmS128,
      kWasmI8,
      kWasmI16,
      is_shared ? wasmRefNullType(kWasmExternRef).shared() : kWasmExternRef,
      is_shared ? wasmRefNullType(kWasmNullExternRef).shared()
               : kWasmNullExternRef,
    ];
    for (const [i, type] of types.entries()) {
      print(
        `${arguments.callee.name} ${is_shared ? "shared" : "unshared"} ${i}`);
      let builder = new WasmModuleBuilder();
      let struct = builder.addStruct({
        fields: [makeField(type, true)],
        is_shared,
      });
      let consumer_sig = makeSig([wasmRefNullType(struct)], []);
      builder.addFunction("atomicGet", consumer_sig)
        .addBody([
          kExprLocalGet, 0,
          kAtomicPrefix, kExprStructAtomicGet, kAtomicSeqCst, struct, 0,
          kExprDrop
        ])
        .exportFunc();

      assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
      /struct\.atomic\.get: Field 0 of type 0 has invalid type/);
    }
  }
})();

(function TestAtomicGet() {
  for (let is_shared of [true, false]) {
    print(`${arguments.callee.name} ${is_shared ? "shared" : "unshared"}`);
    let builder = new WasmModuleBuilder();
    let anyRefT = is_shared
      ? wasmRefNullType(kWasmAnyRef).shared()
      : wasmRefNullType(kWasmAnyRef);
    let struct = builder.addStruct({
      fields: [
        // DO NOT REORDER OR INSERT EXTRA FIELDS IN BETWEEN!
        // The i64 is intentionally "unaligned".
        makeField(kWasmI32, true),
        makeField(kWasmI64, true),
        makeField(anyRefT, true)
      ],
      is_shared,
    });
    let producer_sig = makeSig(
      [kWasmI32, kWasmI64, anyRefT],
      [wasmRefType(struct)]);
    builder.addFunction("newStruct", producer_sig)
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kExprLocalGet, 2,
        kGCPrefix, kExprStructNew, struct])
      .exportFunc();
    builder.addFunction("atomicGet32",
        makeSig([wasmRefNullType(struct)], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,
        kAtomicPrefix, kExprStructAtomicGet, kAtomicSeqCst, struct, 0,
      ])
      .exportFunc();
    builder.addFunction("atomicGet64",
        makeSig([wasmRefNullType(struct)], [kWasmI64]))
      .addBody([
        kExprLocalGet, 0,
        kAtomicPrefix, kExprStructAtomicGet, kAtomicSeqCst, struct, 1,
      ])
      .exportFunc();
    builder.addFunction("atomicGetRef", makeSig(
        [wasmRefNullType(struct)],
        [anyRefT]))
      .addBody([
        kExprLocalGet, 0,
        kAtomicPrefix, kExprStructAtomicGet, kAtomicSeqCst, struct, 2,
      ])
      .exportFunc();
    builder.addFunction("atomicGetRefRef", makeSig(
        [wasmRefNullType(struct)],
        [anyRefT]))
      .addBody([
        kExprLocalGet, 0,
        kAtomicPrefix, kExprStructAtomicGet, kAtomicSeqCst, struct, 2,
        kGCPrefix, kExprRefCast, struct,
        kAtomicPrefix, kExprStructAtomicGet, kAtomicSeqCst, struct, 2,
      ])
      .exportFunc();

    let wasm = builder.instantiate().exports;
    let structObj = wasm.newStruct(42, -64n, "test");
    assertEquals(42, wasm.atomicGet32(structObj));
    assertEquals(-64n, wasm.atomicGet64(structObj));
    assertEquals("test", wasm.atomicGetRef(structObj));
    let structStruct = wasm.newStruct(1, 2n, structObj);
    assertEquals("test", wasm.atomicGetRefRef(structStruct));
    assertTraps(kTrapNullDereference, () => wasm.atomicGet32(null));
    assertTraps(kTrapNullDereference, () => wasm.atomicGet64(null));
    assertTraps(kTrapNullDereference, () => wasm.atomicGetRef(null));
  }
})();

(function TestAtomicGetPacked() {
  for (let is_shared of [true, false]) {
    print(`${arguments.callee.name} ${is_shared ? "shared" : "unshared"}`);
    let builder = new WasmModuleBuilder();
    let struct = builder.addStruct({
      fields: [
        makeField(kWasmI8, true),
        makeField(kWasmI8, true),
        makeField(kWasmI16, true),
        makeField(kWasmI16, true),
      ],
      is_shared,
    });
    let producer_sig = makeSig([kWasmI32, kWasmI32], [wasmRefType(struct)]);
    builder.addFunction("newStruct", producer_sig)
      .addBody([
        kExprI32Const, 42,
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        ...wasmI32Const(12_345),
        kGCPrefix, kExprStructNew, struct])
      .exportFunc();
    builder.addFunction("atomicGetS8",
        makeSig([wasmRefNullType(struct)], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,
        kAtomicPrefix, kExprStructAtomicGetS, kAtomicSeqCst, struct, 1,
      ])
      .exportFunc();
    builder.addFunction("atomicGetS16",
        makeSig([wasmRefNullType(struct)], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,
        kAtomicPrefix, kExprStructAtomicGetS, kAtomicSeqCst, struct, 2,
      ])
      .exportFunc();
    builder.addFunction("atomicGetU8",
        makeSig([wasmRefNullType(struct)], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,
        kAtomicPrefix, kExprStructAtomicGetU, kAtomicSeqCst, struct, 1,
      ])
      .exportFunc();
    builder.addFunction("atomicGetU16",
        makeSig([wasmRefNullType(struct)], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,
        kAtomicPrefix, kExprStructAtomicGetU, kAtomicSeqCst, struct, 2,
      ])
      .exportFunc();

    let wasm = builder.instantiate().exports;
    let structPos = wasm.newStruct(12, 3456);
    assertEquals(12, wasm.atomicGetS8(structPos));
    assertEquals(12, wasm.atomicGetU8(structPos));
    assertEquals(3456, wasm.atomicGetS16(structPos));
    assertEquals(3456, wasm.atomicGetU16(structPos));
    let structNeg = wasm.newStruct(-12, -3456);
    assertEquals(-12, wasm.atomicGetS8(structNeg));
    assertEquals(244, wasm.atomicGetU8(structNeg));
    assertEquals(-3456, wasm.atomicGetS16(structNeg));
    assertEquals(62080, wasm.atomicGetU16(structNeg));
    assertTraps(kTrapNullDereference, () => wasm.atomicGetS8(null));
    assertTraps(kTrapNullDereference, () => wasm.atomicGetS16(null));
  }
})();
