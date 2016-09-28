// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --wasm-eh-prototype

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

var module = (function () {
  var builder = new WasmModuleBuilder();

  builder.addFunction("throw_param_if_not_zero", kSig_i_i)
    .addBody([
      kExprGetLocal, 0,
      kExprI32Const, 0,
      kExprI32Ne,
      kExprIf, kAstStmt,
      kExprGetLocal, 0,
      kExprThrow, kAstStmt,
      kExprEnd,
      kExprI32Const, 1
    ])
    .exportFunc()

  builder.addFunction("throw_20", kSig_v_v)
    .addBody([
      kExprI32Const, 20,
      kExprThrow, kAstStmt
    ])
    .exportFunc()

  builder.addFunction("throw_expr_with_params", kSig_v_ddi)
    .addBody([
      // p2 * (p0 + min(p0, p1))|0 - 20
      kExprGetLocal, 2,
      kExprGetLocal, 0,
      kExprGetLocal, 0,
      kExprGetLocal, 1,
      kExprF64Min,
      kExprF64Add,
      kExprI32SConvertF64,
      kExprI32Mul,
      kExprI32Const, 20,
      kExprI32Sub,
      kExprThrow, kAstStmt
    ])
    .exportFunc()

  return builder.instantiate();
})();

// Check the module exists.
assertFalse(module === undefined);
assertFalse(module === null);
assertFalse(module === 0);
assertEquals("object", typeof module.exports);
assertEquals("function", typeof module.exports.throw_param_if_not_zero);

assertEquals(1, module.exports.throw_param_if_not_zero(0));
assertWasmThrows(10, function() { module.exports.throw_param_if_not_zero(10) });
assertWasmThrows(-1, function() { module.exports.throw_param_if_not_zero(-1) });
assertWasmThrows(20, module.exports.throw_20);
assertWasmThrows(
    -8, function() { module.exports.throw_expr_with_params(1.5, 2.5, 4); });
assertWasmThrows(
    12, function() { module.exports.throw_expr_with_params(5.7, 2.5, 4); });
