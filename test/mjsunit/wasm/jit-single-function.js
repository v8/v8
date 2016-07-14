// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm
// Flags: --wasm-jit-prototype

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

var module = (function () {
  var builder = new WasmModuleBuilder();

  var kSig_i_iiiii =
      makeSig([kAstI32, kAstI32, kAstI32, kAstI32, kAstI32], [kAstI32]);

  var sig_index2 = builder.addType(kSig_i_ii);
  var sig_index5 = builder.addType(kSig_i_iiiii);

  builder.addMemory(1, 1, true);
  var wasm_bytes_sub = [
      0,
      kExprGetLocal, 0,
      kExprGetLocal, 1,
      kExprI32Sub
  ];
  builder.addDataSegment(0, wasm_bytes_sub, false);

  var wasm_bytes_mul = [
      0,
      kExprGetLocal, 0,
      kExprGetLocal, 1,
      kExprI32Mul
  ];
  builder.addDataSegment(6, wasm_bytes_mul, false);

  builder.addPadFunctionTable(10);
  builder.addImport("add", sig_index2);
  builder.addFunction("add", sig_index2)
    .addBody([
      kExprGetLocal, 0, kExprGetLocal, 1, kExprCallImport, kArity2, 0
    ]);
  builder.addFunction("main", sig_index5)
    .addBody([
      kExprGetLocal, 0,
      kExprGetLocal, 1,
      kExprGetLocal, 2,
      kExprJITSingleFunction, sig_index2,
      kExprGetLocal, 2,
      kExprGetLocal, 3,
      kExprGetLocal, 4,
      kExprCallIndirect, kArity2, sig_index2
    ])
    .exportFunc()
  builder.appendToTable([0, 1]);

  return builder.instantiate({add: function(a, b) { return a + b | 0; }});
})();

// Check that the module exists
assertFalse(module === undefined);
assertFalse(module === null);
assertFalse(module === 0);
assertEquals("object", typeof module.exports);
assertEquals("function", typeof module.exports.main);

// Check that the bytes referred to lie in the bounds of the memory buffer
assertTraps(kTrapMemOutOfBounds, "module.exports.main(0, 100000, 3, 55, 99)");
assertTraps(kTrapMemOutOfBounds, "module.exports.main(65536, 1, 3, 55, 99)");

// Check that the index lies in the bounds of the table size
assertTraps(kTrapInvalidIndex, "module.exports.main(0, 6, 10, 55, 99)");
assertTraps(kTrapInvalidIndex, "module.exports.main(0, 6, -1, 55, 99)");

// args: base offset, size of func_bytes, index, param1, param2
assertEquals(-444, module.exports.main(0, 6, 3, 555, 999)); // JIT sub function
assertEquals(13, module.exports.main(0, 6, 9, 45, 32)); // JIT sub function
assertEquals(187, module.exports.main(6, 6, 6, 17, 11)); // JIT mul function
assertEquals(30525, module.exports.main(6, 6, 9, 555, 55)); // JIT mul function
