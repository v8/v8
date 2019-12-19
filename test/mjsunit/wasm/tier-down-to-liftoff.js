// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-tier-up

load('test/mjsunit/wasm/wasm-module-builder.js');

const num_functions = 2;

function create_builder() {
  const builder = new WasmModuleBuilder();
  for (let i = 0; i < num_functions; ++i) {
    builder.addFunction('f' + i, kSig_i_v)
        .addBody(wasmI32Const(i))
        .exportFunc();
  }
  return builder;
}

function check(instance) {
  for (let i = 0; i < num_functions; ++i) {
    %WasmTierUpFunction(instance, i);
    assertFalse(%IsLiftoffFunction(instance.exports['f' + i]));
  }

  %WasmTierDownModule(instance);
  for (let i = 0; i < num_functions; ++i) {
    assertTrue(%IsLiftoffFunction(instance.exports['f' + i]));
  }

  for (let i = 0; i < num_functions; ++i) {
    %WasmTierUpFunction(instance, i);
    assertTrue(%IsLiftoffFunction(instance.exports['f' + i]));
  }

  %WasmTierUpModule(instance);
  for (let i = 0; i < num_functions; ++i) {
    assertFalse(%IsLiftoffFunction(instance.exports['f' + i]));
  }
}

(function testTierDownToLiftoff() {
  print(arguments.callee.name);
  const instance = create_builder().instantiate();
  check(instance);
})();


async function testTierDownToLiftoffAsync() {
  print(arguments.callee.name);
  const instance = await create_builder().asyncInstantiate();
  check(instance);
}

assertPromiseResult(testTierDownToLiftoffAsync());
