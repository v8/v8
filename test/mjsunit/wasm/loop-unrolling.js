// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-typed-funcref --wasm-loop-unrolling

load("test/mjsunit/wasm/wasm-module-builder.js");

(function MultiBlockResultTest() {
  let builder = new WasmModuleBuilder();

  builder.addFunction("main", kSig_i_i)
    .addBody([
      ...wasmI32Const(1),
        kExprLet, kWasmStmt, 1, 1, kWasmI32,
        kExprLoop, kWasmStmt,
          ...wasmI32Const(10),
          kExprLet, kWasmStmt, 1, 1, kWasmI32,
            kExprLocalGet, 0,
            kExprLocalGet, 1,
            kExprI32Sub,
            kExprLocalGet, 2,
            kExprI32Add,
            kExprReturn, // (second let) - (first let) + parameter
          kExprEnd,
        kExprEnd,
      kExprEnd,
      ...wasmI32Const(0)])
    .exportAs("main");

  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module);
  assertEquals(instance.exports.main(100), 109);
})();
