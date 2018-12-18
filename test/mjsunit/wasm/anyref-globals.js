// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-anyref --expose-gc

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDefaultValue() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  const g_nullref = builder.addGlobal(kWasmAnyRef, true);
  builder.addFunction("main", kSig_r_v)
    .addBody([kExprGetGlobal, g_nullref.index])
    .exportAs("main");

  const instance = builder.instantiate();
  assertNull(instance.exports.main());
})();

(function TestDefaultValueSecondGlobal() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  const g_setref = builder.addGlobal(kWasmAnyRef, true);
  const g_nullref = builder.addGlobal(kWasmAnyRef, true);
  builder.addFunction("main", kSig_r_r)
    .addBody([
        kExprGetLocal, 0,
        kExprSetGlobal, g_setref.index,
        kExprGetGlobal, g_nullref.index
    ])
    .exportAs("main");

  const instance = builder.instantiate();
  assertNull(instance.exports.main({}));
})();

(function TestGlobalChangeValue() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  // Dummy global for offset.
  builder.addGlobal(kWasmAnyRef, true);
  const g = builder.addGlobal(kWasmAnyRef, true);
  builder.addFunction("main", kSig_r_r)
    .addBody([
        kExprGetLocal, 0,
        kExprSetGlobal, g.index,
        kExprGetGlobal, g.index
    ])
    .exportAs("main");

  const instance = builder.instantiate();

  const test_value = {hello: 'world'};
  assertSame(test_value, instance.exports.main(test_value));
})();

(function TestGlobalChangeValueWithGC() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  const gc_index = builder.addImport("q", "gc", kSig_v_v);
  // Dummy global for offset.
  builder.addGlobal(kWasmAnyRef, true);
  const g = builder.addGlobal(kWasmAnyRef, true);
  builder.addFunction("main", kSig_r_r)
    .addBody([
        kExprGetLocal, 0,
        kExprSetGlobal, g.index,
        kExprCallFunction, gc_index,  // call gc
        kExprGetGlobal, g.index
    ])
    .exportAs("main");

  const instance = builder.instantiate({q: {gc: gc}});

  const test_value = {hello: 'world'};
  assertSame(test_value, instance.exports.main(test_value));
  assertSame(5, instance.exports.main(5));
  assertSame("Hello", instance.exports.main("Hello"));
})();

(function TestGlobalAsRoot() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  const g = builder.addGlobal(kWasmAnyRef, true);
  builder.addFunction("get_global", kSig_r_v)
    .addBody([
        kExprGetGlobal, g.index
    ])
    .exportAs("get_global");

  builder.addFunction("set_global", kSig_v_r)
    .addBody([
        kExprGetLocal, 0,
        kExprSetGlobal, g.index
    ])
    .exportAs("set_global");

  const instance = builder.instantiate();

  let test_value = {hello: 'world'};
  instance.exports.set_global(test_value);
  test_value = null;
  gc();

  const result = instance.exports.get_global();

  assertEquals('world', result.hello);
})();
