// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --no-wasm-lazy-compilation
// Flags: --turboshaft-wasm-32 --enable-testing-opcode-in-wasm

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Make sure turboshaft bails out graciously for non-implemented features.
(function I64Identity() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("id", makeSig([kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0n, wasm.id(0n));
  assertEquals(1n, wasm.id(1n));
  assertEquals(-1n, wasm.id(-1n));
  assertEquals(0x123456789ABCn, wasm.id(0x123456789ABCn));
  assertEquals(-0x123456789ABCn, wasm.id(-0x123456789ABCn));
})();

(function I64Constants() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("main", makeSig([], [kWasmI64, kWasmI64, kWasmI64]))
    .addBody([
      ...wasmI64Const(0),
      ...wasmI64Const(-12345),
      ...wasmI64Const(0x123456789ABCDEFn),
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals([0n, -12345n, 0x123456789ABCDEFn], wasm.main());
})();

(function I64Multiplication() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("mul", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Mul,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0n, wasm.mul(0n, 5n));
  assertEquals(0n, wasm.mul(5n, 0n));
  assertEquals(5n, wasm.mul(1n, 5n));
  assertEquals(-5n, wasm.mul(5n, -1n));
  assertEquals(35n, wasm.mul(-5n, -7n));
  assertEquals(0xfffffffffn * 0xfn, wasm.mul(0xfffffffffn, 0xfn));
})();

(function I64Addition() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("add", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Add,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(3n, wasm.add(1n, 2n));
  assertEquals(0n, wasm.add(100n, -100n));
  assertEquals(0x12345678n + 0xABCDEF1234n,
               wasm.add(0x12345678n, 0xABCDEF1234n));
})();

(function I64Subtraction() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("sub", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Sub,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(-1n, wasm.sub(1n, 2n));
  assertEquals(200n, wasm.sub(100n, -100n));
  assertEquals(0x12345678n - 0xABCDEF1234n,
               wasm.sub(0x12345678n, 0xABCDEF1234n));
  assertEquals(0n, wasm.sub(0x123456789ABCDEFn, 0x123456789ABCDEFn));
})();

(function I64BitAnd() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("and", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64And,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(
    0b10101010_00000000_11111111_01010101n, wasm.and(
    0b10101010_00000000_11111111_01010101n,
    0b10101010_00000000_11111111_01010101n));
  assertEquals(
    0b10101010_00000000_01010101_00000000n, wasm.and(
    0b10101010_00000000_11111111_01010101n,
    0b11111111_11111111_01010101_00000000n));
})();

(function I64BitOr() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("or", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Ior,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(
    0b10101010_00000000_11111111_01010101n, wasm.or(
    0b10101010_00000000_11111111_01010101n,
    0b10101010_00000000_11111111_01010101n));
  assertEquals(
    0b11111111_11111111_11111111_01010101n, wasm.or(
    0b10101010_00000000_11111111_01010101n,
    0b11111111_11111111_01010101_00000000n));
})();

(function I64BitXor() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("xor", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Xor,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(
    0b10101010_00000000_11111111_01010101n, wasm.xor(
    0b10101010_00000000_11111111_01010101n,
    0b00000000_00000000_00000000_00000000n));
  assertEquals(
    0b11111111_11111111_11111111_01010101n, wasm.xor(
    0b10101010_00000000_11111111_01010101n,
    0b01010101_11111111_00000000_00000000n));
})();
