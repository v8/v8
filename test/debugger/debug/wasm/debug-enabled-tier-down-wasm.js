// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

load("test/mjsunit/wasm/wasm-module-builder.js");

const num_functions = 200;

// Create a simple Wasm script.
function create_builder(delta = 0) {
  const builder = new WasmModuleBuilder();
  for (let i = 0; i < num_functions; ++i) {
    builder.addFunction('f' + i, kSig_i_v)
        .addBody(wasmI32Const(i + delta))
        .exportFunc();
  }
  return builder;
}

function check(instance) {
  for (let i = 0; i < num_functions; ++i) {
    assertTrue(%IsLiftoffFunction(instance.exports['f' + i]));
  }
}

const instance = create_builder().instantiate();
const Debug = new DebugWrapper();
Debug.enable();
check(instance);
const newInstance = create_builder(num_functions*2).instantiate();
check(newInstance);

// Async.
async function testTierDownToLiftoffAsync() {
  Debug.disable();
  const asyncInstance = await create_builder(num_functions).asyncInstantiate();
  Debug.enable();
  check(asyncInstance);
  const newAsyncInstance = await create_builder(num_functions*3).asyncInstantiate();
  check(newAsyncInstance);
}

assertPromiseResult(testTierDownToLiftoffAsync());
