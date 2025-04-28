// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-shared

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function AnyConvertExternValid() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("main",
    makeSig([wasmRefNullType(kWasmExternRef).shared()],
            [wasmRefNullType(kWasmAnyRef).shared()]))
  .addBody([kExprLocalGet, 0, kGCPrefix, kExprAnyConvertExtern])
  .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(null, wasm.main(null));
  assertEquals(1, wasm.main(1));
  assertEquals("a", wasm.main("a"));
})();

(function AnyConvertExternInvalid() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("main",
    makeSig([wasmRefNullType(kWasmExternRef).shared()],
            [wasmRefNullType(kWasmAnyRef)]))
  .addBody([kExprLocalGet, 0, kGCPrefix, kExprAnyConvertExtern])
  .exportFunc();
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /expected anyref, got shared anyref/);

  builder = new WasmModuleBuilder();
  builder.addFunction("main",
    makeSig([wasmRefNullType(kWasmExternRef)],
            [wasmRefNullType(kWasmAnyRef).shared()]))
  .addBody([kExprLocalGet, 0, kGCPrefix, kExprAnyConvertExtern])
  .exportFunc();
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /expected shared anyref, got anyref/);
})();

(function ExternConvertAnyValid() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("main",
    makeSig([wasmRefNullType(kWasmAnyRef).shared()],
            [wasmRefNullType(kWasmExternRef).shared()]))
  .addBody([kExprLocalGet, 0, kGCPrefix, kExprExternConvertAny])
  .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(null, wasm.main(null));
  assertEquals(1, wasm.main(1));
  assertEquals("a", wasm.main("a"));
})();

(function ExternConvertAnyInvalid() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("main",
    makeSig([wasmRefNullType(kWasmAnyRef).shared()],
            [wasmRefNullType(kWasmExternRef)]))
  .addBody([kExprLocalGet, 0, kGCPrefix, kExprExternConvertAny])
  .exportFunc();
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /expected externref, got shared externref/);

  builder = new WasmModuleBuilder();
  builder.addFunction("main",
    makeSig([wasmRefNullType(kWasmAnyRef)],
            [wasmRefNullType(kWasmExternRef).shared()]))
  .addBody([kExprLocalGet, 0, kGCPrefix, kExprExternConvertAny])
  .exportFunc();
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /expected shared externref, got externref/);
})();
