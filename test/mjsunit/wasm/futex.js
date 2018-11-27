// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-sharedarraybuffer
// Flags: --experimental-wasm-threads

'use strict';

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

function WasmAtomicWakeFunction(memory, offset, index, num) {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 0, 20, "shared");
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprGetLocal, 0,
      kExprGetLocal, 1,
      kAtomicPrefix,
      kExprAtomicWake, /* alignment */ 0, offset])
    .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  return instance.exports.main(index, num);
}

function WasmI32AtomicWaitFunction(memory, offset, index, val, timeout) {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 0, 20, "shared");
  builder.addFunction("main",
    makeSig([kWasmI32, kWasmI32, kWasmF64], [kWasmI32]))
    .addBody([
      kExprGetLocal, 0,
      kExprGetLocal, 1,
      kExprGetLocal, 2,
      kExprI64SConvertF64,
      kAtomicPrefix,
      kExprI32AtomicWait, /* alignment */ 0, offset])
      .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  return instance.exports.main(index, val, timeout);
}

function WasmI64AtomicWaitFunction(memory, offset, index, val_low,
                                   val_high, timeout) {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 0, 20, "shared");
  // Wrapper for I64AtomicWait that takes two I32 values and combines to into
  // I64 for the instruction parameter.
  builder.addFunction("main",
    makeSig([kWasmI32, kWasmI32, kWasmI32, kWasmF64], [kWasmI32]))
    .addLocals({i64_count: 1}) // local that is passed as value param to wait
    .addBody([
      kExprGetLocal, 1,
      kExprI64UConvertI32,
      kExprI64Const, 32,
      kExprI64Shl,
      kExprGetLocal, 2,
      kExprI64UConvertI32,
      kExprI64Ior,
      kExprSetLocal, 4, // Store the created I64 value in local
      kExprGetLocal, 0,
      kExprGetLocal, 4,
      kExprGetLocal, 3,
      kExprI64SConvertF64,
      kAtomicPrefix,
      kExprI64AtomicWait, /* alignment */ 0, offset])
      .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  return instance.exports.main(index, val_high, val_low, timeout);
}

(function TestInvalidIndex() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});

  // Valid indexes are 0-65535 (1 page).
  [-2, 65536, 0xffffffff].forEach(function(invalidIndex) {
    assertThrows(function() {
      WasmAtomicWakeFunction(memory, 0, invalidIndex, -1);
    }, Error);
    assertThrows(function() {
      WasmI32AtomicWaitFunction(memory, 0, invalidIndex, 0, -1);
    }, Error);
    assertThrows(function() {
      WasmI64AtomicWaitFunction(memory, 0, invalidIndex, 0, 0, -1);
    }, Error);
    assertThrows(function() {
      WasmAtomicWakeFunction(memory, invalidIndex, 0, -1);
    }, Error);
    assertThrows(function() {
      WasmI32AtomicWaitFunction(memory, invalidIndex, 0, 0, -1);
    }, Error);
    assertThrows(function() {
      WasmI64AtomicWaitFunction(memory, invalidIndex, 0, 0, 0, -1);
    }, Error);
    assertThrows(function() {
      WasmAtomicWakeFunction(memory, invalidIndex/2, invalidIndex/2, -1);
    }, Error);
    assertThrows(function() {
      WasmI32AtomicWaitFunction(memory, invalidIndex/2, invalidIndex/2, 0, -1);
    }, Error);
    assertThrows(function() {
      WasmI64AtomicWaitFunction(memory, invalidIndex/2, invalidIndex/2, 0, 0, -1);
    }, Error);
  });
})();

(function TestI32WaitTimeout() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});
  var waitMs = 100;
  var startTime = new Date();
  assertEquals(2, WasmI32AtomicWaitFunction(memory, 0, 0, 0, waitMs*1000000));
  var endTime = new Date();
  assertTrue(endTime - startTime >= waitMs);
})();

(function TestI64WaitTimeout() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});
  var waitMs = 100;
  var startTime = new Date();
  assertEquals(2, WasmI64AtomicWaitFunction(memory, 0, 0, 0, 0, waitMs*1000000));
  var endTime = new Date();
  assertTrue(endTime - startTime >= waitMs);
})();

(function TestI32WaitNotEqual() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});
  assertEquals(1, WasmI32AtomicWaitFunction(memory, 0, 0, 42, -1));

  assertEquals(2, WasmI32AtomicWaitFunction(memory, 0, 0, 0, 0));

  let i32a = new Int32Array(memory.buffer);
  i32a[0] = 1;
  assertEquals(1, WasmI32AtomicWaitFunction(memory, 0, 0, 0, -1));
  assertEquals(2, WasmI32AtomicWaitFunction(memory, 0, 0, 1, 0));
})();

(function TestI64WaitNotEqual() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});
  assertEquals(1, WasmI64AtomicWaitFunction(memory, 0, 0, 42, 0, -1));

  assertEquals(2, WasmI64AtomicWaitFunction(memory, 0, 0, 0, 0, 0));

  let i32a = new Int32Array(memory.buffer);
  i32a[0] = 1;
  i32a[1] = 2;
  assertEquals(1, WasmI64AtomicWaitFunction(memory, 0, 0, 0, 0, -1));
  assertEquals(2, WasmI64AtomicWaitFunction(memory, 0, 0, 1, 2, 0));
})();

(function TestWakeCounts() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});

  [-1, 0, 4, 100, 0xffffffff].forEach(function(count) {
    WasmAtomicWakeFunction(memory, 0, 0, count);
  });
})();

//// WORKER ONLY TESTS

if (this.Worker) {
  let wasm_wake_adapter = (memory, offset, index, count) => {
    return WasmAtomicWakeFunction(memory, offset, index, count);
  };

  let js_wake_adapter = (memory, offset, index, count) => {
    let i32a = new Int32Array(memory.buffer, offset);
    return Atomics.wake(i32a, index>>>2, count);
  };

  // Wait adapter strings that can be passed as a parameter to TestWaitWake to generate
  // custom worker script
  let js_wait_adapter = `(memory, offset, index) => {
    let i32a = new Int32Array(memory.buffer, offset);
    let res = Atomics.wait(i32a, index>>>2, 0);
    if (res == "ok") return 0;
    if (res == "not-equal") return 1;
    return 2;
  }`

  let wasm_i32wait_adapter = `(memory, offset, index) => {
    load("test/mjsunit/wasm/wasm-constants.js");
    load("test/mjsunit/wasm/wasm-module-builder.js");
    ${WasmI32AtomicWaitFunction.toString()}
    return WasmI32AtomicWaitFunction(memory, offset, index, 0, -1);
  }`

  let wasm_i64wait_adapter = `(memory, offset, index) => {
    load("test/mjsunit/wasm/wasm-constants.js");
    load("test/mjsunit/wasm/wasm-module-builder.js");
    ${WasmI64AtomicWaitFunction.toString()}
    return WasmI64AtomicWaitFunction(memory, offset, index, 0, 0, -1);
  }`

  let TestWaitWake = function(wait_adapter, wake_adapter, num_workers, num_workers_wake) {
    let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});
    let i32a = new Int32Array(memory.buffer);
    let max_workers = 8;
    assertTrue(num_workers <= max_workers);
    // all workers wait on this index. max_workers*8 ensures that this can work for I64Wait too.
    let wait_index = 8*max_workers;
    let wait_index_i32a = wait_index>>>2;

    // SAB values:
    // memory[id*4], where id in range [0, num_workers]:
    //   0 => Worker |id| is still waiting on the futex
    //   1 => Worker |id| is not waiting on futex, but has not be reaped by the
    //        main thread.
    //   2 => Worker |id| has been reaped.
    //
    // memory[wait_index]:
    //   always 0. Each worker is waiting on this index.

    let workerScript =
      `onmessage = function(msg) {
         let id = msg.id;
         let memory = msg.memory;
         let i32a = new Int32Array(memory.buffer);
         let wait_adapter = eval(msg.wait_adapter);
         let result = wait_adapter(memory, 0, ${wait_index});
         // Set i32a[id] to 1 to notify the main thread which workers were
         // woken up.
         Atomics.store(i32a, id, 1);
         postMessage(result);
       };`;

    let workers = [];
    for (let id = 0; id < num_workers; id++) {
      workers[id] = new Worker(workerScript, {type: 'string'});
      workers[id].postMessage({id, memory, wait_adapter});
    }

    // Spin until all workers are waiting on the futex.
    while (%AtomicsNumWaitersForTesting(i32a, wait_index_i32a) != num_workers) {}

    if (num_workers_wake < num_workers) {
      assertEquals(num_workers_wake, wake_adapter(memory, 0, wait_index, num_workers_wake));
    } else {
      assertEquals(num_workers, wake_adapter(memory, 0, wait_index, num_workers_wake));
      num_workers_wake = num_workers;
    }

    let wokenCount = 0;
    while (wokenCount < num_workers_wake) {
      for (let id = 0; id < num_workers; id++) {
        // Look for workers that have not yet been reaped. Set i32a[id] to 2
        // when they've been processed so we don't look at them again.
        if (Atomics.compareExchange(i32a, id, 1, 2) == 1) {
          assertEquals(0, workers[id].getMessage());
          workers[id].terminate();
          wokenCount++;
        }
      }
    }

    assertEquals(num_workers - num_workers_wake,
                 %AtomicsNumWaitersForTesting(i32a, wait_index_i32a));

    // Finally wake and kill all workers.
    wake_adapter(memory, 0, wait_index, num_workers)
    for (let id = 0; id < num_workers; id++) {
      workers[id].terminate();
    }
  };

  TestWaitWake(js_wait_adapter, wasm_wake_adapter, 1, 1);
  TestWaitWake(js_wait_adapter, wasm_wake_adapter, 4, 4);
  TestWaitWake(js_wait_adapter, wasm_wake_adapter, 4, 3);
  TestWaitWake(js_wait_adapter, wasm_wake_adapter, 3, 4);

  TestWaitWake(wasm_i32wait_adapter, wasm_wake_adapter, 1, 1);
  TestWaitWake(wasm_i32wait_adapter, wasm_wake_adapter, 4, 4);
  TestWaitWake(wasm_i32wait_adapter, wasm_wake_adapter, 4, 3);
  TestWaitWake(wasm_i32wait_adapter, wasm_wake_adapter, 3, 4);

  TestWaitWake(wasm_i32wait_adapter, js_wake_adapter, 1, 1);
  TestWaitWake(wasm_i32wait_adapter, js_wake_adapter, 4, 4);
  TestWaitWake(wasm_i32wait_adapter, js_wake_adapter, 4, 3);
  TestWaitWake(wasm_i32wait_adapter, js_wake_adapter, 3, 4);

  TestWaitWake(wasm_i64wait_adapter, wasm_wake_adapter, 1, 1);
  TestWaitWake(wasm_i64wait_adapter, wasm_wake_adapter, 4, 4);
  TestWaitWake(wasm_i64wait_adapter, wasm_wake_adapter, 4, 3);
  TestWaitWake(wasm_i64wait_adapter, wasm_wake_adapter, 3, 4);

  TestWaitWake(wasm_i64wait_adapter, js_wake_adapter, 1, 1);
  TestWaitWake(wasm_i64wait_adapter, js_wake_adapter, 4, 4);
  TestWaitWake(wasm_i64wait_adapter, js_wake_adapter, 4, 3);
  TestWaitWake(wasm_i64wait_adapter, js_wake_adapter, 3, 4);
}
