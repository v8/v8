// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.log('Checks stepping with more then one context group.');

(async function test() {
  InspectorTest.setupScriptMap();
  await Protocol.Debugger.enable();
  let contextGroup = InspectorTest.createContextGroup();
  let session = InspectorTest.createSession(contextGroup);
  InspectorTest.setupScriptMap(session);
  await session.Protocol.Debugger.enable({});
  Protocol.Runtime.evaluate({expression: 'debugger'});
  session.Protocol.Runtime.evaluate({expression: 'setTimeout(() => { debugger }, 0)'});
  Protocol.Runtime.evaluate({expression: 'setTimeout(() => 42, 0)'});
  await waitPauseAndDumpLocation(InspectorTest.session);
  Protocol.Debugger.stepOver();
  await Protocol.Debugger.oncePaused();
  Protocol.Debugger.stepOver();
  await waitPauseAndDumpLocation(InspectorTest.session);
  await session.Protocol.Debugger.disable({});
  await Protocol.Debugger.disable();
  InspectorTest.completeTest();
})();

async function waitPauseAndDumpLocation(session) {
  var message = await session.Protocol.Debugger.oncePaused();
  InspectorTest.log('paused at:');
  await InspectorTest.logSourceLocation(message.params.callFrames[0].location, session);
  return message;
}
