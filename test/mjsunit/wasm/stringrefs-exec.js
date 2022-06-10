// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-stringref

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let kSig_w_ii = makeSig([kWasmI32, kWasmI32], [kWasmStringRef]);
let kSig_w_v = makeSig([], [kWasmStringRef]);
let kSig_i_w = makeSig([kWasmStringRef], [kWasmI32]);
let kSig_i_wi = makeSig([kWasmStringRef, kWasmI32], [kWasmI32]);

function encodeWtf8(str) {
  // String iterator coalesces surrogate pairs.
  let out = [];
  for (let codepoint of str) {
    codepoint = codepoint.codePointAt(0);
    if (codepoint <= 0x7f) {
      out.push(codepoint);
    } else if (codepoint <= 0x7ff) {
      out.push(0xc0 | (codepoint >> 6));
      out.push(0x80 | (codepoint & 0x3f));
    } else if (codepoint <= 0xffff) {
      out.push(0xe0 | (codepoint >> 12));
      out.push(0x80 | ((codepoint >> 6) & 0x3f));
      out.push(0x80 | (codepoint & 0x3f));
    } else if (codepoint <= 0x10ffff) {
      out.push(0xf0 | (codepoint >> 18));
      out.push(0x80 | ((codepoint >> 12) & 0x3f));
      out.push(0x80 | ((codepoint >> 6) & 0x3f));
      out.push(0x80 | (codepoint & 0x3f));
    } else {
      throw new Error("bad codepoint " + codepoint);
    }
  }
  return out;
}

let interestingStrings = ['',
                          'ascii',
                          'latin \xa9 1',
                          'two \ucccc byte',
                          'surrogate \ud800\udc000 pair',
                          'isolated \ud800 leading',
                          'isolated \udc00 trailing'];

function makeWtf8TestDataSegment() {
  let data = []
  let valid = {};
  let invalid = {};

  for (let str of interestingStrings) {
    let bytes = encodeWtf8(str);
    valid[str] = { offset: data.length, length: bytes.length };
    for (let byte of bytes) {
      data.push(byte);
    }
  }
  for (let bytes of ['trailing high byte \xa9',
                     'interstitial high \xa9 byte',
                     'invalid \xc0 byte',
                     'surrogate \xed\xa0\x80\xed\xd0\x80 pair']) {
    invalid[bytes] = { offset: data.length, length: bytes.length };
    for (let i = 0; i < bytes.length; i++) {
      data.push(bytes.charCodeAt(i));
    }
  }

  return { valid, invalid, data: Uint8Array.from(data) };
};

(function TestStringNewWtf8() {
  let builder = new WasmModuleBuilder();

  builder.addMemory(1, undefined, false, false);
  let data = makeWtf8TestDataSegment();
  builder.addDataSegment(0, data.data);

  builder.addFunction("string_new_wtf8", kSig_w_ii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringNewWtf8, 0
    ]);

  let instance = builder.instantiate();
  for (let [str, {offset, length}] of Object.entries(data.valid)) {
    assertEquals(str, instance.exports.string_new_wtf8(offset, length));
  }
  for (let [str, {offset, length}] of Object.entries(data.invalid)) {
    assertThrows(() => instance.exports.string_new_wtf8(offset, length),
                 WebAssembly.RuntimeError, "invalid WTF-8 string");
  }
})();

function encodeWtf16LE(str) {
  // String iterator coalesces surrogate pairs.
  let out = [];
  for (let i = 0; i < str.length; i++) {
    codeunit = str.charCodeAt(i);
    out.push(codeunit & 0xff)
    out.push(codeunit >> 8);
  }
  return out;
}

function makeWtf16TestDataSegment() {
  let data = []
  let valid = {};

  for (let str of interestingStrings) {
    valid[str] = { offset: data.length, length: str.length };
    for (let byte of encodeWtf16LE(str)) {
      data.push(byte);
    }
  }

  return { valid, data: Uint8Array.from(data) };
};

(function TestStringNewWtf16() {
  let builder = new WasmModuleBuilder();

  builder.addMemory(1, undefined, false, false);
  let data = makeWtf16TestDataSegment();
  builder.addDataSegment(0, data.data);

  builder.addFunction("string_new_wtf16", kSig_w_ii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringNewWtf16, 0
    ]);

  let instance = builder.instantiate();
  for (let [str, {offset, length}] of Object.entries(data.valid)) {
    assertEquals(str, instance.exports.string_new_wtf16(offset, length));
  }
})();

(function TestStringConst() {
  let builder = new WasmModuleBuilder();
  for (let [index, str] of interestingStrings.entries()) {
    builder.addLiteralStringRef(encodeWtf8(str));

    builder.addFunction("string_const" + index, kSig_w_v)
      .exportFunc()
      .addBody([
        kGCPrefix, kExprStringConst, index
      ]);

    builder.addGlobal(kWasmStringRef, false, WasmInitExpr.StringConst(index))
      .exportAs("global" + index);
  }

  let instance = builder.instantiate();
  for (let [index, str] of interestingStrings.entries()) {
    assertEquals(str, instance.exports["string_const" + index]());
    assertEquals(str, instance.exports["global" + index].value);
  }
})();

(function TestStringMeasureUtf8AndWtf8() {
  let builder = new WasmModuleBuilder();

  builder.addFunction("string_measure_utf8", kSig_i_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringMeasureUtf8
    ]);

  builder.addFunction("string_measure_wtf8", kSig_i_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringMeasureWtf8
    ]);

  builder.addFunction("string_measure_utf8_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kStringRefCode,
      kGCPrefix, kExprStringMeasureUtf8
    ]);

  builder.addFunction("string_measure_wtf8_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kStringRefCode,
      kGCPrefix, kExprStringMeasureWtf8
    ]);

  function HasIsolatedSurrogate(str) {
    for (let codepoint of str) {
      let value = codepoint.codePointAt(0);
      if (0xD800 <= value && value <= 0xDFFF) return true;
    }
    return false;
  }

  let instance = builder.instantiate();
  for (let str of interestingStrings) {
    let wtf8 = encodeWtf8(str);
    assertEquals(wtf8.length, instance.exports.string_measure_wtf8(str));
    if (HasIsolatedSurrogate(str)) {
      assertEquals(-1, instance.exports.string_measure_utf8(str));
    } else {
      assertEquals(wtf8.length, instance.exports.string_measure_utf8(str));
    }
  }

  assertThrows(() => instance.exports.string_measure_utf8_null(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.string_measure_wtf8_null(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
})();

(function TestStringMeasureWtf16() {
  let builder = new WasmModuleBuilder();

  builder.addFunction("string_measure_wtf16", kSig_i_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringMeasureWtf16
    ]);

  builder.addFunction("string_measure_wtf16_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kStringRefCode,
      kGCPrefix, kExprStringMeasureWtf16
    ]);

  let instance = builder.instantiate();
  for (let str of interestingStrings) {
    assertEquals(str.length, instance.exports.string_measure_wtf16(str));
  }

  assertThrows(() => instance.exports.string_measure_wtf16_null(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
})();

(function TestStringViewWtf16() {
  let builder = new WasmModuleBuilder();

  builder.addFunction("string_view_wtf16_length", kSig_i_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringAsWtf16,
      kGCPrefix, kExprStringViewWtf16Length
    ]);

  builder.addFunction("string_view_wtf16_length_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kStringViewWtf16Code,
      kGCPrefix, kExprStringViewWtf16Length
    ]);

  builder.addFunction("string_view_wtf16_get_codeunit", kSig_i_wi)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringAsWtf16,
      kExprLocalGet, 1,
      kGCPrefix, kExprStringViewWtf16GetCodeunit
    ]);

  builder.addFunction("string_view_wtf16_get_codeunit_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kStringViewWtf16Code,
      kExprI32Const, 0,
      kGCPrefix, kExprStringViewWtf16GetCodeunit
    ]);

  let instance = builder.instantiate();
  for (let str of interestingStrings) {
    assertEquals(str.length, instance.exports.string_view_wtf16_length(str));
    for (let i = 0; i < str.length; i++) {
      assertEquals(str.charCodeAt(i),
                   instance.exports.string_view_wtf16_get_codeunit(str, i));
    }
  }

  assertThrows(() => instance.exports.string_view_wtf16_length_null(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.string_view_wtf16_get_codeunit_null(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.string_view_wtf16_get_codeunit("", 0),
               WebAssembly.RuntimeError, "string offset out of bounds");
})();
