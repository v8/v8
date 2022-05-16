// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --experimental-wasm-stringref

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function assertValid(fn) {
  let builder = new WasmModuleBuilder();
  fn(builder);
  // If an assertValid() ever fails unexpectedly, uncomment this line to
  // get a more precise error:
  // builder.toModule();
  assertTrue(WebAssembly.validate(builder.toBuffer()));
}
function assertInvalid(fn, message) {
  let builder = new WasmModuleBuilder();
  fn(builder);
  assertThrows(() => builder.toModule(), WebAssembly.CompileError,
               `WebAssembly.Module(): ${message}`);
}

// TODO(wingo): Enable when we start parsing string literal sections.
// assertValid(builder => builder.addLiteralStringRef("foo"));

for (let [name, code] of [['string', kWasmStringRef],
                          ['stringview_wtf8', kWasmStringViewWtf8],
                          ['stringview_wtf16', kWasmStringViewWtf16],
                          ['stringview_iter', kWasmStringViewIter]]) {
  let default_init = WasmInitExpr.RefNull(code);

  assertValid(b => b.addType(makeSig([code], [])));
  assertValid(b => b.addStruct([makeField(code, true)]));
  assertValid(b => b.addArray(code, true));
  assertValid(b => b.addType(makeSig([], [code])));
  assertValid(b => b.addGlobal(code, true, default_init));
  // TODO(wingo): Table of strings not yet implemented.
  // assertValid(b => b.addTable(code, 0));
  assertValid(b => b.addPassiveElementSegment([default_init], code));
  assertValid(b => b.addTag(makeSig([code], [])));
  assertValid(
    b => b.addFunction(undefined, kSig_v_v).addLocals(code, 1).addBody([]));
}

let kSig_w_ii = makeSig([kWasmI32, kWasmI32], [kWasmStringRef]);
let kSig_w_v = makeSig([], [kWasmStringRef]);
let kSig_i_w = makeSig([kWasmStringRef], [kWasmI32]);
let kSig_v_wi = makeSig([kWasmStringRef, kWasmI32], []);
let kSig_w_ww = makeSig([kWasmStringRef, kWasmStringRef], [kWasmStringRef]);
let kSig_i_ww = makeSig([kWasmStringRef, kWasmStringRef], [kWasmI32]);
let kSig_x_w = makeSig([kWasmStringRef], [kWasmStringViewWtf8]);
let kSig_i_xii = makeSig([kWasmStringViewWtf8, kWasmI32, kWasmI32],
                         [kWasmI32]);
let kSig_ii_xiii = makeSig([kWasmStringViewWtf8, kWasmI32, kWasmI32,
                            kWasmI32],
                           [kWasmI32, kWasmI32]);
let kSig_w_xii = makeSig([kWasmStringViewWtf8, kWasmI32, kWasmI32],
                         [kWasmStringRef]);
let kSig_y_w = makeSig([kWasmStringRef], [kWasmStringViewWtf16]);
let kSig_i_y = makeSig([kWasmStringViewWtf16], [kWasmI32]);
let kSig_i_yi = makeSig([kWasmStringViewWtf16, kWasmI32], [kWasmI32]);
let kSig_v_yiii = makeSig([kWasmStringViewWtf16, kWasmI32, kWasmI32,
                           kWasmI32], []);
let kSig_w_yii = makeSig([kWasmStringViewWtf16, kWasmI32, kWasmI32],
                         [kWasmStringRef]);
let kSig_z_w = makeSig([kWasmStringRef], [kWasmStringViewIter]);
let kSig_i_z = makeSig([kWasmStringViewIter], [kWasmI32]);
let kSig_i_zi = makeSig([kWasmStringViewIter, kWasmI32], [kWasmI32]);
let kSig_w_zi = makeSig([kWasmStringViewIter, kWasmI32],
                        [kWasmStringRef]);

(function TestInstructions() {
  let builder = new WasmModuleBuilder();

  builder.addMemory(0, undefined, false, false);

  builder.addFunction("string.new_wtf8", kSig_w_ii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringNewWtf8, 0
    ]);

  builder.addFunction("string.new_wtf16", kSig_w_ii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringNewWtf16, 0
    ]);

  // TODO(wingo): Enable when we start parting string literal sections.
  // builder.addFunction("string.const", kSig_w_v)
  //   .addLiteralStringRef("foo")
  //   .addBody([
  //     kGCPrefix, kExprStringConst, 0
  //   ]);

  builder.addFunction("string.measure_utf8", kSig_i_w)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringMeasureUtf8
    ]);

  builder.addFunction("string.measure_wtf8", kSig_i_w)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringMeasureWtf8
    ]);

  builder.addFunction("string.measure_wtf16", kSig_i_w)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringMeasureWtf16
    ]);

  builder.addFunction("string.encode_wtf8/utf-8", kSig_v_wi)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringEncodeWtf8, 0, 0
    ]);
  builder.addFunction("string.encode_wtf8/wtf-8", kSig_v_wi)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringEncodeWtf8, 0, 1
    ]);
  builder.addFunction("string.encode_wtf8/replace", kSig_v_wi)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringEncodeWtf8, 0, 2
    ]);

  builder.addFunction("string.encode_wtf16", kSig_v_wi)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringEncodeWtf16, 0
    ]);

  builder.addFunction("string.concat", kSig_w_ww)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringConcat
    ]);

  builder.addFunction("string.eq", kSig_i_ww)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringEq
    ]);

  builder.addFunction("string.as_wtf8", kSig_x_w)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringAsWtf8
    ]);

  builder.addFunction("stringview_wtf8.advance", kSig_i_xii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
      kGCPrefix, kExprStringViewWtf8Advance
    ]);

  builder.addFunction("stringview_wtf8.encode/utf-8", kSig_ii_xiii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2, kExprLocalGet, 3,
      kGCPrefix, kExprStringViewWtf8Encode, 0, 0
    ]);

  builder.addFunction("stringview_wtf8.encode/wtf-8", kSig_ii_xiii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2, kExprLocalGet, 3,
      kGCPrefix, kExprStringViewWtf8Encode, 0, 1
    ]);

  builder.addFunction("stringview_wtf8.encode/replace", kSig_ii_xiii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2, kExprLocalGet, 3,
      kGCPrefix, kExprStringViewWtf8Encode, 0, 2
    ]);

  builder.addFunction("stringview_wtf8.slice", kSig_w_xii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
      kGCPrefix, kExprStringViewWtf8Slice
    ]);

  builder.addFunction("string.as_wtf16", kSig_y_w)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringAsWtf16
    ]);

  builder.addFunction("stringview_wtf16.length", kSig_i_y)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringViewWtf16Length
    ]);

  builder.addFunction("stringview_wtf16.get_codeunit", kSig_i_yi)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringViewWtf16GetCodeunit
    ]);

  builder.addFunction("stringview_wtf16.encode", kSig_v_yiii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2, kExprLocalGet, 3,
      kGCPrefix, kExprStringViewWtf16Encode, 0
    ]);

  builder.addFunction("stringview_wtf16.slice", kSig_w_yii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
      kGCPrefix, kExprStringViewWtf16Slice
    ]);

  builder.addFunction("string.as_iter", kSig_z_w)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringAsIter
    ]);

  builder.addFunction("stringview_iter.cur", kSig_i_z)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringViewIterCur
    ]);

  builder.addFunction("stringview_iter.advance", kSig_i_zi)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringViewIterAdvance
    ]);

  builder.addFunction("stringview_iter.rewind", kSig_i_zi)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringViewIterRewind
    ]);

  builder.addFunction("stringview_iter.slice", kSig_w_zi)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringViewIterSlice
    ]);

  assertTrue(WebAssembly.validate(builder.toBuffer()));
})();

assertInvalid(
  builder => {
    builder.addFunction("string.new_wtf8/no-mem", kSig_w_ii)
      .addBody([
        kExprLocalGet, 0, kExprLocalGet, 1,
        kGCPrefix, kExprStringNewWtf8, 0
      ]);
  },
  "Compiling function #0:\"string.new_wtf8/no-mem\" failed: " +
    "memory instruction with no memory @+32");

assertInvalid(
  builder => {
    builder.addMemory(0, undefined, false, false);
    builder.addFunction("string.new_wtf8/bad-mem", kSig_w_ii)
      .addBody([
        kExprLocalGet, 0, kExprLocalGet, 1,
        kGCPrefix, kExprStringNewWtf8, 1
      ]);
  },
  "Compiling function #0:\"string.new_wtf8/bad-mem\" failed: " +
    "expected memory index 0, found 1 @+37");

assertInvalid(
  builder => {
    builder.addFunction("string.encode_wtf8/no-mem", kSig_v_wi)
      .addBody([
        kExprLocalGet, 0, kExprLocalGet, 1,
        kGCPrefix, kExprStringEncodeWtf8, 0, 0
      ]);
  },
  "Compiling function #0:\"string.encode_wtf8/no-mem\" failed: " +
    "memory instruction with no memory @+31");

assertInvalid(
  builder => {
    builder.addMemory(0, undefined, false, false);
    builder.addFunction("string.encode_wtf8/bad-mem", kSig_v_wi)
      .addBody([
        kExprLocalGet, 0, kExprLocalGet, 1,
        kGCPrefix, kExprStringEncodeWtf8, 1, 0
      ]);
  },
  "Compiling function #0:\"string.encode_wtf8/bad-mem\" failed: " +
    "expected memory index 0, found 1 @+36");

assertInvalid(
  builder => {
    builder.addMemory(0, undefined, false, false);
    builder.addFunction("string.encode_wtf8/bad-policy", kSig_v_wi)
      .addBody([
        kExprLocalGet, 0, kExprLocalGet, 1,
        kGCPrefix, kExprStringEncodeWtf8, 0, 3
      ]);
  },
  "Compiling function #0:\"string.encode_wtf8/bad-policy\" failed: " +
    "expected wtf8 policy 0, 1, or 2, but found 3 @+37");
