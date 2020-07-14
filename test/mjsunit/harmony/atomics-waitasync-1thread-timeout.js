// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-sharedarraybuffer --harmony-atomics-waitasync

(function test() {
  const sab = new SharedArrayBuffer(16);
  const i32a = new Int32Array(sab);

  // Create a waiter with a timeout.
  const result = Atomics.waitAsync(i32a, 0, 0, 1);

  assertEquals(true, result.async);
  assertEquals(1, %AtomicsNumWaitersForTesting(i32a, 0));

  let resolved = false;
  result.value.then(
   (value) => { assertEquals("timed-out", value); resolved = true; },
   () => { assertUnreachable(); });

  // Verify that the waiter gets woken up.
  let rounds = 1000;
  function wait() {
    --rounds;
    assertTrue(rounds > 0);
    if (!resolved) {
      setTimeout(wait, 0);
    }
  }
  setTimeout(wait, 0);
})();
