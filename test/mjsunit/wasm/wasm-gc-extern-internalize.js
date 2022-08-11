// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc
"use strict";

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let instance = (() => {
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('struct_producer', makeSig([kWasmI32], [kWasmDataRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct])
    .exportFunc();

  builder.addFunction('struct_consumer',
                      makeSig([kWasmExternRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 0,
        kGCPrefix, kExprExternInternalize,
        kExprBrOnNull, 0,
        kGCPrefix, kExprRefAsData,
        kGCPrefix, kExprRefCastStatic, struct,
        kGCPrefix, kExprStructGet, struct, 0, // value
        kExprI32Const, 0, // isNull
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 0, // value (placeholder)
      kExprI32Const, 1, // isNull
    ])
    .exportFunc();

    builder.addFunction('i31_producer', makeSig([kWasmI32], [kWasmI31Ref]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprI31New])
    .exportFunc();

    builder.addFunction('i31_consumer',
                        makeSig([kWasmExternRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 0,
        kGCPrefix, kExprExternInternalize,
        kExprBrOnNull, 0,
        kGCPrefix, kExprRefAsI31,
        kGCPrefix, kExprI31GetS, // value
        kExprI32Const, 0, // isNull
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 0, // value (placeholder)
      kExprI32Const, 1, // isNull
    ])
    .exportFunc();

    let array = builder.addArray(kWasmI32, true);

    builder.addFunction('array_producer', makeSig([kWasmI32], [kWasmArrayRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewFixedStatic, array, 1])
    .exportFunc();

    builder.addFunction('array_consumer',
                        makeSig([kWasmExternRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 0,
        kGCPrefix, kExprExternInternalize,
        kExprBrOnNull, 0,
        kGCPrefix, kExprRefAsArray,
        kGCPrefix, kExprRefCastStatic, array,
        kExprI32Const, 0,
        kGCPrefix, kExprArrayGet, array, // value
        kExprI32Const, 0, // isNull
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 0, // value (placeholder)
      kExprI32Const, 1, // isNull
    ])
    .exportFunc();

  return builder.instantiate({});
})();

// struct
["struct", "i31", "array"].forEach((type) => {
  var fnConsumer = instance.exports[`${type}_consumer`];
  var fnProducer = instance.exports[`${type}_producer`];
  assertEquals([0, 1], fnConsumer(null));
  var obj42 = fnProducer(42);
  assertEquals([42, 0], fnConsumer(obj42));
  assertTraps(kTrapIllegalCast, () => fnConsumer({}));
});
