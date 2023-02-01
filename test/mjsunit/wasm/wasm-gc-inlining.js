// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestInliningStructGet() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('createStruct', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternExternalize,
    ])
    .exportFunc();

  builder.addFunction('getElement', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;
  let n = 100;

  const createStruct = wasm.createStruct;
  const getElement = wasm.getElement;

  let fct = () => {
    let res = 0;
    for (let i = 1; i <= n; ++i) {
      const struct = createStruct(i);
      res += getElement(struct);
    }
    return res;
  }

  %PrepareFunctionForOptimization(fct);
  for (let i = 0; i < 10; ++i) {
    assertEquals((n * n + n) / 2, fct());
  }
  %OptimizeFunctionOnNextCall(fct);
  for (let i = 0; i < 10; ++i) {
    assertEquals((n * n + n) / 2, fct());
  }
})();
