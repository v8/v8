// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-test-streaming

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addImport('a', 'x', kSig_v_v);
const wire_bytes = builder.toBuffer();

assertPromiseResult((async function TestInstantiateStreamingDeferred() {
  const events = [];
  const promise = WebAssembly.instantiateStreaming(wire_bytes, {
    a: {
      get x() {
        events.push('import_getter');
        return () => {};
      },
    },
  });
  Promise.resolve().then(() => events.push('microtask_after_call'));
  await promise;
  assertEquals(['microtask_after_call', 'import_getter'], events);
})());
