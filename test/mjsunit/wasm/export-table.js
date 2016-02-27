// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

load("test/mjsunit/wasm/wasm-constants.js");

(function testExportedMain() {
  var kBodySize = 3;
  var kReturnValue = 99;
  var kNameMainOffset = 4 + 7 + kBodySize + 8 + 1;

  var data = bytes(
    // signatures
    kDeclSignatures, 1,
    0, kAstI32,                  // void -> i32
    // -- main function
    kDeclFunctions,
    1,
    0,                           // decl flags
    0, 0,                        // signature index
    kBodySize, 0,
    // main body
    kExprReturn,
    kExprI8Const,
    kReturnValue,
    // exports
    kDeclExportTable,
    1,
    0, 0,                       // func index index
    kNameMainOffset, 0, 0, 0,   // function name offset
    // names
    kDeclEnd,
    'm', 'a', 'i', 'n', 0       // --
  );

  var ffi = new Object();
  var module = _WASMEXP_.instantiateModule(data, ffi);

  assertEquals("object", typeof module.exports);
  assertEquals("function", typeof module.exports.main);

  assertEquals(kReturnValue, module.exports.main());
})();

(function testExportedTwice() {
  var kBodySize = 3;
  var kReturnValue = 99;
  var kNameMainOffset = 4 + 7 + kBodySize + 14 + 1;
  var kNameFooOffset = kNameMainOffset + 5;

  var data = bytes(
    // signatures
    kDeclSignatures, 1,
    0, kAstI32,                  // void -> i32
    // -- main function
    kDeclFunctions,
    1,
    0,                           // decl flags
    0, 0,                        // signature index
    kBodySize, 0,
    // main body
    kExprReturn,
    kExprI8Const,
    kReturnValue,
    // exports
    kDeclExportTable,
    2,
    0, 0,                       // func index index
    kNameMainOffset, 0, 0, 0,   // function name offset
    0, 0,                       // func index index
    kNameFooOffset, 0, 0, 0,    // function name offset
    // names
    kDeclEnd,
    'b', 'l', 'a', 'h', 0,       // --
    'f', 'o', 'o', 0             // --
  );

  var ffi = new Object();
  var module = _WASMEXP_.instantiateModule(data, ffi);

  assertEquals("object", typeof module.exports);
  assertEquals("function", typeof module.exports.blah);
  assertEquals("function", typeof module.exports.foo);

  assertEquals(kReturnValue, module.exports.blah());
  assertEquals(kReturnValue, module.exports.foo());
})();
