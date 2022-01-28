// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --experimental-wasm-stack-switching
// Flags: --experimental-wasm-type-reflection --expose-gc

load("test/mjsunit/wasm/wasm-module-builder.js");

(function TestSuspender() {
  print(arguments.callee.name);
  let suspender = new WebAssembly.Suspender();
  assertTrue(suspender.toString() == "[object WebAssembly.Suspender]");
  assertThrows(() => WebAssembly.Suspender(), TypeError,
      /WebAssembly.Suspender must be invoked with 'new'/);
})();

(function TestStackSwitchNoSuspend() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, true).exportAs('g');
  builder.addFunction("test", kSig_v_v)
      .addBody([kExprI32Const, 42, kExprGlobalSet, 0]).exportFunc();
  let instance = builder.instantiate();
  let suspender = new WebAssembly.Suspender();
  let wrapper = suspender.returnPromiseOnSuspend(instance.exports.test);
  wrapper();
  assertEquals(42, instance.exports.g.value);
})();

(function TestStackSwitchSuspend() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, true).exportAs('g');
  import_index = builder.addImport('m', 'import', kSig_i_v);
  builder.addFunction("test", kSig_v_v)
      .addBody([
          kExprCallFunction, import_index, // suspend
          kExprGlobalSet, 0 // resume
      ]).exportFunc();
  let suspender = new WebAssembly.Suspender();
  function js_import() {
    return new Promise((resolve) => { resolve(42); });
  };
  let wasm_js_import = new WebAssembly.Function({parameters: [], results: ['i32']}, js_import);
  let suspending_wasm_js_import = suspender.suspendOnReturnedPromise(wasm_js_import);
  let instance = builder.instantiate({m: {import: suspending_wasm_js_import}});
  let wrapped_export = suspender.returnPromiseOnSuspend(instance.exports.test);
  let combined_promise = wrapped_export();
  assertEquals(0, instance.exports.g.value);
  combined_promise.then(_ => assertEquals(42, instance.exports.g.value));
})();

// Check that we can suspend back out of a resumed computation.
(function TestStackSwitchSuspendLoop() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, true).exportAs('g');
  import_index = builder.addImport('m', 'import', kSig_i_v);
  // Pseudo-code for the wasm function:
  // for (i = 0; i < 5; ++i) {
  //   g = g + import();
  // }
  builder.addFunction("test", kSig_v_v)
      .addLocals(kWasmI32, 1)
      .addBody([
          kExprI32Const, 5,
          kExprLocalSet, 0,
          kExprLoop, kWasmVoid,
            kExprCallFunction, import_index, // suspend
            kExprGlobalGet, 0, // resume
            kExprI32Add,
            kExprGlobalSet, 0,
            kExprLocalGet, 0,
            kExprI32Const, 1,
            kExprI32Sub,
            kExprLocalTee, 0,
            kExprBrIf, 0,
          kExprEnd,
      ]).exportFunc();
  let suspender = new WebAssembly.Suspender();
  let i = 0;
  // The n-th call to the import returns a promise that resolves to n.
  function js_import() {
    return new Promise((resolve) => { resolve(++i); });
  };
  let wasm_js_import = new WebAssembly.Function({parameters: [], results: ['i32']}, js_import);
  let suspending_wasm_js_import = suspender.suspendOnReturnedPromise(wasm_js_import);
  let instance = builder.instantiate({m: {import: suspending_wasm_js_import}});
  let wrapped_export = suspender.returnPromiseOnSuspend(instance.exports.test);
  let chained_promise = wrapped_export();
  assertEquals(0, instance.exports.g.value);
  chained_promise.then(_ => assertEquals(15, instance.exports.g.value));
})();

(function TestStackSwitchGC() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let gc_index = builder.addImport('m', 'gc', kSig_v_v);
  builder.addFunction("test", kSig_v_v)
      .addBody([kExprCallFunction, gc_index]).exportFunc();
  let instance = builder.instantiate({'m': {'gc': gc}});
  let suspender = new WebAssembly.Suspender();
  let wrapper = suspender.returnPromiseOnSuspend(instance.exports.test);
  wrapper();
})();
