// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --sandbox-testing

// Verify that swapping two JumpLoop OSR cache slots within the same function's
// FeedbackVector (matching BytecodeArray, parameter count, and frame size,
// differing only in osr_offset) traps in OnStackReplacement.

(function() {
  const kJSFunctionType = Sandbox.getInstanceTypeIdFor('JS_FUNCTION_TYPE');
  const kFeedbackCellOffset =
      Sandbox.getFieldOffset(kJSFunctionType, 'feedback_cell');
  const kFeedbackCellValueOffset = Sandbox.getFieldOffset(
      Sandbox.getInstanceTypeIdFor('FEEDBACK_CELL_TYPE'), 'value');
  const kFeedbackVectorDataOffset = Sandbox.getFieldOffset(
      Sandbox.getInstanceTypeIdFor('FEEDBACK_VECTOR_TYPE'), 'data');
  const kFeedbackVectorLengthOffset = Sandbox.getFieldOffset(
      Sandbox.getInstanceTypeIdFor('FEEDBACK_VECTOR_TYPE'), 'length');
  const memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

  function two_loops(mode, n) {
    let sum = 0;
    if (mode === 1) {
      for (let k = 0; k < n; k++) {
        if (k === 50) %OptimizeOsr();
        sum += k;
      }
      return sum;
    }
    for (let i = 0; i < n; i++) {
      if (i === 50) %OptimizeOsr();
      sum += i;
    }
    return sum;
  }

  %PrepareFunctionForOptimization(two_loops);
  two_loops(0, 1);
  two_loops(1, 1);

  const cell = memory.getUint32(
      Sandbox.getAddressOf(two_loops) + kFeedbackCellOffset, true) - 1;
  const fbv = memory.getUint32(cell + kFeedbackCellValueOffset, true) - 1;
  const fbvLen = memory.getInt32(fbv + kFeedbackVectorLengthOffset, true);

  function snapshotSlots() {
    const arr = [];
    for (let i = 0; i < fbvLen; i++) {
      arr.push(memory.getUint32(fbv + kFeedbackVectorDataOffset + i * 4, true));
    }
    return arr;
  }

  const before = snapshotSlots();
  two_loops(1, 60);
  const afterLoop1 = snapshotSlots();
  two_loops(0, 60);
  const afterLoop2 = snapshotSlots();

  let loop1Slot = -1;
  let loop2Slot = -1;
  for (let i = 0; i < fbvLen; i++) {
    if (afterLoop1[i] !== before[i]) loop1Slot = i;
    if (afterLoop2[i] !== afterLoop1[i]) loop2Slot = i;
  }
  if (loop1Slot < 0 || loop2Slot < 0 || loop1Slot === loop2Slot) {
    // OSR is disabled in this variant (e.g., --jitless, --no-opt).
    return;
  }

  // Overwrite loop 1's OSR slot with loop 2's OSR CodeWrapper.
  memory.setUint32(
      fbv + kFeedbackVectorDataOffset + loop1Slot * 4, afterLoop2[loop2Slot],
      true);

  two_loops(1, 100);
})();
