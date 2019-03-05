// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-bulk-memory

load("test/mjsunit/wasm/wasm-module-builder.js");

(function TestPassiveDataSegment() {
  const builder = new WasmModuleBuilder();
  builder.addMemory(1, 1, false);
  builder.addPassiveDataSegment([0, 1, 2]);
  builder.addPassiveDataSegment([3, 4]);

  // Should not throw.
  builder.instantiate();
})();

(function TestPassiveElementSegment() {
  const builder = new WasmModuleBuilder();
  builder.addFunction('f', kSig_v_v).addBody([]);
  builder.setTableBounds(1, 1);
  builder.addPassiveElementSegment([0, 0, 0]);
  builder.addPassiveElementSegment([0, 0]);

  // Should not throw.
  builder.instantiate();
})();

function assertBufferContents(buf, expected) {
  for (let i = 0; i < expected.length; ++i) {
    assertEquals(expected[i], buf[i]);
  }
  for (let i = expected.length; i < buf.length; ++i) {
    assertEquals(0, buf[i]);
  }
}

function getMemoryInit(mem, segment_data) {
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory("", "mem", 0);
  builder.addPassiveDataSegment(segment_data);
  builder.addFunction('init', kSig_v_iii)
      .addBody([
        kExprGetLocal, 0,  // Dest.
        kExprGetLocal, 1,  // Source.
        kExprGetLocal, 2,  // Size in bytes.
        kNumericPrefix, kExprMemoryInit,
        0,  // Data segment index.
        0,  // Memory index.
      ])
      .exportAs('init');
  return builder.instantiate({'': {mem}}).exports.init;
}

(function TestMemoryInit() {
  const mem = new WebAssembly.Memory({initial: 1});
  const memoryInit = getMemoryInit(mem, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);

  const u8a = new Uint8Array(mem.buffer);

  // All zeroes.
  assertBufferContents(u8a, []);

  // Copy all bytes from data segment 0, to memory at [10, 20).
  memoryInit(10, 0, 10);
  assertBufferContents(u8a, [0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);

  // Copy bytes in range [5, 10) from data segment 0, to memory at [0, 5).
  memoryInit(0, 5, 5);
  assertBufferContents(u8a, [5, 6, 7, 8, 9, 0, 0, 0, 0, 0,
                             0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);

  // Copy 0 bytes does nothing.
  memoryInit(10, 1, 0);
  assertBufferContents(u8a, [5, 6, 7, 8, 9, 0, 0, 0, 0, 0,
                             0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);

  // Copy 0 at end of memory region or data segment is OK.
  memoryInit(kPageSize, 0, 0);
  memoryInit(0, 10, 0);
})();

(function TestMemoryInitOutOfBoundsData() {
  const mem = new WebAssembly.Memory({initial: 1});
  const memoryInit = getMemoryInit(mem, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);

  const u8a = new Uint8Array(mem.buffer);
  const last5Bytes = new Uint8Array(mem.buffer, kPageSize - 5);

  // Write all values up to the out-of-bounds write.
  assertTraps(kTrapMemOutOfBounds, () => memoryInit(kPageSize - 5, 0, 6));
  assertBufferContents(last5Bytes, [0, 1, 2, 3, 4]);

  // Write all values up to the out-of-bounds read.
  u8a.fill(0);
  assertTraps(kTrapMemOutOfBounds, () => memoryInit(0, 5, 6));
  assertBufferContents(u8a, [5, 6, 7, 8, 9]);
})();


(function TestMemoryInitOutOfBounds() {
  const mem = new WebAssembly.Memory({initial: 1});
  // Create a data segment that has a length of kPageSize.
  const memoryInit = getMemoryInit(mem, new Array(kPageSize));

  // OK, copy the full data segment to memory.
  memoryInit(0, 0, kPageSize);

  // Source range must not be out of bounds.
  assertTraps(kTrapMemOutOfBounds, () => memoryInit(0, 1, kPageSize));
  assertTraps(kTrapMemOutOfBounds, () => memoryInit(0, 1000, kPageSize));
  assertTraps(kTrapMemOutOfBounds, () => memoryInit(0, kPageSize, 1));

  // Destination range must not be out of bounds.
  assertTraps(kTrapMemOutOfBounds, () => memoryInit(1, 0, kPageSize));
  assertTraps(kTrapMemOutOfBounds, () => memoryInit(1000, 0, kPageSize));
  assertTraps(kTrapMemOutOfBounds, () => memoryInit(kPageSize, 0, 1));

  // Copy 0 out-of-bounds fails.
  assertTraps(kTrapMemOutOfBounds, () => memoryInit(kPageSize + 1, 0, 0));
  assertTraps(kTrapMemOutOfBounds, () => memoryInit(0, kPageSize + 1, 0));

  // Make sure bounds aren't checked with 32-bit wrapping.
  assertTraps(kTrapMemOutOfBounds, () => memoryInit(1, 1, -1));

  mem.grow(1);

  // Works properly after grow.
  memoryInit(kPageSize, 0, 1000);

  // Traps at new boundary.
  assertTraps(
      kTrapMemOutOfBounds, () => memoryInit(kPageSize + 1, 0, kPageSize));
})();

(function TestMemoryInitOnActiveSegment() {
  const builder = new WasmModuleBuilder();
  builder.addMemory(1);
  builder.addPassiveDataSegment([1, 2, 3]);
  builder.addDataSegment(0, [4, 5, 6]);
  builder.addFunction('init', kSig_v_v)
      .addBody([
        kExprI32Const, 0,  // Dest.
        kExprI32Const, 0,  // Source.
        kExprI32Const, 0,  // Size in bytes.
        kNumericPrefix, kExprMemoryInit,
        1,  // Data segment index.
        0,  // Memory index.
      ])
      .exportAs('init');

  // Instantiation succeeds, because using memory.init with an active segment
  // is a trap, not a validation error.
  const instance = builder.instantiate();

  assertTraps(kTrapDataSegmentDropped, () => instance.exports.init());
})();

(function TestMemoryInitOnDroppedSegment() {
  const builder = new WasmModuleBuilder();
  builder.addMemory(1);
  builder.addPassiveDataSegment([1, 2, 3]);
  builder.addFunction('init', kSig_v_v)
      .addBody([
        kExprI32Const, 0,  // Dest.
        kExprI32Const, 0,  // Source.
        kExprI32Const, 0,  // Size in bytes.
        kNumericPrefix, kExprMemoryInit,
        0,  // Data segment index.
        0,  // Memory index.
      ])
      .exportAs('init');
  builder.addFunction('drop', kSig_v_v)
      .addBody([
        kNumericPrefix, kExprDataDrop,
        0,  // Data segment index.
      ])
      .exportAs('drop');

  // Instantiation succeeds, because using memory.init with an active segment
  // is a trap, not a validation error.
  const instance = builder.instantiate();

  // OK, segment hasn't been dropped.
  instance.exports.init();

  instance.exports.drop();

  // After segment has been dropped, memory.init and memory.drop fail.
  assertTraps(kTrapDataSegmentDropped, () => instance.exports.init());
  assertTraps(kTrapDataSegmentDropped, () => instance.exports.drop());
})();

(function TestDataDropOnActiveSegment() {
  const builder = new WasmModuleBuilder();
  builder.addMemory(1);
  builder.addPassiveDataSegment([1, 2, 3]);
  builder.addDataSegment(0, [4, 5, 6]);
  builder.addFunction('drop', kSig_v_v)
      .addBody([
        kNumericPrefix, kExprDataDrop,
        1,  // Data segment index.
      ])
      .exportAs('drop');

  const instance = builder.instantiate();
  assertTraps(kTrapDataSegmentDropped, () => instance.exports.drop());
})();

function getMemoryCopy(mem) {
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory("", "mem", 0);
  builder.addFunction("copy", kSig_v_iii).addBody([
    kExprGetLocal, 0,  // Dest.
    kExprGetLocal, 1,  // Source.
    kExprGetLocal, 2,  // Size in bytes.
    kNumericPrefix, kExprMemoryCopy, 0, 0,
  ]).exportAs("copy");
  return builder.instantiate({'': {mem}}).exports.copy;
}

(function TestMemoryCopy() {
  const mem = new WebAssembly.Memory({initial: 1});
  const memoryCopy = getMemoryCopy(mem);

  const u8a = new Uint8Array(mem.buffer);
  u8a.set([0, 11, 22, 33, 44, 55, 66, 77]);

  memoryCopy(10, 1, 8);

  assertBufferContents(u8a, [0, 11, 22, 33, 44, 55, 66, 77, 0, 0,
                             11, 22, 33, 44, 55, 66, 77]);

  // Copy 0 bytes does nothing.
  memoryCopy(10, 1, 0);
  assertBufferContents(u8a, [0, 11, 22, 33, 44, 55, 66, 77, 0, 0,
                             11, 22, 33, 44, 55, 66, 77]);

  // Copy 0 at end of memory region is OK.
  memoryCopy(kPageSize, 0, 0);
  memoryCopy(0, kPageSize, 0);
})();

(function TestMemoryCopyOverlapping() {
  const mem = new WebAssembly.Memory({initial: 1});
  const memoryCopy = getMemoryCopy(mem);

  const u8a = new Uint8Array(mem.buffer);
  u8a.set([10, 20, 30]);

  // Copy from [0, 3] -> [2, 5]. The copy must not overwrite 30 before copying
  // it (i.e. cannot copy forward in this case).
  memoryCopy(2, 0, 3);
  assertBufferContents(u8a, [10, 20, 10, 20, 30]);

  // Copy from [2, 5] -> [0, 3]. The copy must not write the first 10 (i.e.
  // cannot copy backward in this case).
  memoryCopy(0, 2, 3);
  assertBufferContents(u8a, [10, 20, 30, 20, 30]);
})();

(function TestMemoryCopyOutOfBoundsData() {
  const mem = new WebAssembly.Memory({initial: 1});
  const memoryCopy = getMemoryCopy(mem);

  const u8a = new Uint8Array(mem.buffer);
  const first5Bytes = new Uint8Array(mem.buffer, 0, 5);
  const last5Bytes = new Uint8Array(mem.buffer, kPageSize - 5);
  u8a.set([11, 22, 33, 44, 55, 66, 77, 88]);

  // Write all values up to the out-of-bounds access.
  assertTraps(kTrapMemOutOfBounds, () => memoryCopy(kPageSize - 5, 0, 6));
  assertBufferContents(last5Bytes, [11, 22, 33, 44, 55]);

  // Copy overlapping with destination < source. Copy will happen forwards, up
  // to the out-of-bounds access.
  u8a.fill(0);
  last5Bytes.set([11, 22, 33, 44, 55]);
  assertTraps(
      kTrapMemOutOfBounds, () => memoryCopy(0, kPageSize - 5, kPageSize));
  assertBufferContents(first5Bytes, [11, 22, 33, 44, 55]);

  // Copy overlapping with source < destination. Copy would happen backwards,
  // but the first byte to copy is out-of-bounds, so no data should be written.
  u8a.fill(0);
  first5Bytes.set([11, 22, 33, 44, 55]);
  assertTraps(
      kTrapMemOutOfBounds, () => memoryCopy(kPageSize - 5, 0, kPageSize));
  assertBufferContents(last5Bytes, [0, 0, 0, 0, 0]);
})();

(function TestMemoryCopyOutOfBounds() {
  const mem = new WebAssembly.Memory({initial: 1});
  const memoryCopy = getMemoryCopy(mem);

  memoryCopy(0, 0, kPageSize);

  // Source range must not be out of bounds.
  assertTraps(kTrapMemOutOfBounds, () => memoryCopy(0, 1, kPageSize));
  assertTraps(kTrapMemOutOfBounds, () => memoryCopy(0, 1000, kPageSize));
  assertTraps(kTrapMemOutOfBounds, () => memoryCopy(0, kPageSize, 1));

  // Destination range must not be out of bounds.
  assertTraps(kTrapMemOutOfBounds, () => memoryCopy(1, 0, kPageSize));
  assertTraps(kTrapMemOutOfBounds, () => memoryCopy(1000, 0, kPageSize));
  assertTraps(kTrapMemOutOfBounds, () => memoryCopy(kPageSize, 0, 1));

  // Copy 0 out-of-bounds fails.
  assertTraps(kTrapMemOutOfBounds, () => memoryCopy(kPageSize + 1, 0, 0));
  assertTraps(kTrapMemOutOfBounds, () => memoryCopy(0, kPageSize + 1, 0));

  // Make sure bounds aren't checked with 32-bit wrapping.
  assertTraps(kTrapMemOutOfBounds, () => memoryCopy(1, 1, -1));

  mem.grow(1);

  // Works properly after grow.
  memoryCopy(0, kPageSize, 1000);

  // Traps at new boundary.
  assertTraps(
      kTrapMemOutOfBounds, () => memoryCopy(0, kPageSize + 1, kPageSize));
})();

function getMemoryFill(mem) {
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory("", "mem", 0);
  builder.addFunction("fill", kSig_v_iii).addBody([
    kExprGetLocal, 0,  // Dest.
    kExprGetLocal, 1,  // Byte value.
    kExprGetLocal, 2,  // Size.
    kNumericPrefix, kExprMemoryFill, 0,
  ]).exportAs("fill");
  return builder.instantiate({'': {mem}}).exports.fill;
}

(function TestMemoryFill() {
  const mem = new WebAssembly.Memory({initial: 1});
  const memoryFill = getMemoryFill(mem);

  const u8a = new Uint8Array(mem.buffer);

  memoryFill(1, 33, 5);
  assertBufferContents(u8a, [0, 33, 33, 33, 33, 33]);

  memoryFill(4, 66, 4);
  assertBufferContents(u8a, [0, 33, 33, 33, 66, 66, 66, 66]);

  // Fill 0 bytes does nothing.
  memoryFill(4, 66, 0);
  assertBufferContents(u8a, [0, 33, 33, 33, 66, 66, 66, 66]);

  // Fill 0 at end of memory region is OK.
  memoryFill(kPageSize, 66, 0);
})();

(function TestMemoryFillValueWrapsToByte() {
  const mem = new WebAssembly.Memory({initial: 1});
  const memoryFill = getMemoryFill(mem);

  const u8a = new Uint8Array(mem.buffer);

  memoryFill(0, 1000, 3);
  const expected = 1000 & 255;
  assertBufferContents(u8a, [expected, expected, expected]);
})();

(function TestMemoryFillOutOfBoundsData() {
  const mem = new WebAssembly.Memory({initial: 1});
  const memoryFill = getMemoryFill(mem);
  const v = 123;

  // Write all values up to the out-of-bound access.
  assertTraps(kTrapMemOutOfBounds, () => memoryFill(kPageSize - 5, v, 999));
  const u8a = new Uint8Array(mem.buffer, kPageSize - 6);
  assertBufferContents(u8a, [0, 123, 123, 123, 123, 123]);
})();

(function TestMemoryFillOutOfBounds() {
  const mem = new WebAssembly.Memory({initial: 1});
  const memoryFill = getMemoryFill(mem);
  const v = 123;

  memoryFill(0, 0, kPageSize);

  // Destination range must not be out of bounds.
  assertTraps(kTrapMemOutOfBounds, () => memoryFill(1, v, kPageSize));
  assertTraps(kTrapMemOutOfBounds, () => memoryFill(1000, v, kPageSize));
  assertTraps(kTrapMemOutOfBounds, () => memoryFill(kPageSize, v, 1));

  // Fill 0 out-of-bounds fails.
  assertTraps(kTrapMemOutOfBounds, () => memoryFill(kPageSize + 1, v, 0));

  // Make sure bounds aren't checked with 32-bit wrapping.
  assertTraps(kTrapMemOutOfBounds, () => memoryFill(1, v, -1));

  mem.grow(1);

  // Works properly after grow.
  memoryFill(kPageSize, v, 1000);

  // Traps at new boundary.
  assertTraps(
      kTrapMemOutOfBounds, () => memoryFill(kPageSize + 1, v, kPageSize));
})();

(function TestElemDropActive() {
  const builder = new WasmModuleBuilder();
  builder.setTableBounds(5, 5);
  builder.addElementSegment(0, false, [0, 0, 0]);
  builder.addFunction('drop', kSig_v_v)
      .addBody([
        kNumericPrefix, kExprElemDrop,
        0,  // Element segment index.
      ])
      .exportAs('drop');

  const instance = builder.instantiate();
  assertTraps(kTrapElemSegmentDropped, () => instance.exports.drop());
})();

(function TestElemDropTwice() {
  const builder = new WasmModuleBuilder();
  builder.setTableBounds(5, 5);
  builder.addPassiveElementSegment([0, 0, 0]);
  builder.addFunction('drop', kSig_v_v)
      .addBody([
        kNumericPrefix, kExprElemDrop,
        0,  // Element segment index.
      ])
      .exportAs('drop');

  const instance = builder.instantiate();
  instance.exports.drop();
  assertTraps(kTrapElemSegmentDropped, () => instance.exports.drop());
})();
