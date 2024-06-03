// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

'use strict';

if (this.Worker) {
  (function TestMutexWorkersLockedTerminated() {
    let workerScript = `onmessage = function(msg) {
         let mutex = msg.mutex;
         let count = 0;
         Atomics.Mutex.lock(mutex, function() {
          while(true) {
            if(count === 0) {
              postMessage("looping");
            }
            count++;
          }
         });
       };
       postMessage("started");`;

    let worker1 = new Worker(workerScript, {type: 'string'});
    assertEquals('started', worker1.getMessage());

    let mutex = new Atomics.Mutex();
    let msg = {mutex};
    worker1.postMessage(msg);
    assertEquals('looping', worker1.getMessage());
    worker1.terminate();

    // Terminating the worker while the mutex is locked should interrupt the
    // execution and terminate the lock
    while (!Atomics.Mutex.tryLock(mutex, function() {}).success) {
    }
  })();

  (function TestMutexWorkersWaitingTerminated() {
    let workerScript = `onmessage = function(msg) {
         let {mutex, cv, cv_mutex} = msg;
         let count = 0;
         Atomics.Mutex.lock(mutex, function() {
          Atomics.Mutex.lock(cv_mutex, function() {
            Atomics.Condition.wait(cv, cv_mutex);
          });
         });
       };
       postMessage("started");`;

    let worker1 = new Worker(workerScript, {type: 'string'});
    let worker2 = new Worker(workerScript, {type: 'string'});
    assertEquals('started', worker1.getMessage());
    assertEquals('started', worker2.getMessage());

    let mutex = new Atomics.Mutex;
    let cv = new Atomics.Condition;
    let cv_mutex = new Atomics.Mutex;
    let msg = {mutex, cv, cv_mutex};
    worker1.postMessage(msg);

    while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv) !== 1) {
    }

    worker2.postMessage(msg);
    while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex) !== 1) {
    }
    worker2.terminate();
    // The worker was woken up by the terminate call and the waiters were
    // removed from the queue.
    while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex) !== 0) {
    }
    Atomics.Condition.notify(cv, 1);
    while (!Atomics.Mutex.tryLock(mutex, function() {}).success) {
    }
    worker1.terminate();
  })();
}
