// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This test exercises the I64x2Mul logic in Liftoff, hence the flags.
// Flags: --wasm-staging --experimental-wasm-simd --liftoff --no-wasm-tier-up

load('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32, false, true);
builder.addGlobal(kWasmI32, 0);
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addType(makeSig([], []));
// Generate function 1 (out of 3).
builder.addFunction(undefined, 0 /* sig */)
  .addLocals({f32_count: 10})
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprI32Const, 0x80, 0x01,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprI32Const, 0x92, 0x01,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, kExprI64x2Mul, 0x01,  // i64x2.mul
kSimdPrefix, kExprI16x8ExtractLaneU, 0x00,  // i16x8.extract_lane_u
kExprEnd,  // end @19
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertEquals(instance.exports.main(1, 2, 3), 18688);
