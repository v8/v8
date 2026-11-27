// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-wasmfx --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();
let sig_v_v = builder.addType(kSig_v_v);
let sig_i_i = builder.addType(kSig_i_i);
let sig_i_v = builder.addType(kSig_i_v);
let cont_i_i = builder.addCont(sig_i_i);
let cont_i_v = builder.addCont(sig_i_v);
let tag_i_v = builder.addTag(kSig_i_v);
let g_cont_i_i = builder.addGlobal(wasmRefNullType(cont_i_i), true, false);

let many_types = [kWasmI32, kWasmI64, kWasmF32, kWasmF64, kWasmS128,
    kWasmExternRef, kWasmExternRef, kWasmS128, kWasmF64, kWasmF32, kWasmI64,
    kWasmI32];
let num_bound = 6;
let num_unbound = 6;
let num_types = many_types.length;
let sig_many = builder.addType(makeSig(many_types, many_types));
let sig_many_v = builder.addType(makeSig([], many_types));
let cont_many = builder.addCont(sig_many);
let cont_many_v = builder.addCont(sig_many_v);
let tag_many_v = builder.addTag(sig_many_v);
let sig_after_bind = builder.addType(
    makeSig(many_types.slice(-num_unbound), many_types));
let cont_after_bind = builder.addCont(sig_after_bind);
let g_cont_many = builder.addGlobal(wasmRefNullType(cont_many), true, false);

let sig_r_r = builder.addType(kSig_r_r);
let sig_r_v = builder.addType(kSig_r_v);
let cont_r_r = builder.addCont(sig_r_r);
let cont_r_v = builder.addCont(sig_r_v);
let tag_r_v = builder.addTag(kSig_r_v);
let g_cont_r_r = builder.addGlobal(wasmRefNullType(cont_r_r), true, false);

let g_ref = builder.addGlobal(kWasmExternRef, true).exportAs('g_ref');

function GenerateValuesOfType(types) {
  body = [];
  for (type of types) {
    switch (type) {
      case kWasmI32:
        body.push(...wasmI32Const(12));
        break;
      case kWasmI64:
        body.push(...wasmI64Const(34));
        break;
      case kWasmF32:
        body.push(...wasmF32Const(56));
        break;
      case kWasmF64:
        body.push(...wasmF64Const(78));
        break;
      case kWasmS128:
        body.push(...wasmS128Const(Math.PI, Math.E));
        break;
      case kWasmExternRef:
        body.push(kExprGlobalGet, g_ref.index);
        break;
    }
  }
  return body;
}

function CheckValuesOfType(types, block_sig, simd_local) {
  let body = [
      kExprBlock, block_sig, // OK.
      kExprBlock, block_sig, // Trap.
  ];
  for (type of types) {
    switch (type) {
      case kWasmI32:
        body.push(...wasmI32Const(12),
            kExprI32Eq,
            kExprBrIf, 1);
        break;
      case kWasmI64:
        body.push(...wasmI64Const(34),
            kExprI64Eq,
            kExprBrIf, 1);
        break;
      case kWasmF32:
        body.push(...wasmF32Const(56),
            kExprF32Eq,
            kExprBrIf, 1);
        break;
      case kWasmF64:
        body.push(...wasmF64Const(56),
            kExprF64Eq,
            kExprBrIf, 1);
        break;
      case kWasmS128:
        body.push(
            kExprLocalTee, simd_local,
            kSimdPrefix, kExprF64x2ExtractLane, 0,
            ...wasmF64Const(Math.PI),
            kExprF64Eq,
            kExprBrIf, 1,
            kExprLocalGet, simd_local,
            kSimdPrefix, kExprF64x2ExtractLane, 0,
            ...wasmF64Const(Math.E),
            kExprF64Eq,
            kExprBrIf, 1);
        break;
      case kWasmExternRef:
        // Just save it, check it outside of the function.
        body.push(kExprGlobalSet, g_ref.index,
            kExprBr, 1);
        break;
    }
  }
  body.push(
    kExprBr, 1, // All equal, escape to outer block.
    kExprEnd,
    kExprUnreachable,
    kExprEnd,
  );
  return body;
}

let gc_index = builder.addImport("m", "gc", kSig_v_v);
let identity_i32 = builder.addFunction("identity_i32", kSig_i_i)
    .addBody([
        kExprLocalGet, 0,
    ]).exportFunc();
let suspend_i32 = builder.addFunction("suspend_i32", kSig_i_v)
    .addBody([
        kExprSuspend, tag_i_v,
    ]).exportFunc();
let block_sig = builder.addType(
    makeSig([], [kWasmI32, wasmRefType(cont_i_i)]));
builder.addFunction("test_i32_new_cont", kSig_i_i)
    .addBody([
        kExprLocalGet, 0,
        kExprRefFunc, identity_i32.index,
        kExprContNew, cont_i_i,
        kExprContBind, cont_i_i, cont_i_v,
        kExprCallFunction, gc_index,
        kExprResume, cont_i_v, 0,
    ]).exportFunc();
block_sig = builder.addType(makeSig([], [wasmRefType(cont_i_i)]));
builder.addFunction("test_i32_suspended", kSig_i_i)
    .addBody([
        kExprBlock, block_sig,
          kExprRefFunc, suspend_i32.index,
          kExprContNew, cont_i_v,
          kExprResume, cont_i_v, 1, kOnSuspend, tag_i_v, 0,
          kExprUnreachable,
        kExprEnd,
        kExprGlobalSet, g_cont_i_i.index,
        kExprLocalGet, 0,
        kExprGlobalGet, g_cont_i_i.index,
        kExprContBind, cont_i_i, cont_i_v,
        kExprCallFunction, gc_index,
        kExprResume, cont_i_v, 0,
    ]).exportFunc();

let identity_many = builder.addFunction("identity_many", sig_many)
    .addBody([
        ...Array.from({ length: num_types },
                      (_, i) => [kExprLocalGet, i]).flat()
    ]).exportFunc();
let suspend_many = builder.addFunction("suspend_many", sig_many_v)
    .addBody([
        kExprSuspend, tag_many_v,
    ]).exportFunc();
check_block_sig = builder.addType(makeSig(many_types, []));
builder.addFunction("test_new_cont_many_args", kSig_v_v)
    .addLocals(kWasmS128, 1)
    .addBody([
        ...GenerateValuesOfType(many_types.slice(-num_unbound)),
        ...GenerateValuesOfType(many_types.slice(0, num_bound)),
        kExprRefFunc, identity_many.index,
        kExprContNew, cont_many,
        kExprContBind, cont_many, cont_after_bind,
        kExprCallFunction, gc_index,
        kExprResume, cont_after_bind, 0,
        ...CheckValuesOfType(many_types.slice().reverse(),
                             check_block_sig, 0)
    ]).exportFunc();
block_sig = builder.addType(makeSig([], [wasmRefType(cont_many)]));
builder.addFunction("test_suspended_many_args", kSig_v_v)
    .addLocals(kWasmS128, 1)
    .addBody([
        kExprBlock, block_sig,
          kExprRefFunc, suspend_many.index,
          kExprContNew, cont_many_v,
          kExprResume, cont_many_v, 1, kOnSuspend, tag_many_v, 0,
          kExprUnreachable,
        kExprEnd,
        kExprGlobalSet, g_cont_many.index,
        ...GenerateValuesOfType(many_types.slice(-num_unbound)),
        ...GenerateValuesOfType(many_types.slice(0, num_bound)),
        kExprGlobalGet, g_cont_many.index,
        kExprContBind, cont_many, cont_after_bind,
        kExprCallFunction, gc_index,
        kExprResume, cont_after_bind, 0,
        ...CheckValuesOfType(many_types.slice().reverse(),
                             check_block_sig, 0)
    ]).exportFunc();
builder.addFunction("test_new_cont_many_args_chained", kSig_v_v)
    .addLocals(kWasmS128, 1)
    .addBody([
        ...GenerateValuesOfType(many_types.slice(-num_unbound)),
        ...GenerateValuesOfType(many_types.slice(0, num_bound)),
        kExprRefFunc, identity_many.index,
        kExprContNew, cont_many,
        kExprContBind, cont_many, cont_after_bind,
        kExprCallFunction, gc_index,
        // Fill the remaining arguments with a second cont.bind before
        // resuming.
        kExprContBind, cont_after_bind, cont_many_v,
        kExprCallFunction, gc_index,
        kExprResume, cont_many_v, 0,
        ...CheckValuesOfType(many_types.slice().reverse(),
                             check_block_sig, 0)
    ]).exportFunc();
builder.addFunction("test_suspended_many_args_chained", kSig_v_v)
    .addLocals(kWasmS128, 1)
    .addBody([
        kExprBlock, block_sig,
          kExprRefFunc, suspend_many.index,
          kExprContNew, cont_many_v,
          kExprResume, cont_many_v, 1, kOnSuspend, tag_many_v, 0,
          kExprUnreachable,
        kExprEnd,
        kExprGlobalSet, g_cont_many.index,
        ...GenerateValuesOfType(many_types.slice(-num_unbound)),
        ...GenerateValuesOfType(many_types.slice(0, num_bound)),
        kExprGlobalGet, g_cont_many.index,
        kExprContBind, cont_many, cont_after_bind,
        kExprCallFunction, gc_index,
        // Fill the remaining arguments with a second cont.bind before
        // resuming.
        kExprContBind, cont_after_bind, cont_many_v,
        kExprCallFunction, gc_index,
        kExprResume, cont_many_v, 0,
        ...CheckValuesOfType(many_types.slice().reverse(),
                             check_block_sig, 0)
    ]).exportFunc();
block_sig = builder.addType(makeSig([], [wasmRefType(cont_r_r)]));
let suspend_ref = builder.addFunction("suspend_ref", kSig_r_v)
    .addBody([
        kExprSuspend, tag_r_v,
    ]);
let call_suspend_ref = builder.addFunction("call_suspend_ref", kSig_r_v)
    .addBody([
        kExprCallFunction, suspend_ref.index,
        // One GC call just to overwrite the callee frame.
        kExprCallFunction, gc_index,
        // Second GC call should not attempt to visit the callee frame's
        // argument buffer.
        kExprCallFunction, gc_index,
    ]).exportFunc();
builder.addFunction("test_gc_after_suspend", kSig_r_r)
    .addBody([
        kExprBlock, block_sig,
          kExprRefFunc, call_suspend_ref.index,
          kExprContNew, cont_r_v,
          kExprResume, cont_r_v, 1, kOnSuspend, tag_r_v, 0,
          kExprUnreachable,
        kExprEnd,
        kExprGlobalSet, g_cont_r_r.index,
        kExprLocalGet, 0,
        kExprGlobalGet, g_cont_r_r.index,
        kExprContBind, cont_r_r, cont_r_v,
        kExprResume, cont_r_v, 0,
    ]).exportFunc();

let instance = builder.instantiate({m: {gc}});

(function TestI32() {
  print(arguments.callee.name);
  assertEquals(123, instance.exports.test_i32_new_cont(123));
  assertEquals(123, instance.exports.test_i32_suspended(123));
})();

(function TestManyArgs() {
  print(arguments.callee.name);
  let ref = {};
  instance.exports.g_ref.value = ref;
  instance.exports.test_new_cont_many_args();
  assertEquals(ref, instance.exports.g_ref.value);
  instance.exports.test_suspended_many_args();
  assertEquals(ref, instance.exports.g_ref.value);
  instance.exports.test_new_cont_many_args_chained();
  assertEquals(ref, instance.exports.g_ref.value);
  instance.exports.test_suspended_many_args_chained();
  assertEquals(ref, instance.exports.g_ref.value);
})();

// Check that the GC does not attempt to visit bound arguments after the
// suspended frame has returned.
(function TestGCAfterSuspend() {
  print(arguments.callee.name);
  let ref = {};
  instance.exports.g_ref.value = ref;
  instance.exports.test_gc_after_suspend();
  assertEquals(ref, instance.exports.g_ref.value);
})();

// Check lazy param_types_ initialization when cont.bind is called on a
// continuation suspended by `switch` (with a young WasmGC struct ref + GC).
(function TestSwitchThenContBindWithGC() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
  let struct_ref = wasmRefNullType(struct_type);
  builder.startRecGroup();
  let dummy_sig = builder.addType(kSig_v_v);
  let dummy_cont = builder.addCont(dummy_sig);
  let ret_sig = builder.addType(
      makeSig([kWasmI32, struct_ref, wasmRefNullType(dummy_cont)],
              [kWasmI32]));
  let ret_cont = builder.addCont(ret_sig);
  let bound_ret_sig = builder.addType(
      makeSig([wasmRefNullType(dummy_cont)], [kWasmI32]));
  let bound_ret_cont = builder.addCont(bound_ret_sig);
  let target_sig = builder.addType(
      makeSig([wasmRefNullType(ret_cont)], [kWasmI32]));
  let target_cont = builder.addCont(target_sig);
  builder.endRecGroup();
  let main_cont = builder.addCont(kSig_i_v);

  let tag0 = builder.addTag(makeSig([], [kWasmI32]));
  let gc_idx = builder.addImport("m", "gc", kSig_v_v);
  let g_ret_cont = builder.addGlobal(wasmRefNullType(ret_cont), true);

  let target = builder.addFunction("target", target_sig)
      .addBody([
        kExprLocalGet, 0,
        kExprGlobalSet, g_ret_cont.index,
        ...wasmI32Const(7),
      ]);

  let switcher = builder.addFunction("switcher", kSig_i_v)
      .addBody([
        kExprRefFunc, target.index,
        kExprContNew, target_cont,
        kExprSwitch, target_cont, tag0,
        kExprDrop,
        kGCPrefix, kExprStructGet, struct_type, 0,
        kExprI32Add,
      ]);

  builder.addFunction("run", kSig_i_ii)
      .addLocals(wasmRefNullType(bound_ret_cont), 1)
      .addBody([
        kExprRefFunc, switcher.index,
        kExprContNew, main_cont,
        kExprResume, main_cont, 1,
          kOnSwitch, tag0,
        kExprDrop,
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kGCPrefix, kExprStructNew, struct_type,
        kExprGlobalGet, g_ret_cont.index,
        kExprContBind, ret_cont, bound_ret_cont,
        kExprLocalSet, 2,
        kExprRefNull, ret_cont,
        kExprGlobalSet, g_ret_cont.index,
        kExprCallFunction, gc_idx,
        kExprRefNull, dummy_cont,
        kExprLocalGet, 2,
        kExprResume, bound_ret_cont, 0,
      ]).exportFunc();

  builder.addDeclarativeElementSegment([target.index, switcher.index]);
  let instance = builder.instantiate(
      {m: {gc: () => { gc({type: 'minor'}); gc(); }}});
  assertEquals(141, instance.exports.run(99, 42));
})();

// Check zero-arg cont.bind (narrowing ref type) followed by ref-arg
// cont.bind + GC, plus on_suspend with subtyping between tag returns and
// continuation params.
(function TestZeroArgContBindThenBindWithGC() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
  let struct_ref = wasmRefType(struct_type);
  let sig_eq_i = builder.addType(makeSig([kWasmEqRef, kWasmI32], [kWasmI32]));
  let cont_eq_i = builder.addCont(sig_eq_i);
  let sig_struct_i =
      builder.addType(makeSig([struct_ref, kWasmI32], [kWasmI32]));
  let cont_struct_i = builder.addCont(sig_struct_i);
  let sig_i_i_b = builder.addType(kSig_i_i);
  let cont_i_i_b = builder.addCont(sig_i_i_b);
  let sig_i_v_b = builder.addType(kSig_i_v);
  let cont_i_v_b = builder.addCont(sig_i_v_b);
  let tag_any_i = builder.addTag(makeSig([], [kWasmAnyRef, kWasmI32]));
  let gc_idx = builder.addImport("m", "gc", kSig_v_v);

  let block_sig = builder.addType(makeSig([], [wasmRefType(cont_eq_i)]));
  let suspender = builder.addFunction("suspender", sig_i_v_b)
      .addLocals(kWasmI32, 1)
      .addBody([
        kExprSuspend, tag_any_i,
        kExprLocalSet, 0,
        kGCPrefix, kExprRefCast, struct_type,
        kGCPrefix, kExprStructGet, struct_type, 0,
        kExprLocalGet, 0,
        kExprI32Add,
      ]);

  builder.addFunction("run", kSig_i_ii)
      .addLocals(wasmRefNullType(cont_struct_i), 1)
      .addLocals(wasmRefNullType(cont_i_i_b), 1)
      .addBody([
        kExprBlock, block_sig,
          kExprRefFunc, suspender.index,
          kExprContNew, cont_i_v_b,
          kExprResume, cont_i_v_b, 1, kOnSuspend, tag_any_i, 0,
          kExprUnreachable,
        kExprEnd,
        // Zero-arg cont.bind narrowing eqref -> struct_ref.
        kExprContBind, cont_eq_i, cont_struct_i,
        kExprLocalSet, 2,
        // Bind a young struct_ref, then GC before resuming with remaining i32.
        kExprLocalGet, 0,
        kGCPrefix, kExprStructNew, struct_type,
        kExprLocalGet, 2,
        kExprContBind, cont_struct_i, cont_i_i_b,
        kExprLocalSet, 3,
        kExprRefNull, cont_struct_i,
        kExprLocalSet, 2,
        kExprCallFunction, gc_idx,
        kExprLocalGet, 1,
        kExprLocalGet, 3,
        kExprResume, cont_i_i_b, 0,
      ]).exportFunc();

  builder.addDeclarativeElementSegment([suspender.index]);
  let instance = builder.instantiate(
      {m: {gc: () => { gc({type: 'minor'}); gc(); }}});
  assertEquals(141, instance.exports.run(99, 42));
})();
