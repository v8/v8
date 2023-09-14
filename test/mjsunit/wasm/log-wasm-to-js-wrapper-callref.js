// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --experimental-wasm-type-reflection

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let sampleCollected = false;
function OnProfilerSampleCallback(profile) {
  profile = profile.replaceAll('\\', '/');
  profile = JSON.parse(profile);
  let wasm_to_js_index = 0;
  let js_to_wasm_index = 0;
  let fib_index = 0;
  let imp_index = 0;
  for (const node of profile.nodes) {
    if (node.callFrame.functionName.startsWith('js-to-wasm')) {
      js_to_wasm_index = node.id;
    } else if (node.callFrame.functionName.startsWith('wasm-to-js')) {
      wasm_to_js_index = node.id;
    } else if (node.callFrame.functionName.startsWith('main')) {
      fib_index = node.id;
    } else if (node.callFrame.functionName.startsWith('imp')) {
      imp_index = node.id;
    }
  }
  assertTrue(js_to_wasm_index > 0);
  assertEquals(js_to_wasm_index + 1, fib_index);
  assertEquals(fib_index + 1, wasm_to_js_index);
  assertEquals(wasm_to_js_index + 1, imp_index);
  // {sampleCollected} is set at the end because the asserts above don't show up
  // in the test runner, probably because this function is called as a callback
  // from d8.
  sampleCollected = true;
}

const builder = new WasmModuleBuilder();
const sigId = builder.addType(kSig_i_i);
const g = builder.addImportedGlobal('m', 'val', kWasmAnyFunc);
builder.addFunction('main', sigId)
    .addBody([
      kExprLocalGet,
      0,
      kExprGlobalGet,
      g,
      kGCPrefix,
      kExprRefCast,
      sigId,
      kExprCallRef,
      sigId,
    ])
    .exportFunc();
const wasm_module = builder.toModule();

d8.profiler.setOnProfileEndListener(OnProfilerSampleCallback);
function imp(i) {
  d8.profiler.triggerSample();
  console.profileEnd();
}
const wrapped_imp =
    new WebAssembly.Function({parameters: ['i32'], results: ['i32']}, imp);
let instance = new WebAssembly.Instance(wasm_module, {m: {val: wrapped_imp}});
console.profile();
instance.exports.main(3);

assertTrue(sampleCollected);
