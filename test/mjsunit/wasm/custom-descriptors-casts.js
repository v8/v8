// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-custom-descriptors

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();
builder.startRecGroup();
let $desc0 = builder.nextTypeIndex() + 1;
let $s0 = builder.addStruct({fields: [], descriptor: $desc0});
builder.addStruct({fields: [], describes: $s0});
let $desc1 = builder.nextTypeIndex() + 1;
let $s1 = builder.addStruct({fields: [], descriptor: $desc1, final: true});
builder.addStruct({fields: [], describes: $s1, final: true});
builder.endRecGroup();

let $glob0 = builder.addGlobal(kWasmAnyRef, true, false, [
  kGCPrefix, kExprStructNew, $desc0,
  kGCPrefix, kExprStructNew, $s0,
]);
let $glob1 = builder.addGlobal(kWasmAnyRef, true, false, [
  kGCPrefix, kExprStructNew, $desc1,
  kGCPrefix, kExprStructNew, $s1,
]);

function MakeFuncs(name, type_index, global_index) {
  let sig_r_v = makeSig([], [wasmRefType(type_index)]);
  builder.addFunction("cast" + name, sig_r_v).exportFunc().addBody([
    kExprGlobalGet, global_index,
    kGCPrefix, kExprRefCast, kWasmExact, type_index,
  ]);
  builder.addFunction("test" + name, kSig_i_v).exportFunc().addBody([
    kExprGlobalGet, global_index,
    kGCPrefix, kExprRefTest, kWasmExact, type_index,
  ]);
  builder.addFunction("br" + name, kSig_i_v).exportFunc().addBody([
    kExprBlock, kAnyRefCode,
    kExprGlobalGet, global_index,
    kGCPrefix, kExprBrOnCast, 0b11, 0, kAnyRefCode, kWasmExact, type_index,
    kExprI32Const, 0,  // Branch not taken.
    kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprI32Const, 1,  // Branch taken.
  ]);
  builder.addFunction("brfail" + name, kSig_i_v).exportFunc().addBody([
    kExprBlock, kAnyRefCode,
    kExprGlobalGet, global_index,
    kGCPrefix, kExprBrOnCastFail, 0b11, 0, kAnyRefCode, kWasmExact, type_index,
    kExprI32Const, 1,  // Branch not taken.
    kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprI32Const, 0,  // Branch taken.
  ]);
}

MakeFuncs("", $s0, $glob0.index);
MakeFuncs("_final", $s1, $glob1.index);

let instance = builder.instantiate();

instance.exports.cast();  // Should not trap.
instance.exports.cast_final();  // Should not trap.
assertEquals(1, instance.exports.test());
assertEquals(1, instance.exports.test_final());
assertEquals(1, instance.exports.br());
assertEquals(1, instance.exports.br_final());
assertEquals(1, instance.exports.brfail());
assertEquals(1, instance.exports.brfail_final());
