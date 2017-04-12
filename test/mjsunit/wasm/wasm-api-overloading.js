// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

let buffer = (() => {
  let builder = new WasmModuleBuilder();
  builder.addFunction("f", kSig_i_v)
    .addBody([kExprI32Const, 42])
    .exportAs("f");
  return builder.toBuffer();
})();

var module = new WebAssembly.Module(buffer);
var wrapper = Promise.resolve(module);

assertPromiseResult(
  WebAssembly.instantiate(wrapper),
  assertUnreachable,
  e => assertTrue(e instanceof TypeError));

assertPromiseResult((
  () => {
    var old = %SetWasmCompileFromPromiseOverload();
    var ret = WebAssembly.instantiate(wrapper);
    %ResetWasmOverloads(old);
    return ret;
  })(),
  pair => {
    assertTrue(pair.instance instanceof WebAssembly.Instance);
    assertTrue(pair.module instanceof WebAssembly.Module)
  },
  assertUnreachable);
