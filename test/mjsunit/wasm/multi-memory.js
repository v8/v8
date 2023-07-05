// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-multi-memory

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

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

// Add a {growN} and {sizeN} method for memory N.
function addGrowAndSizeFunctions(builder, mem_index) {
  builder.addFunction('grow' + mem_index, kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprMemoryGrow, mem_index])
      .exportFunc();
  builder.addFunction('size' + mem_index, kSig_i_v)
      .addBody([kExprMemorySize, mem_index])
      .exportFunc();
}

// Helper to test that two memories can be accessed independently.
function testTwoMemories(instance, mem0_size, mem1_size) {
  const load0 = offset => instance.exports.load0(offset);
  const load1 = offset => instance.exports.load1(offset);
  const store0 = (value, offset) => instance.exports.store0(value, offset);
  const store1 = (value, offset) => instance.exports.store1(value, offset);

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
  assertEquals(22, load1(1));
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

function assertMemoryEquals(expected, memory) {
  assertInstanceof(memory, WebAssembly.Memory);
  assertInstanceof(expected, Uint8Array);
  const buf = new Uint8Array(memory.buffer);
  // For better output, check the first 50 bytes separately first.
  assertEquals(expected.slice(0, 50), buf.slice(0, 50));
  // Now also check the full memory content.
  assertEquals(expected, buf);
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

(function testMemoryIndexDecodedAsU32() {
  print(arguments.callee.name);
  for (let leb_length = 1; leb_length <= 6; ++leb_length) {
    // Create the array [0x80, 0x80, ..., 0x0] of length `leb_length`. This
    // encodes `0` using `leb_length` bytes.
    const leb = new Array(leb_length).fill(0x80).with(leb_length - 1, 0);
    const builder = new WasmModuleBuilder();
    builder.addMemory(1, 1);
    builder.addFunction('load', kSig_i_i)
        .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0x40, ...leb, 0])
        .exportFunc();
    builder.addFunction('store', kSig_v_ii)
        .addBody([
          kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0x40, ...leb, 0
        ])
        .exportFunc();
    builder.exportMemoryAs('mem');

    if (leb_length == 6) {
      assertThrows(
          () => builder.instantiate(), WebAssembly.CompileError,
          /length overflow while decoding memory index/);
      continue;
    }
    const instance = builder.instantiate();
    assertEquals(0, instance.exports.load(7));
    instance.exports.store(7, 11);
    assertEquals(11, instance.exports.load(7));
    assertEquals(0, instance.exports.load(11));

    const expected_memory = new Uint8Array(kPageSize);
    expected_memory[7] = 11;
    assertMemoryEquals(expected_memory, instance.exports.mem);
  }
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

(function testMultiMemoryDataSegments() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addMemory(1, 1);
  builder.addMemory(1, 1);
  addLoadAndStoreFunctions(builder, 0);
  addLoadAndStoreFunctions(builder, 1);
  const mem0_offset = 11;
  const mem1_offset = 23;
  builder.addDataSegment(mem1_offset, [7, 7], false, 1);
  builder.addDataSegment(mem0_offset, [9, 9], false, 0);
  builder.exportMemoryAs('mem0', 0);
  builder.exportMemoryAs('mem1', 1);

  const instance = builder.instantiate();
  const expected_memory0 = new Uint8Array(kPageSize);
  expected_memory0.set([9, 9], mem0_offset);
  const expected_memory1 = new Uint8Array(kPageSize);
  expected_memory1.set([7, 7], mem1_offset);

  assertMemoryEquals(expected_memory0, instance.exports.mem0);
  assertMemoryEquals(expected_memory1, instance.exports.mem1);
})();

(function testMultiMemoryDataSegmentsOutOfBounds() {
  print(arguments.callee.name);
  // Check that we use the right memory size for the bounds check.
  for (let [mem0_size, mem1_size] of [[1, 2], [2, 1]]) {
    for (let [mem0_offset, mem1_offset] of [[0, 0], [1, 2], [0, 2], [1, 0]]) {
      var builder = new WasmModuleBuilder();
      builder.addMemory(mem0_size, mem0_size);
      builder.addMemory(mem1_size, mem1_size);
      builder.addDataSegment(mem0_offset * kPageSize, [0], false, 0);
      builder.addDataSegment(mem1_offset * kPageSize, [0], false, 1);
      if (mem0_offset < mem0_size && mem1_offset < mem1_size) {
        builder.instantiate();  // should not throw.
        continue;
      }
      const oob = mem0_offset >= mem0_size ? 0 : 1;
      const expected_offset = [mem0_offset, mem1_offset][oob] * kPageSize;
      const expected_mem_size = [mem0_size, mem1_size][oob] * kPageSize;
      const expected_msg =
          `WebAssembly.Instance(): data segment ${oob} is out of bounds ` +
          `(offset ${expected_offset}, length 1, ` +
          `memory size ${expected_mem_size})`;
      assertThrows(
          () => builder.instantiate(), WebAssembly.RuntimeError, expected_msg);
    }
  }
})();

(function testGrowMultipleMemories() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addMemory(1, 4);
  builder.addMemory(1, 5);
  addGrowAndSizeFunctions(builder, 0);
  addGrowAndSizeFunctions(builder, 1);
  builder.exportMemoryAs('mem0', 0);
  builder.exportMemoryAs('mem1', 1);
  const instance = builder.instantiate();

  assertEquals(1, instance.exports.grow0(2));
  assertEquals(3, instance.exports.grow0(1));
  assertEquals(4, instance.exports.size0());
  assertEquals(-1, instance.exports.grow0(1));
  assertEquals(4, instance.exports.size0());

  assertEquals(1, instance.exports.grow1(2));
  assertEquals(3, instance.exports.grow1(2));
  assertEquals(5, instance.exports.size1());
  assertEquals(-1, instance.exports.grow1(1));
  assertEquals(5, instance.exports.size1());
})();

(function testGrowDecodesMemoryIndexAsU32() {
  print(arguments.callee.name);
  for (let leb_length = 1; leb_length <= 6; ++leb_length) {
    // Create the array [0x80, 0x80, ..., 0x0] of length `leb_length`. This
    // encodes `0` using `leb_length` bytes.
    const leb = new Array(leb_length).fill(0x80).with(leb_length - 1, 0);
    var builder = new WasmModuleBuilder();
    builder.addMemory(1, 4);
    builder.addFunction('grow', kSig_i_i)
        .addBody([kExprLocalGet, 0, kExprMemoryGrow, ...leb])
        .exportFunc();
    builder.exportMemoryAs('mem', 0);

    if (leb_length == 6) {
      assertThrows(
          () => builder.instantiate(), WebAssembly.CompileError,
          /length overflow while decoding memory index/);
      continue;
    }
    const instance = builder.instantiate();

    assertEquals(1, instance.exports.grow(2));
    assertEquals(-1, instance.exports.grow(2));
    assertEquals(3, instance.exports.grow(1));
  }
})();
