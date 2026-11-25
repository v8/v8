// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// --no-verify-heap is needed because swapping `victim`'s feedback_cell to
// `osr_target`'s intentionally violates JSFunctionVerify's check that a
// JSFunction and its FeedbackCell share the same JSDispatchHandle.
// Flags: --allow-natives-syntax --sandbox-testing --expose-gc --no-verify-heap

const kJSFunctionType = Sandbox.getInstanceTypeIdFor('JS_FUNCTION_TYPE');
const kFeedbackCellOffset =
    Sandbox.getFieldOffset(kJSFunctionType, 'feedback_cell');
const memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

// Value of StackFrame::INTERNAL in StackFrame::Type. Pre-fix, storing Smi(13)
// into a JS frame's context slot matched TypeToMarker(StackFrame::INTERNAL),
// causing the stack walker to misclassify a Maglev frame as INTERNAL and scan
// raw unboxed double spill slots as tagged pointers during GC.
const kInternalFrameMarker = 13;

// Unboxed double whose bit pattern looks like an out-of-sandbox tagged pointer.
const u64 = new BigUint64Array([0x0000700012345679n]);
const crafted_float = new Float64Array(u64.buffer)[0];

let should_gc = false;
let gc_ran = false;
function trigger_gc() {
  if (should_gc) {
    gc_ran = true;
    gc();
  }
  return 2.0;
}
%NeverOptimizeFunction(trigger_gc);

// Compiled with OSR inside a block scope that saves/restores the outer context
// in register r3 (PushContext r3 / PopContext r3). When `victim` OSR-enters
// this code via the swapped feedback cell, PopContext loads `victim`'s r3
// (Smi(kInternalFrameMarker)) into `cp`, which is then captured as the context
// of `spill_doubles` and pushed into `spill_doubles`'s Maglev frame.
function osr_target(iterations, payload) {
  {
    let block_var = 1;
    let keep = () => block_var;
    for (let i = 0; i < iterations; i++) {
      if (i === 50) %OptimizeOsr();
    }
  }
  let spill_doubles = function(p) {
    let v1 = p.f1 + p.crafted_float;
    let v2 = p.f2 + 2.0;
    let v3 = p.f3 + 4.0;
    let v4 = p.f4 + 6.0;
    let v5 = p.f5 + 8.0;
    let v6 = p.f6 + 10.0;
    let v7 = p.f7 + 12.0;
    let v8 = p.f8 + 14.0;
    let v9 = p.f9 + 16.0;
    let v10 = p.f10 + 18.0;
    let v11 = p.f11 + 20.0;
    let v12 = p.f12 + 22.0;
    let v13 = p.f13 + 24.0;
    let v14 = p.f14 + 26.0;
    let v15 = p.f15 + 28.0;
    let num = p.trigger_gc();
    return v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12 +
        v13 + v14 + v15 + num;
  };
  if (payload.call_inner) {
    return spill_doubles.call(null, payload);
  }
  return spill_doubles;
}

// Same parameter count and register layout as `osr_target` so OSR entry reads
// `fake_context` (r3) as `osr_target`'s saved outer context and `loop_count`
// (r2) as its loop counter.
function victim(iterations, payload) {
  let pad0 = 0;
  let pad1 = 0;
  let loop_count = iterations;
  let fake_context = 13;  // kInternalFrameMarker (StackFrame::INTERNAL)
  for (let k = 0; k < 10; k++) {}
  return pad0 + pad1 + loop_count + fake_context + payload.f1;
}

const payload = {
  f1: 0.0, f2: 2.0, f3: 4.0, f4: 6.0, f5: 8.0,
  f6: 10.0, f7: 12.0, f8: 14.0, f9: 16.0, f10: 18.0,
  f11: 20.0, f12: 22.0, f13: 24.0, f14: 26.0, f15: 28.0,
  crafted_float,
  trigger_gc,
  call_inner: false,
};

%PrepareFunctionForOptimization(osr_target);
%PrepareFunctionForOptimization(victim);

const spill_doubles_ref = osr_target(1, payload);
%PrepareFunctionForOptimization(spill_doubles_ref);
payload.call_inner = true;
for (let i = 0; i < 10; i++) {
  osr_target(1, payload);
}
%OptimizeMaglevOnNextCall(spill_doubles_ref);
osr_target(1, payload);

payload.call_inner = false;
for (let i = 0; i < 5; i++) {
  victim(1, payload);
}

// Compile OSR code for `osr_target` and store it in `osr_target`'s
// FeedbackVector (attached to its FeedbackCell).
payload.call_inner = true;
osr_target(60, payload);

// Corrupt `victim`'s feedback_cell pointer to point to `osr_target`'s
// FeedbackCell. On the next JumpLoop in `victim`, Ignition looks up OSR code in
// the swapped FeedbackVector and OSR-enters `osr_target`'s compiled code.
const osr_target_cell = memory.getUint32(
    Sandbox.getAddressOf(osr_target) + kFeedbackCellOffset, true);
memory.setUint32(
    Sandbox.getAddressOf(victim) + kFeedbackCellOffset, osr_target_cell, true);

should_gc = true;
victim(100, payload);
if (!gc_ran) {
  throw new Error('Expected OSR into osr_target to reach gc()');
}
