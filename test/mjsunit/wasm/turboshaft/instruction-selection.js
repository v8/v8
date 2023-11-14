// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --no-wasm-lazy-compilation --experimental-wasm-stringref
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

(function ArithmeticInt32() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let maddSig = makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]);
  builder.addFunction("madd", maddSig)
    .addBody([
      // local[0] + (local[1] * local[2])
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprI32Mul,
      kExprI32Add
    ])
    .exportFunc();
  builder.addFunction("madd2", maddSig)
    .addBody([
      // (local[0] * local[1]) + local[2]
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32Mul,
      kExprLocalGet, 2,
      kExprI32Add
    ])
    .exportFunc();
  builder.addFunction("addConstant", makeSig([kWasmI32], [kWasmI32]))
    .addBody([kExprLocalGet, 0, ...wasmI32Const(-7), kExprI32Add])
    .exportFunc();
  builder.addFunction("add", makeSig([kWasmI32, kWasmI32], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction("msub", maddSig)
    .addBody([
      // local[0] - (local[1] * local[2])
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprI32Mul,
      kExprI32Sub
    ])
    .exportFunc();
  builder.addFunction("subConstant", makeSig([kWasmI32], [kWasmI32]))
    .addBody([kExprLocalGet, 0, ...wasmI32Const(-7), kExprI32Sub])
    .exportFunc();
  builder.addFunction("sub", makeSig([kWasmI32, kWasmI32], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Sub])
    .exportFunc();
  builder.addFunction("mulConstant", makeSig([kWasmI32], [kWasmI32]))
    // Can be lowered to local[0] + local[0] << 3.
    .addBody([kExprLocalGet, 0, ...wasmI32Const(9), kExprI32Mul])
    .exportFunc();
  builder.addFunction("mulNegate", makeSig([kWasmI32, kWasmI32], [kWasmI32]))
    .addBody([
      // (0 - local[0]) * local[1]
      kExprI32Const, 0,
      kExprLocalGet, 0,
      kExprI32Sub,
      kExprLocalGet, 1,
      kExprI32Mul])
    .exportFunc();
  builder.addFunction("mulNegate2", makeSig([kWasmI32, kWasmI32], [kWasmI32]))
    .addBody([
      // local[0] * (0 - local[1])
      kExprLocalGet, 0,
      kExprI32Const, 0,
      kExprLocalGet, 1,
      kExprI32Sub,
      kExprI32Mul])
    .exportFunc();
  builder.addFunction("mul", makeSig([kWasmI32, kWasmI32], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(3 + (4 * 5), wasm.madd(3, 4, 5));
  assertEquals((3 * 4) + 5, wasm.madd2(3, 4, 5));
  assertEquals(5 + -7, wasm.addConstant(5));
  assertEquals(5 + -7, wasm.add(5, -7));

  assertEquals(3 - (4 * 5), wasm.msub(3, 4, 5));
  assertEquals(5 - -7, wasm.subConstant(5));
  assertEquals(5 - -7, wasm.sub(5, -7));

  assertEquals(42 * 9, wasm.mulConstant(42));
  assertEquals(-42 * 4, wasm.mulNegate(42, 4));
  assertEquals(42 * -4, wasm.mulNegate2(42, 4));
  assertEquals(5 * -7, wasm.mul(5, -7));
})();

(function ArithmeticInt64() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let maddSig = makeSig([kWasmI64, kWasmI64, kWasmI64], [kWasmI64]);
  builder.addFunction("madd", maddSig)
    .addBody([
      // local[0] + (local[1] * local[2])
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprI64Mul,
      kExprI64Add
    ])
    .exportFunc();
  builder.addFunction("madd2", maddSig)
    .addBody([
      // (local[0] * local[1]) + local[2]
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Mul,
      kExprLocalGet, 2,
      kExprI64Add
    ])
    .exportFunc();
  builder.addFunction("addConstant", makeSig([kWasmI64], [kWasmI64]))
    .addBody([kExprLocalGet, 0, ...wasmI64Const(-7), kExprI64Add])
    .exportFunc();
  builder.addFunction("add", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI64Add])
    .exportFunc();
  builder.addFunction("msub", maddSig)
    .addBody([
      // local[0] - (local[1] * local[2])
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprI64Mul,
      kExprI64Sub
    ])
    .exportFunc();
  builder.addFunction("subConstant", makeSig([kWasmI64], [kWasmI64]))
    .addBody([kExprLocalGet, 0, ...wasmI64Const(-7), kExprI64Sub])
    .exportFunc();
  builder.addFunction("sub", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI64Sub])
    .exportFunc();
  builder.addFunction("mulConstant", makeSig([kWasmI64], [kWasmI64]))
    // Can be lowered to local[0] + local[0] << 3.
    .addBody([kExprLocalGet, 0, ...wasmI64Const(9), kExprI64Mul])
    .exportFunc();
  builder.addFunction("mulNegate", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      // (0 - local[0]) * local[1]
      kExprI64Const, 0,
      kExprLocalGet, 0,
      kExprI64Sub,
      kExprLocalGet, 1,
      kExprI64Mul])
    .exportFunc();
  builder.addFunction("mulNegate2", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      // local[0] * (0 - local[1])
      kExprLocalGet, 0,
      kExprI64Const, 0,
      kExprLocalGet, 1,
      kExprI64Sub,
      kExprI64Mul])
    .exportFunc();
  builder.addFunction("mul", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI64Mul])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(3n + (4n * 5n), wasm.madd(3n, 4n, 5n));
  assertEquals((3n * 4n) + 5n, wasm.madd2(3n, 4n, 5n));
  assertEquals(5n + -7n, wasm.addConstant(5n));
  assertEquals(5n + -7n, wasm.add(5n, -7n));

  assertEquals(3n - (4n * 5n), wasm.msub(3n, 4n, 5n));
  assertEquals(5n - -7n, wasm.subConstant(5n));
  assertEquals(5n - -7n, wasm.sub(5n, -7n));

  assertEquals(42n * 9n, wasm.mulConstant(42n));
  assertEquals(-42n * 4n, wasm.mulNegate(42n, 4n));
  assertEquals(42n * -4n, wasm.mulNegate2(42n, 4n));
  assertEquals(5n * -7n, wasm.mul(5n, -7n));
})();

(function Loads() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addFunction("isString", makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprAnyConvertExtern,
    kGCPrefix, kExprRefTest, kStringRefCode,
  ])
  .exportFunc();

  // Loops emit stack checks (which includes loading from the root register).
  let loop_sig = builder.addType(kSig_i_i);
  // Works for positive numbers only.
  builder.addFunction("factorial", kSig_i_i)
    .addBody([
      kExprI32Const, 1,
      kExprLoop, loop_sig,
        kExprLocalGet, 0,
        kExprI32Mul,
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kExprI32Sub,
        kExprLocalTee, 0,
        kExprI32Const, 1,
        kExprI32GtS,
        kExprBrIf, 0,
      kExprEnd])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0, wasm.isString({}));
  assertEquals(0, wasm.isString(1));
  assertEquals(0, wasm.isString(1.5));
  assertEquals(0, wasm.isString(-0.0));
  assertEquals(0, wasm.isString(null));
  assertEquals(1, wasm.isString("test"));

  assertEquals(1, wasm.factorial(1));
  assertEquals(24, wasm.factorial(4));
  assertEquals(720, wasm.factorial(6));
})();
