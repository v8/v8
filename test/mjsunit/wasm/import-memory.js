// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

(function TestOne() {
  print("TestOne");
  let memory = new WebAssembly.Memory({initial: 1});
  assertEquals(kPageSize, memory.buffer.byteLength);
  let i32 = new Int32Array(memory.buffer);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("mine");
  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprI32Const, 0,
      kExprI32LoadMem, 0, 0])
    .exportAs("main");

  let main = builder.instantiate({mine: memory}).exports.main;
  assertEquals(0, main());

  i32[0] = 993377;

  assertEquals(993377, main());
})();

(function TestIdentity() {
  print("TestIdentity");
  let memory = new WebAssembly.Memory({initial: 1});
  let i32 = new Int32Array(memory.buffer);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("garg");
  builder.exportMemoryAs("daggle");

  let instance = builder.instantiate({garg: memory});
  assertSame(memory, instance.exports.daggle);
})();


(function TestImportExport() {
  print("TestImportExport");
  var i1;
  {
    let builder = new WasmModuleBuilder();
    builder.addMemory(1, 1, false);
    builder.exportMemoryAs("exported_mem");
    builder.addFunction("foo", kSig_i_i)
      .addBody([
        kExprGetLocal, 0,
        kExprI32LoadMem, 0, 0])
      .exportAs("foo");
    i1 = builder.instantiate();
  }

  var i2;
  {
    let builder = new WasmModuleBuilder();
    builder.addMemory(1, 1, false);
    builder.addImportedMemory("imported_mem");
    builder.addFunction("bar", kSig_i_i)
      .addBody([
        kExprGetLocal, 0,
        kExprI32LoadMem, 0, 0])
      .exportAs("bar");
    i2 = builder.instantiate({imported_mem: i1.exports.exported_mem});
  }

  let i32 = new Int32Array(i1.exports.exported_mem.buffer);

  for (var i = 0; i < 1e11; i = i * 3 + 5) {
    for (var j = 0; j < 10; j++) {
      var val = i + 99077 + j;
      i32[j] = val;
      assertEquals(val | 0, i1.exports.foo(j * 4));
      assertEquals(val | 0, i2.exports.bar(j * 4));
    }
  }
})();
