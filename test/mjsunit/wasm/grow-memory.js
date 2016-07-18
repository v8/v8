// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --expose-gc --stress-compaction

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

var kPageSize = 0x10000;

function genGrowMemoryBuilder() {
  var builder = new WasmModuleBuilder();
  builder.addFunction("grow_memory", kSig_i_i)
      .addBody([kExprGetLocal, 0, kExprGrowMemory])
      .exportFunc();
  builder.addFunction("load", kSig_i_i)
      .addBody([kExprGetLocal, 0, kExprI32LoadMem, 0, 0])
      .exportFunc();
  builder.addFunction("store", kSig_i_ii)
      .addBody([kExprGetLocal, 0, kExprGetLocal, 1, kExprI32StoreMem, 0, 0])
      .exportFunc();
  return builder;
}

function testGrowMemoryReadWrite() {
  var builder = genGrowMemoryBuilder();
  builder.addMemory(1, 1, false);
  var module = builder.instantiate();
  var offset;
  function peek() { return module.exports.load(offset); }
  function poke(value) { return module.exports.store(offset, value); }
  function growMem(pages) { return module.exports.grow_memory(pages); }

  for(offset = 0; offset <= (kPageSize - 4); offset++) {
    poke(20);
    assertEquals(peek(), 20);
  }
  for (offset = kPageSize - 3; offset < kPageSize + 4; offset++) {
    assertTraps(kTrapMemOutOfBounds, poke);
    assertTraps(kTrapMemOutOfBounds, peek);
  }

  try {
    assertEquals(growMem(3), 1);
  } catch (e) {
    assertEquals("object", typeof e);
    assertEquals(e.message, kTrapMsgs[kTrapMemAllocationFail]);
    return;
  }

  for (offset = kPageSize; offset <= 4*kPageSize -4; offset++) {
    poke(20);
    assertEquals(peek(), 20);
  }
  for (offset = 4*kPageSize - 3; offset < 4*kPageSize + 4; offset++) {
    assertTraps(kTrapMemOutOfBounds, poke);
    assertTraps(kTrapMemOutOfBounds, peek);
  }

  try {
    assertEquals(growMem(15), 4);
  } catch (e) {
    assertEquals("object", typeof e);
    assertEquals(e.message, kTrapMsgs[kTrapMemAllocationFail]);
    return;
  }

  for (offset = 4*kPageSize - 3; offset <= 4*kPageSize + 4; offset++) {
    poke(20);
    assertEquals(peek(), 20);
  }
  for (offset = 19*kPageSize - 10; offset <= 19*kPageSize - 4; offset++) {
    poke(20);
    assertEquals(peek(), 20);
  }
  for (offset = 19*kPageSize - 3; offset < 19*kPageSize + 5; offset++) {
    assertTraps(kTrapMemOutOfBounds, poke);
    assertTraps(kTrapMemOutOfBounds, peek);
  }
}

testGrowMemoryReadWrite();

function testGrowMemoryZeroInitialSize() {
  var builder = genGrowMemoryBuilder();
  var module = builder.instantiate();
  var offset;
  function peek() { return module.exports.load(offset); }
  function poke(value) { return module.exports.store(offset, value); }
  function growMem(pages) { return module.exports.grow_memory(pages); }

  assertTraps(kTrapMemOutOfBounds, peek);
  assertTraps(kTrapMemOutOfBounds, poke);

  try {
    assertEquals(growMem(1), 0);
  } catch (e) {
    assertEquals("object", typeof e);
    assertEquals(e.message, kTrapMsgs[kTrapMemAllocationFail]);
    return;
  }

  for(offset = 0; offset <= kPageSize - 4; offset++) {
    poke(20);
    assertEquals(peek(), 20);
  }

  //TODO(gdeepti): Fix tests with correct write boundaries
  //when runtime function is fixed.
  for(offset = kPageSize; offset <= kPageSize + 5; offset++) {
    assertTraps(kTrapMemOutOfBounds, peek);
  }
}

testGrowMemoryZeroInitialSize();

function testGrowMemoryTrapMaxPagesZeroInitialMemory() {
  var builder = genGrowMemoryBuilder();
  var module = builder.instantiate();
  var maxPages = 16385;
  function growMem() { return module.exports.grow_memory(maxPages); }
  assertTraps(kTrapMemOutOfBounds, growMem);
}

testGrowMemoryTrapMaxPagesZeroInitialMemory();

function testGrowMemoryTrapMaxPages() {
  var builder = genGrowMemoryBuilder();
  builder.addMemory(1, 1, false);
  var module = builder.instantiate();
  var maxPages = 16384;
  function growMem() { return module.exports.grow_memory(maxPages); }
  assertTraps(kTrapMemOutOfBounds, growMem);
}

testGrowMemoryTrapMaxPages();
