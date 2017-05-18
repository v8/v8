// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.log('Checks Debugger.pause');

InspectorTest.setupScriptMap();
Protocol.Debugger.enable();
InspectorTest.runAsyncTestSuite([
  async function testPause() {
    Protocol.Debugger.pause();
    Protocol.Runtime.evaluate({expression: 'var a = 42;'});
    await waitPauseAndDumpLocation();
    await Protocol.Debugger.resume();
  },

  async function testSkipFrameworks() {
    Protocol.Debugger.setBlackboxPatterns({patterns: ['framework\.js']});
    Protocol.Debugger.pause();
    Protocol.Runtime.evaluate({expression: 'var a = 42; //# sourceURL=framework.js'});
    Protocol.Runtime.evaluate({expression: 'var a = 239;'});
    await waitPauseAndDumpLocation();
    await Protocol.Debugger.resume();
  },

  async function testSkipOtherContext1() {
    let contextGroup = InspectorTest.createContextGroup();
    let session = InspectorTest.createSession(contextGroup);
    session.Protocol.Debugger.enable({});
    Protocol.Debugger.pause();
    Protocol.Runtime.evaluate({expression: 'var a = 42; //# sourceURL=framework.js'});
    session.Protocol.Runtime.evaluate({expression: 'var a = 239;'});
    Protocol.Runtime.evaluate({expression: 'var a = 1;'});
    await waitPauseAndDumpLocation();
    await Protocol.Debugger.resume();
    await session.Protocol.Debugger.disable({});
  },

  async function testSkipOtherContext2() {
    let contextGroup = InspectorTest.createContextGroup();
    let session = InspectorTest.createSession(contextGroup);
    InspectorTest.setupScriptMap(session);
    session.Protocol.Debugger.enable({});
    session.Protocol.Debugger.pause({});
    Protocol.Runtime.evaluate({expression: 'var a = 42; //# sourceURL=framework.js'});
    session.Protocol.Runtime.evaluate({expression: 'var a = 239;'});
    Protocol.Runtime.evaluate({expression: 'var a = 1;'});
    await waitPauseAndDumpLocation(session);
    // should not resume pause from different context group id.
    Protocol.Debugger.resume();
    session.Protocol.Debugger.stepOver({});
    await waitPauseAndDumpLocation(session);
    await session.Protocol.Debugger.resume({});
    await session.Protocol.Debugger.disable({});
  },

  async function testWithNativeBreakpoint() {
    InspectorTest.contextGroup.schedulePauseOnNextStatement('', '');
    await Protocol.Debugger.pause();
    InspectorTest.contextGroup.cancelPauseOnNextStatement();
    Protocol.Runtime.evaluate({expression: 'var a = 42;'});
    await waitPauseAndDumpLocation();
    await Protocol.Debugger.resume();

    await Protocol.Debugger.pause();
    InspectorTest.contextGroup.schedulePauseOnNextStatement('', '');
    InspectorTest.contextGroup.cancelPauseOnNextStatement();
    Protocol.Runtime.evaluate({expression: 'var a = 42;'});
    await waitPauseAndDumpLocation();
    await Protocol.Debugger.resume();

    InspectorTest.contextGroup.schedulePauseOnNextStatement('', '');
    InspectorTest.contextGroup.cancelPauseOnNextStatement();
    await Protocol.Debugger.pause();
    Protocol.Runtime.evaluate({expression: 'var a = 42;'});
    await waitPauseAndDumpLocation();
    await Protocol.Debugger.resume();
  },

  async function testDisableBreaksShouldCancelPause() {
    await Protocol.Debugger.pause();
    await Protocol.Debugger.setBreakpointsActive({active: false});
    Protocol.Runtime.evaluate({expression: 'var a = 42;'})
      .then(() => Protocol.Debugger.setBreakpointsActive({active: true}))
      .then(() => Protocol.Runtime.evaluate({expression: 'debugger'}));
    await waitPauseAndDumpLocation();
    await Protocol.Debugger.resume();
  }
]);

async function waitPauseAndDumpLocation(session) {
  session = session || InspectorTest.session;
  var message = await session.Protocol.Debugger.oncePaused();
  InspectorTest.log('paused at:');
  await InspectorTest.logSourceLocation(message.params.callFrames[0].location, session);
  return message;
}
