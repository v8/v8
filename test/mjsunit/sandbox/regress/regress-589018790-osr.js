// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --sandbox-testing

// Verify that cross-function OSR via a swapped JSFunction::feedback_cell traps
// in OnStackReplacement when the target OSR Code was compiled for a different
// BytecodeArray (even when both loops share the same JumpLoop bytecode offset).

const kJSFunctionType = Sandbox.getInstanceTypeIdFor('JS_FUNCTION_TYPE');
const kFeedbackCellOffset =
    Sandbox.getFieldOffset(kJSFunctionType, 'feedback_cell');
const memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

function osr_target(n) {
  let s = 0;
  for (let i = 0; i < n; i++) {
    if (i === 50) %OptimizeOsr();
    s += i;
  }
  return s;
}

function victim(n) {
  let s = 0;
  for (let i = 0; i < n; i++) {
    if (i === 50) %OptimizeOsr();
    s += i;
  }
  return s + 1;
}

%PrepareFunctionForOptimization(osr_target);
%PrepareFunctionForOptimization(victim);
osr_target(1);
victim(1);

// Compile OSR code for osr_target into osr_target's FeedbackVector.
osr_target(60);

// Swap victim's feedback_cell to osr_target's feedback_cell and re-enter the
// loop so victim attempts to OSR into osr_target's compiled Code.
const osr_target_cell = memory.getUint32(
    Sandbox.getAddressOf(osr_target) + kFeedbackCellOffset, true);
memory.setUint32(
    Sandbox.getAddressOf(victim) + kFeedbackCellOffset, osr_target_cell, true);

victim(100);
