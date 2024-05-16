// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged
// Flags: --wasm-inlining-ignore-call-counts

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeoptInlined() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction("mul", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
    .exportFunc();

  // TODO(mliedtke): This should all break with parameter stack slots as
  // currently both the outer frame state as well as the inner frame state push
  // them onto the stack (meaning they are there twice).
  let mainSig = makeSig([kWasmI32, kWasmI32, wasmRefType(funcRefT)], [kWasmI32]);
  let inlinee = builder.addFunction("inlinee", mainSig)
    .addBody([
      kExprLocalGet, 1,
      kExprI32Const, 1,
      kExprI32Add,
      kExprLocalTee, 1,
      kExprLocalGet, 0,
      kExprI32Const, 1,
      kExprI32Add,
      kExprLocalTee, 0,
      kExprLocalGet, 2,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  builder.addFunction("main", mainSig)
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLocalGet, 0,
      kExprI32Const, 1,
      kExprI32Add,
      kExprLocalGet, 1,
      kExprI32Const, 1,
      kExprI32Add,
      kExprLocalGet, 2,
      kExprCallFunction, inlinee.index,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(46, wasm.main(12, 30, wasm.add));
  // Tier up.
  %WasmTierUpFunction(wasm.main);
  assertEquals(46, wasm.main(12, 30, wasm.add));
  assertTrue(%IsTurboFanFunction(wasm.main));
  // Cause deopt.
  assertEquals(14 * 32, wasm.main(12, 30, wasm.mul));
  // Deopt happened.
  assertFalse(%IsTurboFanFunction(wasm.main));
  assertEquals(46, wasm.main(12, 30, wasm.add));
  // Trigger re-opt.
  %WasmTierUpFunction(wasm.main);
  // Both call targets are used in the re-optimized function, so they don't
  // trigger new deopts.
  assertEquals(46, wasm.main(12, 30, wasm.add));
  assertTrue(%IsTurboFanFunction(wasm.main));
  assertEquals(14 * 32, wasm.main(12, 30, wasm.mul));
  assertTrue(%IsTurboFanFunction(wasm.main));
})();
