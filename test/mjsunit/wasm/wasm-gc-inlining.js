// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --allow-natives-syntax --turbofan
// Flags: --no-always-turbofan --no-always-sparkplug --expose-gc
// Flags: --experimental-wasm-js-inlining

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function testOptimized(run, fctToOptimize) {
  fctToOptimize = fctToOptimize ?? run;
  %PrepareFunctionForOptimization(fctToOptimize);
  for (let i = 0; i < 10; ++i) {
    run();
  }
  %OptimizeFunctionOnNextCall(fctToOptimize);
  run();
  assertOptimized(fctToOptimize);
}

(function TestInliningStructGet() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('createStructNull', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternExternalize,
    ])
    .exportFunc();

  builder.addFunction('getElementNull', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  builder.addFunction('createStruct',
                      makeSig([kWasmI32], [wasmRefType(kWasmExternRef)]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternExternalize,
    ])
    .exportFunc();

  builder.addFunction('getElement',
                      makeSig([wasmRefType(kWasmExternRef)], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  // TODO(mliedtke): Consider splitting this loop as the reuse seems to prevent
  // proper feedback for the second iteration.
  for (let [create, get] of [
      [wasm.createStruct, wasm.getElement],
      [wasm.createStructNull, wasm.getElementNull]]) {
    let fct = () => {
      for (let i = 1; i <= 10; ++i) {
        const struct = create(i);
        assertEquals(i, get(struct));
      }
    };
    testOptimized(fct);

    // While these cases will all trap on the ref.cast, they cover very
    // different code paths in extern.internalize.
    print("Test exceptional cases");
    const trap = kTrapIllegalCast;
    print("- test get null");
    const getNull = () => get(null);
    testOptimized(() => assertTraps(trap, getNull), getNull);
    print("- test undefined");
    const getUndefined = () => get(undefined);
    testOptimized(() => assertTraps(trap, getUndefined), getUndefined);
    print("- test Smi");
    const getSmi = () => get(1);
    testOptimized(() => assertTraps(trap, getSmi), getSmi);
    print("- test -0");
    const getNZero = () => get(-0);
    testOptimized(() => assertTraps(trap, getNZero), getNZero);
    print("- test HeapNumber with fractional digits");
    const getFractional = () => get(0.5);
    testOptimized(() => assertTraps(trap, getFractional), getFractional);
    print("- test Smi/HeapNumber too large for i31ref");
    const getLargeNumber = () => get(0x4000_000);
    testOptimized(() => assertTraps(trap, getLargeNumber), getLargeNumber);

    print("- test inlining into try block");
    // TODO(7748): This is currently not supported by inlining yet.
    const getTry = () => {
      try {
        get(null);
      } catch (e) {
        assertTrue(e instanceof WebAssembly.RuntimeError);
        return;
      }
      assertUnreachable();
    };
    testOptimized(getTry);
  }
})();

(function TestInliningStructGetElementTypes() {
  print(arguments.callee.name);
  const i64Value = Number.MAX_SAFE_INTEGER;
  const f64Value = 11.1;
  const i8Value = 123;
  const i16Value = 456;
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([
    makeField(kWasmI64, true),
    makeField(kWasmF64, true),
    makeField(kWasmI8, true),
    makeField(kWasmI16, true),
  ]);

  builder.addFunction('createStruct', makeSig([], [kWasmExternRef]))
    .addBody([
      ...wasmI64Const(i64Value),
      ...wasmF64Const(f64Value),
      ...wasmI32Const(i8Value),
      ...wasmI32Const(i16Value),
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternExternalize,
    ])
    .exportFunc();

  builder.addFunction('getI64', makeSig([kWasmExternRef], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0,
    ])
    .exportFunc();
  builder.addFunction('getF64', makeSig([kWasmExternRef], [kWasmF64]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 1,
    ])
    .exportFunc();
  builder.addFunction('getI8', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCast, struct,
      // TODO(7748): Currently struct.get_s / struct.get_u does not get inlined.
      kGCPrefix, kExprStructGetS, struct, 2,
    ])
    .exportFunc();
  builder.addFunction('getI16', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCast, struct,
      // TODO(7748): Currently struct.get_s / struct.get_u does not get inlined.
      kGCPrefix, kExprStructGetU, struct, 3,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let structVal = wasm.createStruct();
  print("- getI64");
  let getI64 =
    () => assertEquals(BigInt(i64Value), wasm.getI64(structVal));
  testOptimized(getI64);
  print("- getF64");
  let getF64 = () => assertEquals(f64Value, wasm.getF64(structVal));
  testOptimized(getF64);
  print("- getI8");
  let getI8 = () => assertEquals(i8Value, wasm.getI8(structVal));
  testOptimized(getI8);
  print("- getI16");
  let getI16 = () => assertEquals(i16Value, wasm.getI16(structVal));
  testOptimized(getI16);
})();

(function TestInliningMultiModule() {
  print(arguments.callee.name);

  let createModule = (fieldType) => {
    let builder = new WasmModuleBuilder();
    let struct = builder.addStruct([makeField(fieldType, true)]);

    builder.addFunction('createStruct', makeSig([fieldType], [kWasmExternRef]))
      .addBody([
        kExprLocalGet, 0,
        kGCPrefix, kExprStructNew, struct,
        kGCPrefix, kExprExternExternalize,
      ])
      .exportFunc();

    builder.addFunction('get', makeSig([kWasmExternRef], [fieldType]))
      .addBody([
        kExprLocalGet, 0,
        kGCPrefix, kExprExternInternalize,
        kGCPrefix, kExprRefCast, struct,
        kGCPrefix, kExprStructGet, struct, 0])
      .exportFunc();

    let instance = builder.instantiate({});
    return instance.exports;
  };

  let moduleA = createModule(kWasmI32);
  let moduleB = createModule(kWasmF64);
  let structA = moduleA.createStruct(123);
  let structB = moduleB.createStruct(321);

  // Only one of the two calls can be fully inlined. For the other call only the
  // wrapper is inlined.
  let multiModule =
    () => assertEquals(444, moduleA.get(structA) + moduleB.get(structB));
  testOptimized(multiModule);

  // The struct types are incompatible (but both use type index 0).
  // One of the two calls gets inlined and the inlined and the non-inlined
  // function have to keep apart the different wasm modules.
  let i = 0;
  let multiModuleTrap =
    () => ++i % 2 == 0 ? moduleA.get(structB) : moduleB.get(structA);
  testOptimized(() => assertTraps(kTrapIllegalCast, () => multiModuleTrap()),
                multiModuleTrap);
})();

(function TestInliningTrapStackTrace() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('createStruct', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternExternalize,
    ])
    .exportFunc();

  builder.addFunction('getElement', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  const getTrap = () => wasm.getElement(null);
  const testTrap = () => {
    try {
      getTrap();
      assertUnreachable();
    } catch(e) {
      // TODO(7748): The stack trace should always contain the wasm frame, even
      // if it was inlined. The regex should be:
      //   /illegal cast[\s]+at getElement \(wasm:/
      // For now we assert that the stack trace isn't fully broken and contains
      // at least the `getTrap()` call above.
      assertMatches(/illegal cast[\s]+at [.\s\S]*getTrap/, e.stack);
    }
  };
  testOptimized(testTrap, getTrap);
})();
