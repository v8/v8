// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

utils.load('test/inspector/debugger/restart-frame/restart-frame-test.js');

const {session, Protocol} =
    InspectorTest.start('Checks that restarting the top frame works with breakpoints');

session.setupScriptMap();

const source = `
function foo() {
  const x = 1;
  const y = 2;
}
//# sourceURL=testRestartFrame.js`;

(async () => {
  await Protocol.Debugger.enable();

  await Protocol.Runtime.evaluate({ expression: source });

  await Protocol.Debugger.setBreakpointByUrl({
    lineNumber: 3,
    url: 'testRestartFrame.js',
  });

  const { callFrames } = await RestartFrameTest.evaluateAndWaitForPause('foo()');
  await RestartFrameTest.restartFrameAndWaitForPause(callFrames, 0);

  Protocol.Debugger.resume();  // Resuming hits the breakpoint again.
  await Protocol.Debugger.oncePaused();
  await Protocol.Debugger.resume();

  InspectorTest.completeTest();
})();
