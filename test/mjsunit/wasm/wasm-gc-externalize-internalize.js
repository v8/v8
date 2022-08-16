// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc
"use strict";

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let instance = (() => {
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  /**
   * type_producer    -> create type and pass as anyref
   *                     (implicit externalize by calling convention)
   * type_externalize -> create type, externalize, pass by externref
   * type_consumer    -> consume type by anyref
   *                     (implicit internalize by calling convention)
   * type_internalize -> consume type by externref, internalize
   */

  builder.addFunction('struct_producer', makeSig([kWasmI32], [kWasmAnyRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct])
    .exportFunc();

  builder.addFunction('struct_externalize',
                      makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternExternalize,
    ])
    .exportFunc();

  builder.addFunction('struct_consumer',
                      makeSig([kWasmAnyRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 0,
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

  builder.addFunction('struct_internalize',
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

    builder.addFunction('i31_producer', makeSig([kWasmI32], [kWasmAnyRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprI31New])
    .exportFunc();

    builder.addFunction('i31_externalize',
                        makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprI31New,
      kGCPrefix, kExprExternExternalize,
    ])
    .exportFunc();

    builder.addFunction('i31_consumer',
                        makeSig([kWasmAnyRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 0,
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

    builder.addFunction('i31_internalize',
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

    builder.addFunction('array_producer', makeSig([kWasmI32], [kWasmAnyRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewFixedStatic, array, 1])
    .exportFunc();

    builder.addFunction('array_externalize',
                        makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewFixedStatic, array, 1,
      kGCPrefix, kExprExternExternalize,
    ])
    .exportFunc();

    builder.addFunction('array_consumer',
                        makeSig([kWasmAnyRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 0,
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

    builder.addFunction('array_internalize',
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

for (let type of ["struct", "i31", "array"]) {
  for (let consume of ["consumer", "internalize"]) {
    let fnConsume = instance.exports[`${type}_${consume}`];
    // A null is converted to (ref null none).
    assertEquals([0, 1], fnConsume(null));
    // Passing a JavaScript object is allowed and can be internalized
    // but will fail on casting it.
    assertTraps(kTrapIllegalCast, () => fnConsume({}));

    for (let produce of ["producer", "externalize"]) {
      let fnProduce = instance.exports[`${type}_${produce}`];
      // Test roundtrip of a value produced in Wasm passed back to Wasm.
      let obj42 = fnProduce(42);
      assertEquals([42, 0], fnConsume(obj42));
    }
  }
}
