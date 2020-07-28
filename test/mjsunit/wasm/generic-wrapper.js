// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-generic-wrapper

load("test/mjsunit/wasm/wasm-module-builder.js");

(function testGenericWrapper() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_v_v);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func() {
    x = 20;
  }

  builder.instantiate({ mod: { func: import_func } }).exports.main();
  assertEquals(x, 20);
})();

(function testGenericWrapperFunctionTraps() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_v_v);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprUnreachable
    ])
    .exportFunc();

  let instance = builder.instantiate();
  assertTraps(kTrapUnreachable, instance.exports.main);
})();
