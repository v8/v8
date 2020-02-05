// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-anyref

load("test/mjsunit/wasm/wasm-module-builder.js");

// Create a simple Wasm module.
function create_builder() {
  const builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_r_v)
      .addBody([kExprRefNull])
      .exportFunc();
  return builder;
}

const instance = create_builder().instantiate();

// Test recompilation.
const Debug = new DebugWrapper();
Debug.enable();
assertFalse(%IsLiftoffFunction(instance.exports.main));

// Async.
async function testTierDownToLiftoffAsync() {
  Debug.disable();
  const builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_i_r)
      .addBody([kExprLocalGet, 0, kExprRefIsNull])
      .exportFunc();
  const asyncInstance = await builder.asyncInstantiate();

  // Test recompilation.
  Debug.enable();
  assertFalse(%IsLiftoffFunction(instance.exports.main));
}

assertPromiseResult(testTierDownToLiftoffAsync());
