// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-verify-load-elimination

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const builder = new WasmModuleBuilder();

const sig_i_v = builder.addType(kSig_i_v);
const array_type = builder.addArray(kWasmFuncRef, {mutable: true});
const sig_f = builder.addType(makeSig([wasmRefType(sig_i_v)], [kWasmI32]));

const g1 = builder.addFunction("g1", sig_i_v).addBody([kExprI32Const, 1]);
const g2 = builder.addFunction("g2", sig_i_v).addBody([kExprI32Const, 2]);

builder.addFunction("get_g1", makeSig([], [wasmRefType(sig_i_v)]))
  .addBody([kExprRefFunc, g1.index])
  .exportFunc();

builder.addFunction("get_g2", makeSig([], [wasmRefType(sig_i_v)]))
  .addBody([kExprRefFunc, g2.index])
  .exportFunc();

const elem_seg = builder.addPassiveElementSegment([g2.index, g2.index]);
builder.addDeclarativeElementSegment([g1.index]);

builder.addFunction("f", sig_f)
  .addBody([
    kExprLocalGet, 0,
    kExprCallRef, sig_i_v,
    kExprI32Const, 0,
    kExprI32Const, 2,
    kGCPrefix, kExprArrayNewElem, array_type, elem_seg,
    kExprI32Const, 1,
    kExprRefNull, kFuncRefCode,
    kGCPrefix, kExprArraySet, array_type,
    kExprLocalGet, 0,
    kExprCallRef, sig_i_v,
    kExprI32Add,
  ])
  .exportFunc();

const module = builder.toModule();

const inst1 = new WebAssembly.Instance(module);
const g1_inst1 = inst1.exports.get_g1();
const g2_inst1 = inst1.exports.get_g2();
// Collect polymorphic feedback for both call_ref sites, then tier up `f`.
for (let i = 0; i < 10; i++) {
  assertEquals(4, inst1.exports.f(g2_inst1));
}
assertEquals(2, inst1.exports.f(g1_inst1));
%WasmTierUpFunction(inst1.exports.f);

// On a fresh instance, calling `get_g1()` only initializes `func_refs[g1]`,
// leaving `func_refs[g2]` uninitialized. The `array.new_elem` in `f` lazily
// initializes the element segment, populating `func_refs[g2]`, so the second
// `call_ref`'s check on `func_refs[g2]` must not reuse the pre-call load.
const inst2 = new WebAssembly.Instance(module);
const g1_inst2 = inst2.exports.get_g1();
assertEquals(2, inst2.exports.f(g1_inst2));
