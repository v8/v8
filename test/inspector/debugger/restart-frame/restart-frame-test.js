// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

RestartFrameTest = {};

RestartFrameTest.evaluateAndWaitForPause = async (expression) => {
  const pausedPromise = Protocol.Debugger.oncePaused();
  const evaluatePromise = Protocol.Runtime.evaluate({ expression });

  const { params: { callFrames } } = await pausedPromise;
  InspectorTest.log('Paused at (after evaluation):');
  await session.logSourceLocation(callFrames[0].location);

  // Ignore the last frame, it's always an anonymous empty frame for the
  // Runtime#evaluate call.
  InspectorTest.log('Pause stack:');
  for (const frame of callFrames.slice(0, -1)) {
    InspectorTest.log(`  ${frame.functionName}:${frame.location.lineNumber} (canBeRestarted = ${frame.canBeRestarted ?? false})`);
  }
  InspectorTest.log('');

  return { callFrames, evaluatePromise };
};

RestartFrameTest.restartFrameAndWaitForPause = async (callFrames, index) => {
  const pausedPromise = Protocol.Debugger.oncePaused();
  const frame = callFrames[index];

  InspectorTest.log(`Restarting function "${frame.functionName}" ...`);
  const response = await Protocol.Debugger.restartFrame({ callFrameId: frame.callFrameId, mode: 'StepInto' });
  if (response.error) {
    InspectorTest.log(`Failed to restart function "${frame.functionName}":`);
    InspectorTest.logMessage(response.error);
    return;
  }

  const { params: { callFrames: pausedCallFrames } } = await pausedPromise;
  InspectorTest.log('Paused at (after restart):');
  await session.logSourceLocation(pausedCallFrames[0].location);

  return callFrames;
};
