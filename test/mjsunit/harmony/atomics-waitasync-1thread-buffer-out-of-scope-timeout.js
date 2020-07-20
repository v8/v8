// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-sharedarraybuffer --harmony-atomics-waitasync --expose-gc --no-stress-opt --noincremental-marking

(function test() {
  let timed_out = false;

  (function() {
    const sab = new SharedArrayBuffer(16);
    const i32a = new Int32Array(sab);
    // Create a waiter with a timeout.
    const result = Atomics.waitAsync(i32a, 0, 0, 1);
    result.value.then(
      (value) => { assertEquals('timed-out', value); timed_out = true; },
      () => { assertUnreachable(); });
    })();
  // Make sure sab, ia32 and result get gc()d.
  gc();

  assertEquals(1, %AtomicsNumAsyncWaitersForTesting());

  // Even if the buffer went out of scope, we keep the waitAsync alive so that it can still time out.
  let resolved = false;
  const sab2 = new SharedArrayBuffer(16);
  const i32a2 = new Int32Array(sab2);
  const result2 = Atomics.waitAsync(i32a2, 0, 0);
  result2.value.then(
    (value) => { assertEquals("ok", value); resolved = true; },
    () => { assertUnreachable(); });
  assertEquals(2, %AtomicsNumAsyncWaitersForTesting());

  const notify_return_value = Atomics.notify(i32a2, 0);
  assertEquals(1, notify_return_value);
  assertEquals(1, %AtomicsNumAsyncWaitersForTesting());

  // Verify that the waiter gets woken up.
  let rounds = 1000;
  function wait() {
    --rounds;
    assertTrue(rounds > 0);
    if (!timed_out) {
      setTimeout(wait, 0);
    }
  }
  setTimeout(wait, 0);
})();
