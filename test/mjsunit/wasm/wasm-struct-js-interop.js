// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --experimental-wasm-gc --wasm-gc-js-interop
// Flags: --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const kIterationsCountForICProgression = 20;

// TODO(ishell): remove once leaked maps could keep NativeModule alive.
let instances = [];

function createStruct_i() {
  let builder = new WasmModuleBuilder();

  const type_index = builder.addStruct([
      {type: kWasmI32, mutability: true},
  ]);

  let sig_a_i = makeSig_r_x(kWasmDataRef, kWasmI32);
  let sig_i_a = makeSig_r_x(kWasmI32, kWasmDataRef);
  let sig_v_ai = makeSig([kWasmDataRef, kWasmI32], []);

  builder.addFunction("new_struct", sig_a_i)
    .addBody([
      kExprLocalGet, 0,                              // --
      kGCPrefix, kExprRttCanon, type_index,          // --
      kGCPrefix, kExprStructNewWithRtt, type_index]) // --
    .exportAs("new_struct");

  builder.addFunction("get_field", sig_i_a)
    .addBody([
      kExprLocalGet, 0,                          // --
      kGCPrefix, kExprRttCanon, type_index,      // --
      kGCPrefix, kExprRefCast,                   // --
      kGCPrefix, kExprStructGet, type_index, 0]) // --
    .exportAs("get_field");

  builder.addFunction("set_field", sig_v_ai)
    .addBody([
      kExprLocalGet, 0,                          // --
      kGCPrefix, kExprRttCanon, type_index,      // --
      kGCPrefix, kExprRefCast,                   // --
      kExprLocalGet, 1,                          // --
      kGCPrefix, kExprStructSet, type_index, 0]) // --
    .exportAs("set_field");

  let instance = builder.instantiate();
  instances.push(instance);
  let new_struct = instance.exports.new_struct;
  let get_field = instance.exports.get_field;
  let set_field = instance.exports.set_field;

  let value = 42;
  let o = new_struct(value);
  %DebugPrint(o);

  let res;
  res = get_field(o);
  assertEquals(value, res);

  set_field(o, 153);
  res = get_field(o);
  assertEquals(153, res);

  return o;
}

(function TestSimpleStructInterop() {
  function f(o) {
    for (let i = 0; i < kIterationsCountForICProgression; i++) {
      let v = o.$field0;
      assertEquals(153, v);
    }
  }

  let o = createStruct_i();
  %DebugPrint(o);

  f(o);
  gc();
})();
