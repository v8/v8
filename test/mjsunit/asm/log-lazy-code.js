// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --log-function-events

let sampleCollected = false;

function OnProfilerSampleCallback(profile) {
  profile = profile.replaceAll('\\', '/');
  profile = JSON.parse(profile);
  let functionNames = profile.nodes.map(n => n.callFrame.functionName);
  for (let i = 0; i < functionNames.length; ++i) {
    // Sampling the stack isn't entirely reliable: about 1 in 30K runs have
    // a bogus frame somewhere early on. While that may not be WAI, it's not
    // this test's purpose to worry about it; it seems that we'll always find
    // the f -> wasm-to-js -> g sequence somewhere on the stack, which is what
    // this test cares about.
    // In theory, the frame before "f" should always be "js-to-wasm:i:i", but
    // that isn't always the case.
    if (functionNames[i] == 'g') {
      assertTrue(functionNames[i - 1] == 'wasm-to-js');
      assertTrue(functionNames[i - 2] == 'f');
      // {sampleCollected} is set at the end because the asserts above don't
      // show up in the test runner, probably because this function is called as
      // a callback from d8.
      sampleCollected = true;
      return;
    }
  }
  assertUnreachable();
}
d8.profiler.setOnProfileEndListener(OnProfilerSampleCallback);

function Asm(stdlib, imports, buffer) {
  "use asm";
  var g = imports.g;

  function f(i) {
    i = i|0;
    return g() | 0;
  }

  return { f: f };
}

var heap = new ArrayBuffer(64*1024);

function g() {
  d8.profiler.triggerSample();
  console.profileEnd();
  return 42;
}
var asm = Asm(this, {g: g}, heap);
assertTrue(%IsAsmWasmCode(Asm));

console.profile();
asm.f(3);
assertTrue(sampleCollected);
