// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const builder = new WasmModuleBuilder();

builder.startRecGroup();
const sig_g = builder.addType(makeSig([], [wasmRefType(0)]));
builder.endRecGroup();
const sig_f = builder.addType(makeSig([wasmRefType(sig_g)], [wasmRefType(sig_g)]));

const g1 = builder.addFunction("g1", sig_g)
  .addBody([kExprRefFunc, 1])
  .exportFunc();
const g2 = builder.addFunction("g2", sig_g)
  .addBody([kExprRefFunc, 1]);
assertEquals(1, g2.index);

builder.addDeclarativeElementSegment([g1.index, g2.index]);

builder.addFunction("f", sig_f)
  .addBody([
    kExprLocalGet, 0,
    kExprCallRef, sig_g,
    kExprCallRef, sig_g,
  ])
  .exportFunc();

const module = builder.toModule();

const inst1 = new WebAssembly.Instance(module);
const g2_inst1 = inst1.exports.g1();
// Collect polymorphic feedback [g2, g1] on the first call_ref and monomorphic
// feedback [g2] on the second call_ref, then tier up `f`.
inst1.exports.f(g2_inst1);
inst1.exports.f(g2_inst1);
inst1.exports.f(inst1.exports.g1);
%WasmTierUpFunction(inst1.exports.f);

// On a fresh instance, func_refs[g2] is still uninitialized until the inlined
// `g1` executes `ref.func g2`. The second call_ref must not reuse the stale
// pre-`ref.func` load of func_refs[g2] and spuriously deoptimize.
const inst2 = new WebAssembly.Instance(module);
inst2.exports.f(inst2.exports.g1);
if (%IsWasmTieringPredictable()) {
  assertEquals(0, %WasmDeoptsExecutedForFunction(inst2.exports.f));
  assertTrue(%IsTurboFanFunction(inst2.exports.f));
}
