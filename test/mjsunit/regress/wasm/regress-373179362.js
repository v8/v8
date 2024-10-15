// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const __v_36 = new WasmModuleBuilder();
__v_36.addFunction('add_reduce', kSig_f_ffff)
    .addLocals(kWasmS128, 1)
    .addBody([
    kExprLocalGet, 0,  // $var0
    kSimdPrefix, kExprF32x4Splat,
    kExprLocalGet, 1,  // $var1
    kSimdPrefix, kExprF32x4ReplaceLane, 1,
    kExprLocalGet, 2,  // $var2
    kSimdPrefix, kExprF32x4ReplaceLane, 2,
    kExprLocalGet, 3,  // $var3
    kSimdPrefix, kExprF32x4ReplaceLane, 3,
    kExprLocalTee, 4,  // $var4
    kExprLocalGet, 4,  // $var4
    kExprLocalGet, 4,  // $var4
    kSimdPrefix, kExprI8x16Shuffle, 8, 18, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0,
    ...SimdInstr(kExprF32x4Add),
    kExprLocalTee, 4,  // $var4
    kExprLocalGet, 4,  // $var4
    kExprLocalGet, 4,  // $var4
    kSimdPrefix, kExprI8x16Shuffle, 4, 5, 6, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    ...SimdInstr(kExprF32x4Add),
    kSimdPrefix, kExprF32x4ExtractLane, 0,
    ])
    .exportFunc();
const __v_37 = __v_36.instantiate().exports;
assertNotEquals(NaN, __v_37.add_reduce(-1.56657e-37, -1.56657e-37, 0));
