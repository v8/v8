// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-sharedarraybuffer --harmony-atomics-waitasync --expose-gc --no-stress-opt --noincremental-marking

(function test() {
  (function() {
    const sab = new SharedArrayBuffer(16);
    const i32a = new Int32Array(sab);
    const result = Atomics.waitAsync(i32a, 0, 0);
    result.value.then(
      (value) => { assertUnreachable(); },
      () => { assertUnreachable(); });
    })();
  // Make sure sab, ia32 and result get gc()d.
  gc();

  assertEquals(1, %AtomicsNumAsyncWaitersForTesting());

  // The next time iterate the waiter list, we clean up the waiter which can
  // never be woken up.
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
  assertEquals(0, %AtomicsNumAsyncWaitersForTesting());

  setTimeout(()=> {
    assertTrue(resolved);
  }, 0);
})();
