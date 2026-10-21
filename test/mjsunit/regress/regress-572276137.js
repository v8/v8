// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --enable-inspector --allow-natives-syntax

let inspector_enabled = false;
let should_trigger = false;
let current_trigger = null;
let paused_count = 0;

function receive(message) {
  let msg = JSON.parse(message);
  if (msg.id === 1) {
    inspector_enabled = true;
  } else if (msg.method === 'Debugger.paused') {
    paused_count++;
    if (should_trigger) {
      should_trigger = false;
      const target_frame =
          msg.params.callFrames.find(f => f.functionName === 'target');
      send(JSON.stringify({
        id: 100 + paused_count,
        method: 'Debugger.restartFrame',
        params: {callFrameId: target_frame.callFrameId, mode: 'StepInto'}
      }));
    } else {
      send(JSON.stringify({id: 200 + paused_count, method: 'Debugger.resume'}));
    }
  }
}

function target() {
  if (should_trigger) {
    current_trigger();
  }
  return 42;
}

send(JSON.stringify({id: 1, method: 'Debugger.enable'}));
if (!inspector_enabled) {
  // --fuzzing disables --enable-inspector, making send() a no-op.
  quit();
}

send(JSON.stringify({
  id: 2,
  method: 'Debugger.setPauseOnExceptions',
  params: {state: 'all'}
}));

const triggers = [
  () => { throw new Error('boom'); },
  () => { Promise.reject(new Error('reject')).catch(() => {}); },
  () => { new Promise((_, reject) => reject(new Error('reject2'))).catch(() => {}); },
  () => { console.assert(false); },
  () => { %ScheduleBreak(); %ScheduleBreak(); (() => {})(); },
];

for (const trigger of triggers) {
  current_trigger = trigger;

  // Test restartFrame from an interpreted frame.
  should_trigger = true;
  assertEquals(42, target());

  // Test restartFrame from an optimized frame (exercising lazy deopt + unwind).
  %PrepareFunctionForOptimization(target);
  should_trigger = false;
  assertEquals(42, target());
  %OptimizeFunctionOnNextCall(target);
  should_trigger = true;
  assertEquals(42, target());
}

assertEquals(triggers.length * 4, paused_count);
