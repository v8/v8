// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-threads

(function TestPostMessageUnsharedMemory() {
  let worker = new Worker('');
  let memory = new WebAssembly.Memory({initial: 1, maximum: 2});

  assertThrows(() => worker.postMessage(memory), Error);
})();

(function TestPostMessageSharedMemory() {
  let workerScript =
    `onmessage = function(memory) {
       // Can't use assert in a worker.
       if (!(memory instanceof WebAssembly.Memory)) {
         postMessage("Error: memory is not a WebAssembly.Memory");
         return;
       }

       if (!(memory.buffer instanceof SharedArrayBuffer)) {
         postMessage("Error: memory.buffer is not a SharedArrayBuffer");
         return;
       }

       if (memory.buffer.byteLength != 65536) {
         postMessage("Error: memory.buffer.byteLength is not 1 page");
         return;
       }

       postMessage("OK");
     };`;
  let worker = new Worker(workerScript);
  let memory = new WebAssembly.Memory({initial: 1, maximum: 2, shared: true});
  worker.postMessage(memory);
  assertEquals("OK", worker.getMessage());
  worker.terminate();
})();
