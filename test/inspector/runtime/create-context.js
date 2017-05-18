// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.log('Checks createContext().');

var executionContextIds = new Set();
var contextGroup = InspectorTest.createContextGroup();
var session = InspectorTest.createSession(contextGroup);
setup(InspectorTest.session);
setup(session);

Protocol.Runtime.enable()
  .then(() => session.Protocol.Runtime.enable({}))
  .then(() => Protocol.Debugger.enable())
  .then(() => session.Protocol.Debugger.enable({}))
  .then(InspectorTest.logMessage)
  .then(() => {
    Protocol.Runtime.evaluate({ expression: 'debugger;' });
    session.Protocol.Runtime.evaluate({expression: 'setTimeout(x => x * 2, 0)'});
    Protocol.Runtime.evaluate({ expression: 'setTimeout(x => x * 3, 0)' });
  })
  .then(() => InspectorTest.waitPendingTasks())
  .then(() => {
    InspectorTest.log(`Reported script's execution id: ${executionContextIds.size}`);
    executionContextIds.clear();
  })
  .then(() => InspectorTest.session.reconnect())
  .then(() => session.reconnect())
  .then(() => {
    Protocol.Runtime.evaluate({ expression: 'debugger;' })
    session.Protocol.Runtime.evaluate({ expression: 'setTimeout(x => x * 2, 0)' });
    Protocol.Runtime.evaluate({ expression: 'setTimeout(x => x * 3, 0)' });
  })
  .then(() => InspectorTest.waitPendingTasks())
  .then(() => session.Protocol.Debugger.disable({}))
  .then(() => Protocol.Debugger.disable({}))
  .then(() => InspectorTest.log(`Reported script's execution id: ${executionContextIds.size}`))
  .then(InspectorTest.completeTest);

function setup(session) {
  session.Protocol.Runtime.onExecutionContextCreated(InspectorTest.logMessage);
  InspectorTest.setupScriptMap(session);
  session.Protocol.Debugger.onPaused((message) => {
    InspectorTest.logSourceLocation(message.params.callFrames[0].location, session);
    session.Protocol.Debugger.stepOut();
  });
  session.Protocol.Debugger.onScriptParsed(message => executionContextIds.add(message.params.executionContextId));
}
