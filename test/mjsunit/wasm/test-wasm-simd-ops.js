// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --wasm-simd-prototype --wasm-num-compilation-tasks=0

load('test/mjsunit/wasm/wasm-constants.js');
load('test/mjsunit/wasm/wasm-module-builder.js');

// TODO(gdeepti): Currently wasm simd ops are tested using runtime calls to
// JS runtime functions, as this needs heap allocation sequential compile is
// triggered here. This is only used for bootstrapping and needs to be removed
// when we have architectuaral/scalar implementations for wasm simd ops.

(function SimdSplatExtractTest() {
  var module = new WasmModuleBuilder();
  module.addFunction("splatextract", kSig_i_ii)
      .addBody([kExprGetLocal, 0, kExprSimdPrefix, kExprI32x4Splat,
          kExprI32Const, 1, kExprSimdPrefix, kExprI32x4ExtractLane])
      .exportAs("main");
  var instance = module.instantiate();
  assertEquals(123, instance.exports.main(123, 2));
})();
