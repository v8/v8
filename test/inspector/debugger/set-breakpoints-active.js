// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, Protocol} =
    InspectorTest.start('Tests for set-breakpoints-active');

Protocol.Debugger.enable();
Protocol.Debugger.onPaused(({params}) => {
  InspectorTest.log(`Paused. (reason: ${params.reason})`);
  Protocol.Debugger.resume();
});

InspectorTest.runAsyncTestSuite([
  async function testDeactivatedBreakpointsAfterReconnect() {
    await Protocol.Debugger.setBreakpointsActive({active: true});
    await Protocol.Runtime.evaluate({expression: 'debugger'});
    await Protocol.Debugger.setBreakpointsActive({active: false});
    session.reconnect();
    InspectorTest.log('Reconnected.');
    await Protocol.Runtime.evaluate({expression: 'debugger'});
    InspectorTest.log('Debugger break executed.');
  },
  async function testDeactivatedBreakpointsAfterDisableEnable() {
    await Protocol.Debugger.setBreakpointsActive({active: true});
    await Protocol.Runtime.evaluate({expression: 'debugger'});
    await Protocol.Debugger.setBreakpointsActive({active: false});
    await Protocol.Debugger.disable();
    InspectorTest.log('Disabled.');
    await Protocol.Debugger.enable();
    InspectorTest.log('Enabled.');
    await Protocol.Runtime.evaluate({expression: 'debugger'});
    InspectorTest.log('Debugger break executed.');
  }
]);
