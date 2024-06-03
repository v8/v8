// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

'use strict';

if (this.Worker) {
  (function TestConditionWaitTerminated() {
    let workerScript = `onmessage = function(msg) {
         let mutex = msg.mutex;
         let cv = msg.cv;
         let res = Atomics.Mutex.lock(mutex, function() {
           return Atomics.Condition.wait(cv, mutex);
         });
         postMessage(res);
       };
       postMessage('started')`;

    let mutex = new Atomics.Mutex;
    let cv = new Atomics.Condition;
    let msg = {mutex, cv};

    let worker = new Worker(workerScript, {type: 'string'});
    assertEquals('started', worker.getMessage());
    worker.postMessage(msg);

    // Spin until the worker is waiting.
    while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv) != 1) {
    }

    worker.terminate();

    // Terminating the worker while the condition variable is waiting should
    // notify the condition variable and remove the waiter from the queue.
    while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv) != 0) {
    }
  })();
}
