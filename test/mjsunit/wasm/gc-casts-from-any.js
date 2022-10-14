// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --no-wasm-gc-structref-as-dataref

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestRefTest() {
  var builder = new WasmModuleBuilder();
  let structSuper = builder.addStruct([makeField(kWasmI32, true)]);
  let structSub = builder.addStruct([makeField(kWasmI32, true)], structSuper);
  let array = builder.addArray(kWasmI32);

  let fct =
  builder.addFunction('createStructSuper',
                      makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, structSuper,
      kGCPrefix, kExprExternExternalize,
    ]).exportFunc();
  builder.addFunction('createStructSub', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, structSub,
      kGCPrefix, kExprExternExternalize,
    ]).exportFunc();
  builder.addFunction('createArray', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewFixed, array, 1,
      kGCPrefix, kExprExternExternalize,
    ]).exportFunc();
  builder.addFunction('createFuncRef', makeSig([], [kWasmFuncRef]))
    .addBody([
      kExprRefFunc, fct.index,
    ]).exportFunc();

  [
    ["StructSuper", structSuper],
    ["StructSub", structSub],
    ["Array", array],
    ["I31", kI31RefCode],
    ["AnyArray", kArrayRefCode],
    ["Struct", kStructRefCode],
    ["Eq", kEqRefCode],
    // 'ref.test any' is semantically the same as '!ref.is_null' here.
    ["Any", kAnyRefCode],
  ].forEach(([typeName, typeCode]) => {
    builder.addFunction(`refTest${typeName}`,
                        makeSig([kWasmExternRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefTest, typeCode,
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefTestNull, typeCode,
    ]).exportFunc();

    builder.addFunction(`refCast${typeName}`,
                        makeSig([kWasmExternRef], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCast, typeCode,
      kGCPrefix, kExprExternExternalize,
    ]).exportFunc();
  });

  var instance = builder.instantiate();
  let wasm = instance.exports;
  // result: [ref.test, ref.test null]
  assertEquals([0, 1], wasm.refTestStructSuper(null));
  assertEquals([0, 0], wasm.refTestStructSuper(undefined));
  assertEquals([1, 1], wasm.refTestStructSuper(wasm.createStructSuper()));
  assertEquals([1, 1], wasm.refTestStructSuper(wasm.createStructSub()));
  assertEquals([0, 0], wasm.refTestStructSuper(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestStructSuper(wasm.createFuncRef()));
  assertEquals([0, 0], wasm.refTestStructSuper(1));
  assertEquals([0, 0], wasm.refTestStructSuper({'JavaScript': 'Object'}));

  assertEquals([0, 1], wasm.refTestStructSub(null));
  assertEquals([0, 0], wasm.refTestStructSub(undefined));
  assertEquals([0, 0], wasm.refTestStructSub(wasm.createStructSuper()));
  assertEquals([1, 1], wasm.refTestStructSub(wasm.createStructSub()));
  assertEquals([0, 0], wasm.refTestStructSub(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestStructSub(wasm.createFuncRef()));
  assertEquals([0, 0], wasm.refTestStructSub(1));
  assertEquals([0, 0], wasm.refTestStructSub({'JavaScript': 'Object'}));

  assertEquals([0, 1], wasm.refTestArray(null));
  assertEquals([0, 0], wasm.refTestArray(undefined));
  assertEquals([0, 0], wasm.refTestArray(wasm.createStructSuper()));
  assertEquals([0, 0], wasm.refTestArray(wasm.createStructSub()));
  assertEquals([1, 1], wasm.refTestArray(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestArray(wasm.createFuncRef()));
  assertEquals([0, 0], wasm.refTestArray(1));
  assertEquals([0, 0], wasm.refTestArray({'JavaScript': 'Object'}));

  assertEquals([0, 1], wasm.refTestI31(null));
  assertEquals([0, 0], wasm.refTestI31(undefined));
  assertEquals([0, 0], wasm.refTestI31(wasm.createStructSuper()));
  assertEquals([0, 0], wasm.refTestI31(wasm.createStructSub()));
  assertEquals([0, 0], wasm.refTestI31(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestI31(wasm.createFuncRef()));
  assertEquals([1, 1], wasm.refTestI31(1));
  assertEquals([0, 0], wasm.refTestI31({'JavaScript': 'Object'}));

  assertEquals([0, 1], wasm.refTestAnyArray(null));
  assertEquals([0, 0], wasm.refTestAnyArray(undefined));
  assertEquals([0, 0], wasm.refTestAnyArray(wasm.createStructSuper()));
  assertEquals([0, 0], wasm.refTestAnyArray(wasm.createStructSub()));
  assertEquals([1, 1], wasm.refTestAnyArray(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestAnyArray(wasm.createFuncRef()));
  assertEquals([0, 0], wasm.refTestAnyArray(1));
  assertEquals([0, 0], wasm.refTestAnyArray({'JavaScript': 'Object'}));

  assertEquals([0, 1], wasm.refTestStruct(null));
  assertEquals([0, 0], wasm.refTestStruct(undefined));
  assertEquals([1, 1], wasm.refTestStruct(wasm.createStructSuper()));
  assertEquals([1, 1], wasm.refTestStruct(wasm.createStructSub()));
  assertEquals([0, 0], wasm.refTestStruct(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestStruct(wasm.createFuncRef()));
  assertEquals([0, 0], wasm.refTestStruct(1));
  assertEquals([0, 0], wasm.refTestStruct({'JavaScript': 'Object'}));

  assertEquals([0, 1], wasm.refTestEq(null));
  assertEquals([0, 0], wasm.refTestEq(undefined));
  assertEquals([1, 1], wasm.refTestEq(wasm.createStructSuper()));
  assertEquals([1, 1], wasm.refTestEq(wasm.createStructSub()));
  assertEquals([1, 1], wasm.refTestEq(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestEq(wasm.createFuncRef()));
  assertEquals([1, 1], wasm.refTestEq(1)); // ref.i31
  assertEquals([0, 0], wasm.refTestEq({'JavaScript': 'Object'}));

  assertEquals([0, 1], wasm.refTestAny(null));
  assertEquals([1, 1], wasm.refTestAny(undefined));
  assertEquals([1, 1], wasm.refTestAny(wasm.createStructSuper()));
  assertEquals([1, 1], wasm.refTestAny(wasm.createStructSub()));
  assertEquals([1, 1], wasm.refTestAny(wasm.createArray()));
  assertEquals([1, 1], wasm.refTestAny(wasm.createFuncRef()));
  assertEquals([1, 1], wasm.refTestAny(1)); // ref.i31
  assertEquals([1, 1], wasm.refTestAny({'JavaScript': 'Object'}));

  // ref.cast
  let structSuperObj = wasm.createStructSuper();
  let structSubObj = wasm.createStructSub();
  let arrayObj = wasm.createArray();
  let jsObj = {'JavaScript': 'Object'};
  let funcObj = wasm.createFuncRef();

  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(undefined));
  assertSame(structSuperObj, wasm.refCastStructSuper(structSuperObj));
  assertSame(structSubObj, wasm.refCastStructSuper(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(jsObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(structSuperObj));
  assertSame(structSubObj, wasm.refCastStructSub(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(jsObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(structSubObj));
  assertSame(arrayObj, wasm.refCastArray(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(jsObj));


  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(funcObj));
  assertEquals(1, wasm.refCastI31(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(jsObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(structSubObj));
  assertSame(arrayObj, wasm.refCastAnyArray(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(jsObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(undefined));
  assertSame(structSuperObj, wasm.refCastStruct(structSuperObj));
  assertSame(structSubObj, wasm.refCastStruct(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(jsObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastEq(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastEq(undefined));
  assertSame(structSuperObj, wasm.refCastEq(structSuperObj));
  assertSame(structSubObj, wasm.refCastEq(structSubObj));
  assertSame(arrayObj, wasm.refCastEq(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastEq(funcObj));
  assertEquals(1, wasm.refCastEq(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastEq(jsObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastAny(null));
  assertSame(undefined, wasm.refCastAny(undefined));
  assertSame(structSuperObj, wasm.refCastAny(structSuperObj));
  assertSame(structSubObj, wasm.refCastAny(structSubObj));
  assertSame(arrayObj, wasm.refCastAny(arrayObj));
  assertSame(funcObj, wasm.refCastAny(funcObj));
  assertEquals(1, wasm.refCastAny(1));
  assertSame(jsObj, wasm.refCastAny(jsObj));
})();
