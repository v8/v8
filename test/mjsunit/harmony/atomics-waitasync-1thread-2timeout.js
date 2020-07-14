// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-sharedarraybuffer --harmony-atomics-waitasync

(function test() {
  const sab = new SharedArrayBuffer(16);
  const i32a = new Int32Array(sab);

  // Create a waiter with a long timeout.
  const result_slow = Atomics.waitAsync(i32a, 0, 0, 200000);
  // Create a waiter with a short timeout.
  const result_fast = Atomics.waitAsync(i32a, 0, 0, 1);

  assertEquals(true, result_slow.async);
  assertEquals(true, result_fast.async);
  assertEquals(2, %AtomicsNumWaitersForTesting(i32a, 0));

  let slow_value = undefined;
  result_slow.value.then(
   (value) => { slow_value = value; },
   () => { assertUnreachable(); });

  let fast_value = undefined;
  result_fast.value.then(
   (value) => { fast_value = value; },
   () => { assertUnreachable(); });

  // Verify that the waiter with the short time out times out.
  let rounds = 1000;
  function continuation1() {
    --rounds;
    assertTrue(rounds > 0);
    if (fast_value == undefined) {
      setTimeout(continuation1, 0);
    } else {
      assertEquals("timed-out", fast_value);
      // Wake up the waiter with the long time out.
      let notify_return_value = Atomics.notify(i32a, 0, 1);
      assertEquals(1, notify_return_value);
      setTimeout(continuation2, 0);
    }
  }
  function continuation2() {
    assertEquals("ok", slow_value);
  }

  setTimeout(continuation1, 0);
})();
