// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-fp16 --wasm-revectorize --allow-natives-syntax
//
// Revectorization packs two adjacent i32x4.add into one 256-bit add. The
// ExtractF128 of lane 0 is elided, so the 256-bit register feeds the F16x8
// Qfma/Qfms below directly as its third input. On x64 with AVX10.1
// (--enable-avx10-1, needs a V8_ENABLE_AVX10_1 build and an AVX10.1 CPU) the
// code generator must treat that Simd256 register as a Simd128 register.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const builder = new WasmModuleBuilder();
builder.addMemory(1, 1);

for (let [name, op] of [['madd', kExprF16x8Qfma], ['nmadd', kExprF16x8Qfms]]) {
  let f = builder.addFunction(
      name, makeSig([kWasmI32, kWasmI32, kWasmF32], [kWasmF32]));
  f.addLocals(kWasmS128, 2);
  let x = 3, y = 4;  // After the 3 parameters.
  f.addBody([
    // x = mem[p + 0] + splat(bits), y = mem[p + 16] + splat(bits). With zeroed
    // memory, lane 0 of x holds the f16 bit pattern `bits`.
    kExprLocalGet, 0, kSimdPrefix, kExprS128LoadMem, 0, 0,
    kExprLocalGet, 1, kSimdPrefix, kExprI32x4Splat,
    kSimdPrefix, ...wasmUnsignedLeb(kExprI32x4Add),
    kExprLocalSet, x,
    kExprLocalGet, 0, kSimdPrefix, kExprS128LoadMem, 0, 16,
    kExprLocalGet, 1, kSimdPrefix, kExprI32x4Splat,
    kSimdPrefix, ...wasmUnsignedLeb(kExprI32x4Add),
    kExprLocalSet, y,
    // Adjacent stores are the revectorization seeds.
    kExprLocalGet, 0, kExprLocalGet, x, kSimdPrefix, kExprS128StoreMem, 0, 32,
    kExprLocalGet, 0, kExprLocalGet, y, kSimdPrefix, kExprS128StoreMem, 0, 48,
    // f16 qfma(splat(f), splat(f), x), where x is lane 0 of the packed add.
    kExprLocalGet, 2, kSimdPrefix, ...kExprF16x8Splat,
    kExprLocalGet, 2, kSimdPrefix, ...kExprF16x8Splat,
    kExprLocalGet, x,
    kSimdPrefix, ...op,
    kSimdPrefix, ...kExprF16x8ExtractLane, 0,
  ]).exportFunc();
}

const instance = builder.instantiate();
const kBitsOne = 0x3c00;  // f16 bit pattern of 1.0, ends up in lane 0 of x.

function check() {
  // Memory is zero-initialized, so x lane 0 is f16 1.0.
  // 2.0 * 2.0 + 1.0 = 5.0
  assertEquals(5, instance.exports.madd(0, kBitsOne, 2));
  // -(2.0 * 2.0) + 1.0 = -3.0
  assertEquals(-3, instance.exports.nmadd(0, kBitsOne, 2));
}

// Call the functions normally first (they start out in Liftoff).
check();

// Revectorization only runs in TurboFan, so tier up and check again.
%WasmTierUpFunction(instance.exports.madd);
%WasmTierUpFunction(instance.exports.nmadd);
check();
