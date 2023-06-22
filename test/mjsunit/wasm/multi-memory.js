// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-multi-memory

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Add a {loadN} and {storeN} method for memory N.
function addLoadAndStoreFunctions(builder, mem_index) {
  builder.addFunction('load' + mem_index, kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0x40, mem_index, 0])
      .exportFunc();
  builder.addFunction('store' + mem_index, kSig_v_ii)
      .addBody([
        kExprLocalGet, 1, kExprLocalGet, 0, kExprI32StoreMem, 0x40, mem_index, 0
      ])
      .exportFunc();
}

// Helper to test that two memories can be accessed independently.
function testTwoMemories(instance, mem0_size, mem1_size) {
  const load0 = offset => instance.exports.load0(offset);
  const load1 = offset => instance.exports.load1(offset);
  const store0 = (value, offset) =>  instance.exports.store0(value, offset);
  const store1 = (value, offset) =>  instance.exports.store1(value, offset);

  assertEquals(0, load0(0));
  assertEquals(0, load1(0));

  store0(47, 0);
  assertEquals(47, load0(0));
  assertEquals(0, load1(0));

  store1(11, 0);
  assertEquals(47, load0(0));
  assertEquals(11, load1(0));

  store1(22, 1);
  assertEquals(47, load0(0));
  assertEquals(22 , load1(1));
  // The 22 is in the second byte when loading from 0.
  assertEquals(22 * 256 + 11, load1(0));

  const mem0_bytes = mem0_size * kPageSize;
  load0(mem0_bytes - 4);  // should not trap.
  for (const offset of [-3, -1, mem0_bytes - 3, mem0_bytes - 1, mem0_bytes]) {
    assertTraps(kTrapMemOutOfBounds, () => load0(offset));
    assertTraps(kTrapMemOutOfBounds, () => store0(0, offset));
  }

  const mem1_bytes = mem1_size * kPageSize;
  load1(mem1_bytes - 4);  // should not trap.
  for (const offset of [-3, -1, mem1_bytes - 3, mem1_bytes - 1, mem1_bytes]) {
    assertTraps(kTrapMemOutOfBounds, () => load1(offset));
    assertTraps(kTrapMemOutOfBounds, () => store1(0, offset));
  }
}

(function testBasicMultiMemory() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addMemory(1, 1);
  builder.addMemory(1, 1);
  addLoadAndStoreFunctions(builder, 0);
  addLoadAndStoreFunctions(builder, 1);

  const instance = builder.instantiate();
  testTwoMemories(instance, 1, 1);
})();

(function testImportedAndDeclaredMemories() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory('imp', 'mem0');
  builder.addMemory(1, 1);
  addLoadAndStoreFunctions(builder, 0);
  addLoadAndStoreFunctions(builder, 1);

  const mem0 = new WebAssembly.Memory({initial: 3, maximum: 4});
  const instance = builder.instantiate({imp: {mem0: mem0}});
  testTwoMemories(instance, 3, 1);
})();

(function testTwoImportedMemories() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory('imp', 'mem0');
  builder.addImportedMemory('imp', 'mem1');
  addLoadAndStoreFunctions(builder, 0);
  addLoadAndStoreFunctions(builder, 1);

  const mem0 = new WebAssembly.Memory({initial: 2, maximum: 3});
  const mem1 = new WebAssembly.Memory({initial: 3, maximum: 4});
  const instance = builder.instantiate({imp: {mem0: mem0, mem1: mem1}});
  testTwoMemories(instance, 2, 3);
})();

(function testTwoExportedMemories() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addMemory(1, 1);
  builder.addMemory(1, 1);
  addLoadAndStoreFunctions(builder, 0);
  addLoadAndStoreFunctions(builder, 1);
  builder.exportMemoryAs('mem0', 0);
  builder.exportMemoryAs('mem1', 1);

  const instance = builder.instantiate();
  const mem0 = new Uint8Array(instance.exports.mem0.buffer);
  const mem1 = new Uint8Array(instance.exports.mem1.buffer);
  assertEquals(0, mem0[11]);
  assertEquals(0, mem1[11]);

  instance.exports.store0(47, 11);
  assertEquals(47, mem0[11]);
  assertEquals(0, mem1[11]);

  instance.exports.store1(49, 11);
  assertEquals(47, mem0[11]);
  assertEquals(49, mem1[11]);
})();
