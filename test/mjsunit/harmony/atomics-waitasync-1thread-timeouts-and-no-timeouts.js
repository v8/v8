// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-sharedarraybuffer --harmony-atomics-waitasync

(function test() {
  const sab = new SharedArrayBuffer(16);
  const i32a = new Int32Array(sab);

  const N = 10;
  let log = [];

  // Create N async waiters; the even ones without timeout and the odd ones
  // with timeout.
  for (let i = 0; i < N; ++i) {
    let result;
    if (i % 2 == 0) {
      result = Atomics.waitAsync(i32a, 0, 0);
    } else {
      result = Atomics.waitAsync(i32a, 0, 0, i);
    }
    assertEquals(true, result.async);
    result.value.then(
      (value) => { log.push(value + " " + i); },
      () => { assertUnreachable(); });
  }
  assertEquals(N, %AtomicsNumWaitersForTesting(i32a, 0));
  assertEquals(0, %AtomicsNumUnresolvedAsyncPromisesForTesting(i32a, 0));

  // Wait until the timed out waiters time out.
  let rounds = 10000;
  let previous_length = 0;
  function wait() {
    --rounds;
    assertTrue(rounds > 0);
    if (log.length > previous_length) {
        // Made progress. Give the test more time.
        previous_length = log.length;
        rounds = 10000;
    }
    if (log.length < N / 2) {
      setTimeout(wait, 0);
    } else {
      continuation1();
    }
  }
  setTimeout(wait, 0);

  function continuation1() {
    // Verify that all timed out waiters timed out in FIFO order.
    assertEquals(N / 2, log.length);
    let waiter_no = 1;
    for (let i = 0; i < N / 2; ++i) {
      assertEquals("timed-out " + waiter_no, log[i]);
      waiter_no += 2;
    }
    // Wake up all waiters
    let notify_return_value = Atomics.notify(i32a, 0);
    assertEquals(N / 2, notify_return_value);
    assertEquals(0, %AtomicsNumWaitersForTesting(i32a, 0));
    assertEquals(N / 2, %AtomicsNumUnresolvedAsyncPromisesForTesting(i32a, 0));
    setTimeout(continuation2, 0);
  }

  function continuation2() {
    // Verify that the waiters woke up in FIFO order.
    assertEquals(N, log.length);
    let waiter_no = 0;
    for (let i = N / 2; i < N; ++i) {
      assertEquals("ok " + waiter_no, log[i]);
      waiter_no += 2;
    }
  }
})();
