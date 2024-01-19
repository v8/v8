// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-imported-strings

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let kRefExtern = wasmRefType(kWasmExternRef);
let kSig_e_v = makeSig([], [kRefExtern]);

// Part I: Test that the WebAssembly.String.* functions throw when called
// with arguments of incorrect types.

let length = 3;
let array_maker = (() => {
  let builder = new WasmModuleBuilder();
  builder.startRecGroup();
  let array_i16 = builder.addArray(kWasmI16, true, kNoSuperType, true);
  let array_i8 = builder.addArray(kWasmI8, true, kNoSuperType, true);
  builder.endRecGroup();

  let wtf16_data = builder.addPassiveDataSegment([97, 0, 98, 0, 99, 0]);
  let wtf8_data = builder.addPassiveDataSegment([97, 98, 99]);


  builder.addFunction(
      "make_i16_array", makeSig([], [wasmRefType(array_i16)]))
    .exportFunc()
    .addBody([
      kExprI32Const, 0, kExprI32Const, length,
      kGCPrefix, kExprArrayNewData, array_i16, wtf16_data
    ]).index;

  builder.addFunction(
      "make_i8_array", makeSig([], [wasmRefType(array_i8)]))
    .exportFunc()
    .addBody([
      kExprI32Const, 0, kExprI32Const, length,
      kGCPrefix, kExprArrayNewData, array_i8, wtf8_data
    ]).index;

  return builder.instantiate();
})();

let a16 = array_maker.exports.make_i16_array();
let a8 = array_maker.exports.make_i8_array();

assertThrows(() => WebAssembly.String.fromWtf16Array(a16, 0, length),
             WebAssembly.RuntimeError);
assertThrows(() => WebAssembly.String.fromUtf8Array(a8, 0, length),
             WebAssembly.RuntimeError);
assertThrows(() => WebAssembly.String.toWtf16Array("foo", a16, 0),
             WebAssembly.RuntimeError);

// Part II: Test that instantiating the module throws a LinkError when the
// string imports use incorrect types.

let array_i16;
let array_i8;

function MakeInvalidImporterBuilder() {
  let builder = new WasmModuleBuilder();
  builder.startRecGroup();
  array_i16 = builder.addArray(kWasmI16, true, kNoSuperType, true);
  array_i8 = builder.addArray(kWasmI8, true, kNoSuperType, true);
  builder.endRecGroup();
  return builder;
}

let b1 = MakeInvalidImporterBuilder();
let b2 = MakeInvalidImporterBuilder();
let b3 = MakeInvalidImporterBuilder();
let b4 = MakeInvalidImporterBuilder();

let array16ref = wasmRefNullType(array_i16);
let array8ref = wasmRefNullType(array_i8);

b1.addImport('String', 'fromWtf16Array',
             makeSig([array16ref, kWasmI32, kWasmI32], [kRefExtern]));
b2.addImport('String', 'fromUtf8Array',
             makeSig([array8ref, kWasmI32, kWasmI32], [kRefExtern]));
b3.addImport('String', 'toWtf16Array',
             makeSig([kWasmExternRef, array16ref, kWasmI32], [kWasmI32]));
// One random example of a non-array-related incorrect type (incorrect result).
b4.addImport('String', 'charCodeAt',
             makeSig([kWasmExternRef, kWasmI32], [kWasmI64]));

assertThrows(() => b1.instantiate({ String: WebAssembly.String }),
             WebAssembly.LinkError);
assertThrows(() => b2.instantiate({ String: WebAssembly.String }),
             WebAssembly.LinkError);
assertThrows(() => b3.instantiate({ String: WebAssembly.String }),
             WebAssembly.LinkError);
assertThrows(() => b4.instantiate({ String: WebAssembly.String }),
             WebAssembly.LinkError);
