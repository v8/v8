// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-simd

load("test/mjsunit/wasm/wasm-module-builder.js");

(function S128InSignatureThrows() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, 1, true);
  builder.addFunction('foo', kSig_s_i)
    .addBody([
      kExprGetLocal, 0,
      kSimdPrefix,
      kExprI32x4Splat])
    .exportFunc()
  const instance = builder.instantiate();
  assertThrows(() => instance.exports.foo(33), TypeError);
})();

(function S128ParamInSignatureThrows() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, 1, true);
  builder.addFunction('foo', kSig_i_s)
      .addBody([
          kExprGetLocal, 0,
          kSimdPrefix,
          kExprI32x4ExtractLane, 1])
      .exportFunc();
  const instance = builder.instantiate();
  assertThrows(() => instance.exports.invalid_foo(10), TypeError);
})();

(function ImportS128Return() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addImport('', 'f', makeSig([], [kWasmS128]));
  builder.addFunction('foo', kSig_v_v)
      .addBody([kExprCallFunction, 0, kExprDrop])
      .exportFunc();
  const instance = builder.instantiate({'': {f: _ => 1}});
  assertThrows(() => instance.exports.foo(), TypeError);
})();

(function S128ImportThrows() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_i_i);
  let sig_s128_index = builder.addType(kSig_i_s);
  let index = builder.addImport('', 'func', sig_s128_index);
  builder.addFunction('foo', sig_index)
    .addBody([
      kExprGetLocal, 0,
      kSimdPrefix,
      kExprI32x4Splat,
      kExprCallFunction, index])
    .exportFunc();
  const instance = builder.instantiate({'': {func: _ => {}}});
  assertThrows(() => instance.exports.foo(14), TypeError);
})();

(function TestS128GlobalConstructor() {
  assertThrows(() => new WebAssembly.Global({value: 'i128'}), TypeError);
})();
