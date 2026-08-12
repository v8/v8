// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let memory = new WebAssembly.Memory({initial: 1, maximum: 5, shared: true});

let builder = new WasmModuleBuilder();
builder.addImportedMemory("mod", "mem", 1, 5, true);

// Function:
// 1. Sets memory[0] = 1 atomically to signal main thread.
// 2. Waits in Loop 1 for memory[0] == 2 (main thread signal).
// 3. Enters 100 straight-line atomic load checks (NO LOOP).
// 4. Reads memory.size into local 0 (updating cached memory size).
// 5. Performs a memory load at byte offset 65536 (page 2) which tests bounds check against updated cached memory size.
// 6. Returns page2_val.
let body = [
  ...wasmI32Const(0),
  ...wasmI32Const(1),
  kAtomicPrefix, kExprI32AtomicStore, 2, 0,

  // Loop 1: Wait for memory[0] == 2
  kExprLoop, kWasmVoid,
    ...wasmI32Const(0),
    kAtomicPrefix, kExprI32AtomicLoad, 2, 0,
    kExprI32Const, 2,
    kExprI32Ne,
    kExprBrIf, 0,
  kExprEnd,
];

// 100 straight-line atomic load checks
// (no loop as stack checks would reload the size via an interrupt).
for (let i = 0; i < 100; i++) {
  body.push(
    ...wasmI32Const(4),
    kAtomicPrefix, kExprI32AtomicLoad, 2, 0,
    ...wasmI32Const(0xABC),
    kExprI32Eq,
    kExprIf, kWasmVoid,
      // 1. Read memory.size (updates cached memory size in instance data)
      kExprMemorySize, 0,
      // 2. Perform memory load at byte offset 65536 (page 2 offset 0)
      // Bounds checking verifies that cached memory size was updated to 2 pages (131072 bytes)
      ...wasmI32Const(65536),
      kAtomicPrefix, kExprI32AtomicLoad, 2, 0,
      kExprReturn,
    kExprEnd
  );
}
body.push(kExprI32Const, 0, kExprI32Const, 0);

builder.addFunction("check_size_and_load_page2", makeSig([], [kWasmI32, kWasmI32]))
  .addBody(body)
  .exportFunc();

let module = new WebAssembly.Module(builder.toBuffer());

function workerCode() {
  onmessage = function({data: {module, memory}}) {
    let instance = new WebAssembly.Instance(module, {mod: {mem: memory}});
    postMessage({status: "ready"});
    let results = instance.exports.check_size_and_load_page2();
    postMessage({status: "done", size: results[0], page2_val: results[1]});
  };
}

let worker = new Worker(workerCode, {type: 'function'});
worker.postMessage({module: module, memory: memory});

let msg1 = worker.getMessage();
assertEquals("ready", msg1.status);

// Wait for worker to enter the Wasm function Loop 1
let i32 = new Int32Array(memory.buffer);
while (Atomics.load(i32, 0) !== 1) {}

// Main thread grows memory first while worker is waiting in Loop 1
memory.grow(1);

// Write payload to memory[4] to signal growth completion
let i32_grown = new Int32Array(memory.buffer);
Atomics.store(i32_grown, 1, 0xABC);

// Write magic payload to byte offset 65536 (index 16384 in Int32Array)
Atomics.store(i32_grown, 16384, 0x778899);

// Release worker from Loop 1 by setting memory[0] = 2
Atomics.store(i32_grown, 0, 2);

let msg2 = worker.getMessage();
assertEquals("done", msg2.status);
// Worker observed memory.size == 2 and successfully loaded 0x778899 from
// page 2 (byte offset 65536)
assertEquals(2, msg2.size);
assertEquals(0x778899, msg2.page2_val);

worker.terminate();
