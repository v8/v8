// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

utils.load('test/inspector/debugger/restart-frame/restart-frame-test.js');

const {session, contextGroup, Protocol} =
  InspectorTest.start('Checks that restart frame fails when embedder frames would be unwound');

session.setupScriptMap();

contextGroup.addScript(`
function breaker() {
  debugger;
}
function entrypoint() {
  inspector.callbackForTests(breaker);
}
`, 0, 0, 'test.js');

(async () => {
  await Protocol.Debugger.enable();

  const { callFrames } = await RestartFrameTest.evaluateAndWaitForPause('entrypoint()');

  // Restart the `entrypoint` frame. Inbetween is the C++ API method `callbackForTests`.
  const restartFrameIndex = 1;  // 0 is `breaker`, 1 is `entrypoint`.
  await RestartFrameTest.restartFrameAndWaitForPause(callFrames, restartFrameIndex);

  InspectorTest.completeTest();
})();
