// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --experimental-wasm-eh --experimental-wasm-simd --allow-natives-syntax

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

//TODO(mstarzinger): Duplicated in the exceptions.js file. Dedupe.
function assertWasmThrows(instance, runtime_id, values, code) {
  try {
    if (typeof code === 'function') {
      code();
    } else {
      eval(code);
    }
  } catch (e) {
    assertInstanceof(e, WebAssembly.RuntimeError);
    var e_runtime_id = %GetWasmExceptionId(e, instance);
    assertTrue(Number.isInteger(e_runtime_id));
    assertEquals(e_runtime_id, runtime_id);
    var e_values = %GetWasmExceptionValues(e);
    assertArrayEquals(values, e_values);
    return;  // Success.
  }
  throw new MjsUnitAssertionError('Did not throw expected <' + runtime_id +
                                  '> with values: ' + values);
}

(function TestThrowS128Default() {
  var builder = new WasmModuleBuilder();
  var kSig_v_s = makeSig([kWasmS128], []);
  var except = builder.addException(kSig_v_s);
  builder.addFunction("throw_simd", kSig_v_v)
      .addLocals({s128_count: 1})
      .addBody([
        kExprGetLocal, 0,
        kExprThrow, 0,
      ])
      .exportFunc();
  var instance = builder.instantiate();

  assertWasmThrows(instance, except, [0, 0, 0, 0, 0, 0, 0, 0],
                   () => instance.exports.throw_simd());
})();

(function TestThrowCatchS128Default() {
  var builder = new WasmModuleBuilder();
  var kSig_v_s = makeSig([kWasmS128], []);
  var except = builder.addException(kSig_v_s);
  builder.addFunction("throw_catch_simd", kSig_i_v)
      .addLocals({s128_count: 1})
      .addBody([
        kExprTry, kWasmI32,
          kExprGetLocal, 0,
          kExprThrow, 0,
        kExprCatch, except,
        // TODO(mstarzinger): Actually return some compressed form of the s128
        // value here to make sure it is extracted properly from the exception.
          kExprDrop,
          kExprI32Const, 1,
        kExprEnd,
      ])
      .exportFunc();
  var instance = builder.instantiate();

  assertEquals(1, instance.exports.throw_catch_simd());
})();
