// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-gc
// Flags: --stress-incremental-marking --random-gc-interval=2000

// Regression for https://crbug.com/540352782 (ClusterFuzz TSAN data race).
//
// ClusterFuzz minimized testcase reduced to a Smi-elements Array.prototype.fill
// of a Smi value (originally `var a = [204]; a.fill(832);`) while concurrent
// marking may be scanning the same FixedArray slots. The Smi fast path must
// not use non-atomic MemsetTagged over live element slots during marking.

function assertFilled(array, value, start, end) {
  for (let i = start; i < end; ++i) {
    assertEquals(value, array[i]);
  }
}

// Minimal shape from the ClusterFuzz minimized input.
(function ClusterFuzzSmiFillShape() {
  const a = [204];
  assertTrue(%HasSmiElements(a));
  a.fill(832);
  assertEquals(832, a[0]);
  assertTrue(%HasSmiElements(a));
})();

// Larger packed Smi fill so the bulk Smi path dominates.
(function PackedSmiBulkFill() {
  const a = [];
  for (let i = 0; i < 4096; ++i) a.push(i & 0xffff);
  assertTrue(%HasSmiElements(a));
  assertFalse(%HasHoleyElements(a));
  a.fill(832, 16, 4000);
  assertFilled(a, 832, 16, 4000);
  for (let i = 0; i < 16; ++i) assertEquals(i & 0xffff, a[i]);
  for (let i = 4000; i < a.length; ++i) assertEquals(i & 0xffff, a[i]);
})();

// Overwrite slots that previously held HeapObjects with a Smi. Concurrent
// marking may still be reading those slots while the fill runs.
(function FillSmiOverObjectSlotsUnderMarkingStress() {
  const arrays = [];
  for (let round = 0; round < 40; ++round) {
    const a = [];
    for (let i = 0; i < 2048; ++i) {
      a.push(i % 3 === 0 ? {x: i} : i);
    }
    a.fill(832, 0, 1500);
    a.fill(0, 1000, a.length);
    arrays.push(a);
    if ((round & 3) === 0) gc({type: 'major'});
  }
  for (const a of arrays) {
    assertFilled(a, 832, 0, 1000);
    assertFilled(a, 0, 1000, a.length);
  }
})();

// COW Smi elements must stay isolated after fill (EnsureWritableFastElements).
(function CowSmiFill() {
  function makeCow() {
    return [204, 205, 206, 207];
  }
  const a = makeCow();
  const b = makeCow();
  a.fill(832);
  assertFilled(a, 832, 0, a.length);
  assertArrayEquals([204, 205, 206, 207], b);
})();
