// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --experimental-wasm-gc

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let array = builder.addArray(kWasmS128, true);
let empty_sig = builder.addType(makeSig([], []));
builder.addFunction(undefined, empty_sig)
  .addLocals(kWasmI32, 1).addLocals(wasmRefType(0), 18)
  .addBody([
    // The empty try will never jump to the catch making the catch block
    // unreachable already during graph construction time.
    kExprTry, kWasmVoid,
    kExprCatchAll,
      kExprI32Const, 0,
      kSimdPrefix, kExprI8x16Splat,
      kExprI32Const, 42,
      kGCPrefix, kExprArrayNew, array,  // array.new
      kExprDrop,
    kExprEnd,
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
print(instance.exports.main());
