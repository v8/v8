// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

load('test/mjsunit/wasm/wasm-constants.js');
load('test/mjsunit/wasm/wasm-module-builder.js');

let initialMemoryPages = 1;
let maximumMemoryPages = 5;
let other_load_idx = 0;

// This builder can be used to generate a module with memory + load/store
// functions and/or an additional imported function.
function generateBuilder(add_memory, add_import) {
  let builder = new WasmModuleBuilder();
  if (add_import) {
    // Add the import if we expect a module builder with imported functions.
    let idx = builder.addImport('import_module', 'other_module_load', kSig_i_i);
    // The imported function should always have index 0. With this assertion we
    // verify that we can use other_load_idx to refer to this function.
    assertEquals(idx, other_load_idx)
  }
  if (add_memory) {
    // Add the memory if we expect a module builder with memory and load/store.
    builder.addMemory(initialMemoryPages, maximumMemoryPages, true);
    builder.addFunction('load', kSig_i_i)
        .addBody([kExprGetLocal, 0, kExprI32LoadMem, 0, 0])
        .exportFunc();
    builder.addFunction('store', kSig_i_ii)
        .addBody([
          kExprGetLocal, 0, kExprGetLocal, 1, kExprI32StoreMem, 0, 0,
          kExprGetLocal, 1
        ])
        .exportFunc();
  }
  return builder;
}

// This test verifies that when a Wasm module without memory invokes a function
// imported from another module that has memory, the second module reads its own
// memory and returns the expected value.
(function TestExternalCallBetweenTwoWasmModulesWithoutAndWithMemory() {
  print('TestExternalCallBetweenTwoWasmModulesWithoutAndWithMemory');

  let first_module = generateBuilder(add_memory = false, add_import = true);
  // Function to invoke the imported function and add 1 to the result.
  first_module.addFunction('plus_one', kSig_i_i)
      .addBody([
        kExprGetLocal, 0,                   // -
        kExprCallFunction, other_load_idx,  // call the imported function
        kExprI32Const, 1,                   // -
        kExprI32Add,                        // add 1 to the result
        kExprReturn                         // -
      ])
      .exportFunc();
  let second_module = generateBuilder(add_memory = true, add_import = false);

  let index = kPageSize - 4;
  let second_value = 2222;
  // Instantiate the instances.
  let second_instance = second_module.instantiate();
  let first_instance = first_module.instantiate(
      {import_module: {other_module_load: second_instance.exports.load}});
  // Write the values in the second instance.
  second_instance.exports.store(index, second_value);
  assertEquals(second_value, second_instance.exports.load(index));
  // Verify that the value is correct when passing from the imported function.
  assertEquals(second_value + 1, first_instance.exports.plus_one(index));
})();

// This test verifies that when a Wasm module with memory invokes a function
// imported from another module that also has memory, the second module reads
// its own memory and returns the expected value.
(function TestExternalCallBetweenTwoWasmModulesWithMemory() {
  print('TestExternalCallBetweenTwoWasmModulesWithMemory');

  let first_module = generateBuilder(add_memory = true, add_import = true);
  // Function to invoke the imported function and add 1 to the result.
  first_module.addFunction('plus_one', kSig_i_i)
      .addBody([
        kExprGetLocal, 0,                   // -
        kExprCallFunction, other_load_idx,  // call the imported function
        kExprI32Const, 1,                   // -
        kExprI32Add,                        // add 1 to the result
        kExprReturn                         // -
      ])
      .exportFunc();
  let second_module = generateBuilder(add_memory = true, add_import = false);

  let index = kPageSize - 4;
  let first_value = 1111;
  let second_value = 2222;
  // Instantiate the instances.
  let second_instance = second_module.instantiate();
  let first_instance = first_module.instantiate(
      {import_module: {other_module_load: second_instance.exports.load}});
  // Write the values in the two instances.
  first_instance.exports.store(index, first_value);
  second_instance.exports.store(index, second_value);
  // Verify that the values were stored to memory.
  assertEquals(first_value, first_instance.exports.load(index));
  assertEquals(second_value, second_instance.exports.load(index));
  // Verify that the value is correct when passing from the imported function.
  assertEquals(second_value + 1, first_instance.exports.plus_one(index));
})();
