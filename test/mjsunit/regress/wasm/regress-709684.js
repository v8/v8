// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --allow-natives-syntax

load('test/mjsunit/wasm/wasm-constants.js');
load('test/mjsunit/wasm/wasm-module-builder.js');

const importingModuleBinary1 = (() => {
  const builder = new WasmModuleBuilder();
  builder.addImport('', 'f', kSig_i_v);
  return new Int8Array(builder.toBuffer());
})();

const importingModuleBinary2 = (() => {
  const builder = new WasmModuleBuilder();
  builder.addImport('', 'f', kSig_i_v);
  builder.addFunction('g', kSig_v_v)
    .addBody([kExprNop]);
  return new Int8Array(builder.toBuffer());
})();

const importingModuleBinary3 = (() => {
  const builder = new WasmModuleBuilder();
  builder.addImport('', 'f', kSig_i_v);
  builder.addImport('', 'f2', kSig_i_v);
  builder.addFunction('g', kSig_v_v)
    .addBody([kExprNop]);
  return new Int8Array(builder.toBuffer());
})();

const importingModuleBinary4 = (() => {
  const builder = new WasmModuleBuilder();
  builder.addFunction('g', kSig_v_v)
    .addBody([kExprNop]);
  return new Int8Array(builder.toBuffer());
})();

const complexImportingModuleBinary1 = (() => {
  const builder = new WasmModuleBuilder();

  builder.addImport('a', 'b', kSig_v_v);
  builder.addImportedMemory('c', 'd', 1);
  builder.addImportedTable('e', 'f', 1);
  builder.addImportedGlobal('g', '⚡', kWasmI32);
  builder.addFunction('g', kSig_v_v)
    .addBody([kExprNop]);
  return builder.toBuffer();
})();

const complexImportingModuleBinary2 = (() => {
  const builder = new WasmModuleBuilder();

  builder.addImport('a', 'b', kSig_v_v);
  builder.addImportedMemory('c', 'd', 1);
  builder.addImportedTable('e', 'f', 1);
  builder.addImportedGlobal('g', '⚡', kWasmI32);
  builder.addFunction('g', kSig_v_v)
    .addBody([kExprNop]);
  return builder.toBuffer();
})();

const tests = [
  importingModuleBinary1,
  importingModuleBinary2,
  importingModuleBinary3,
  importingModuleBinary4,
  complexImportingModuleBinary1,
  complexImportingModuleBinary2
];

for (const test of tests) {
  assertPromiseFulfills(WebAssembly.compile(test))
    .then(m => { assertTrue(m instanceof WebAssembly.Module) });
}
