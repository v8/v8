// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --stress-compaction

load('test/mjsunit/wasm/wasm-constants.js');
load('test/mjsunit/wasm/wasm-module-builder.js');

var initialMemoryPages = 1;
var maximumMemoryPages = 5;

// Grow memory in directly called functions.
print('=== grow_memory in direct calls ===');

// This test verifies that the current_memory instruction returns the correct
// value after returning from a function (direct call) that grew memory.
(function TestGrowMemoryInFunction() {
  print('TestGrowMemoryInFunction ...');
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages, true);
  let kGrowFunction =
      builder.addFunction('grow', kSig_i_i)
          .addBody([kExprGetLocal, 0, kExprGrowMemory, kMemoryZero])
          .exportFunc()
          .index;
  builder.addFunction('main', kSig_i_i)
      .addBody([
        kExprGetLocal, 0,                  // get number of new pages
        kExprCallFunction, kGrowFunction,  // call the grow function
        kExprDrop,                         // drop the result of grow
        kExprMemorySize, kMemoryZero       // get the memory size
      ])
      .exportFunc();
  var instance = builder.instantiate();
  // The caller should be aware that the memory was grown by the callee.
  var deltaPages = 1;
  assertEquals(
      initialMemoryPages + deltaPages, instance.exports.main(deltaPages));
})();

// This test verifies that accessing a memory page that has been created inside
// a function (direct call) does not trap in the caller.
(function TestGrowMemoryAndAccessInFunction() {
  print('TestGrowMemoryAndAccessInFunction ...');
  let index = 2 * kPageSize - 4;
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages, true);
  let kGrowFunction =
      builder.addFunction('grow', kSig_i_i)
          .addBody([kExprGetLocal, 0, kExprGrowMemory, kMemoryZero])
          .exportFunc()
          .index;
  builder.addFunction('load', kSig_i_i)
      .addBody([kExprGetLocal, 0, kExprI32LoadMem, 0, 0])
      .exportFunc();
  builder.addFunction('main', kSig_v_iii)
      .addBody([
        kExprGetLocal, 0,                  // get number of new pages
        kExprCallFunction, kGrowFunction,  // call the grow function
        kExprDrop,                         // drop the result of grow
        kExprGetLocal, 1,                  // get index
        kExprGetLocal, 2,                  // get value
        kExprI32StoreMem, 0, 0             // store
      ])
      .exportFunc();
  var instance = builder.instantiate();
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.load(index));
  var deltaPages = 1;
  instance.exports.main(deltaPages, index, 1234);
  // The caller should be able to access memory that was grown by the callee.
  assertEquals(1234, instance.exports.load(index));
})();

// This test verifies that when a function (direct call) grows and store
// something in the grown memory, the caller always reads from the grown
// memory. This checks that the memory start address gets updated in the caller.
(function TestGrowMemoryAndStoreInFunction() {
  print('TestGrowMemoryAndStoreInFunction ...');
  let index = 0;
  let oldValue = 21;
  let newValue = 42;
  let deltaPages = 1;
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages, true);
  let kGrowFunction =
      builder.addFunction('grow', kSig_v_v)
          .addBody([
            kExprI32Const, deltaPages,     // always grow memory by deltaPages
            kExprGrowMemory, kMemoryZero,  // grow memory
            kExprDrop,                     // drop the result of grow
            kExprI32Const, index,          // put index on stack
            kExprI32Const, newValue,       // put new value on stack
            kExprI32StoreMem, 0, 0         // store
          ])
          .exportFunc()
          .index;
  builder.addFunction('main', kSig_i_i)
      .addBody([
        kExprI32Const, index,              // put index on stack
        kExprI32Const, oldValue,           // put old value on stack
        kExprI32StoreMem, 0, 0,            // store
        kExprCallFunction, kGrowFunction,  // call grow_and_store
        kExprI32Const, index,              // put index on stack
        kExprI32LoadMem, 0, 0              // load from grown memory
      ])
      .exportFunc();
  var instance = builder.instantiate();
  // The caller should always read from grown memory.
  assertEquals(newValue, instance.exports.main());
})();

// Grow memory in indirectly called functions.
print('\n=== grow_memory in indirect calls ===');

// This test verifies that the current_memory instruction returns the correct
// value after returning from a function (indirect call) that grew memory.
(function TestGrowMemoryInIndirectCall() {
  print('TestGrowMemoryInIndirectCall ...');
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages, true);
  let kGrowFunction =
      builder.addFunction('grow', kSig_i_i)
          .addBody([kExprGetLocal, 0, kExprGrowMemory, kMemoryZero])
          .exportFunc()
          .index;
  builder.addFunction('main', kSig_i_ii)
      .addBody([
        kExprGetLocal, 1,                  // get number of new pages
        kExprGetLocal, 0,                  // get index of the function
        kExprCallIndirect, 0, kTableZero,  // call the function
        kExprDrop,                         // drop the result of grow
        kExprMemorySize, kMemoryZero       // get the memory size
      ])
      .exportFunc();
  builder.appendToTable([kGrowFunction]);
  var instance = builder.instantiate();
  // The caller should be aware that the memory was grown by the callee.
  var deltaPages = 1;
  assertEquals(
      initialMemoryPages + deltaPages,
      instance.exports.main(kGrowFunction, deltaPages));
})();

// This test verifies that accessing a memory page that has been created inside
// a function (indirect call) does not trap in the caller.
(function TestGrowMemoryAndAccessInIndirectCall() {
  print('TestGrowMemoryAndAccessInIndirectCall ...');
  let index = 2 * kPageSize - 4;
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages, true);
  let kGrowFunction =
      builder.addFunction('grow', kSig_i_i)
          .addBody([kExprGetLocal, 0, kExprGrowMemory, kMemoryZero])
          .exportFunc()
          .index;
  builder.addFunction('load', kSig_i_i)
      .addBody([kExprGetLocal, 0, kExprI32LoadMem, 0, 0])
      .exportFunc();
  let sig = makeSig([kWasmI32, kWasmI32, kWasmI32, kWasmI32], []);
  builder.addFunction('main', sig)
      .addBody([
        kExprGetLocal, 1,                  // get number of new pages
        kExprGetLocal, 0,                  // get index of the function
        kExprCallIndirect, 0, kTableZero,  // call the function
        kExprDrop,                         // drop the result of grow
        kExprGetLocal, 2,                  // get index
        kExprGetLocal, 3,                  // get value
        kExprI32StoreMem, 0, 0             // store
      ])
      .exportFunc();
  builder.appendToTable([kGrowFunction]);
  var instance = builder.instantiate();
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.load(index));
  let deltaPages = 1;
  let value = 1234;
  instance.exports.main(kGrowFunction, deltaPages, index, value);
  // The caller should be able to access memory that was grown by the callee.
  assertEquals(value, instance.exports.load(index));
})();

// This test verifies that when a function (indirect call) grows and store
// something in the grown memory, the caller always reads from the grown
// memory. This checks that the memory start address gets updated in the caller.
(function TestGrowMemoryAndStoreInIndirectCall() {
  print('TestGrowMemoryAndStoreInIndirectCall ...');
  let index = 0;
  let oldValue = 21;
  let newValue = 42;
  let deltaPages = 1;
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages, true);
  let kGrowFunction =
      builder.addFunction('grow', kSig_v_v)
          .addBody([
            kExprI32Const, deltaPages,     // always grow memory by deltaPages
            kExprGrowMemory, kMemoryZero,  // grow memory
            kExprDrop,                     // drop the result of grow
            kExprI32Const, index,          // put index on stack
            kExprI32Const, newValue,       // put new value on stack
            kExprI32StoreMem, 0, 0         // store
          ])
          .exportFunc()
          .index;
  builder.addFunction('main', kSig_i_i)
      .addBody([
        kExprI32Const, index,              // put index on stack
        kExprI32Const, oldValue,           // put old value on stack
        kExprI32StoreMem, 0, 0,            // store
        kExprGetLocal, 0,                  // get index of the function
        kExprCallIndirect, 0, kTableZero,  // call the function
        kExprI32Const, index,              // put index on stack
        kExprI32LoadMem, 0, 0              // load from grown memory
      ])
      .exportFunc();
  builder.appendToTable([kGrowFunction]);
  var instance = builder.instantiate();
  // The caller should always read from grown memory.
  assertEquals(42, instance.exports.main(kGrowFunction));
})();
