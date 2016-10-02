// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.sendCommand("Runtime.enable", {});

addConsoleMessagePromise("console.log(239)")
  .then(dumpMessage)
  .then(() => addConsoleMessagePromise("var l = console.log;\n  l(239)"))
  .then(dumpMessage)
  .then(() => InspectorTest.completeTest());

function addConsoleMessagePromise(expression)
{
  var cb;
  var p = new Promise((resolver) => cb = resolver);
  InspectorTest.eventHandler["Runtime.consoleAPICalled"] = (messageObject) => cb(messageObject);
  InspectorTest.sendCommand("Runtime.evaluate", { expression: expression });
  return p;
}

function dumpMessage(messageObject)
{
  var msg = messageObject.params;
  delete msg.executionContextId;
  delete msg.args;
  delete msg.timestamp;
  for (var frame of msg.stackTrace.callFrames)
    frame.scriptId = 0;
  if (!frame.functionName)
    frame.functionName = "(anonymous)";
  if (!frame.url)
    frame.url = "(empty)";
  InspectorTest.logObject(msg);
}
