// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.evaluateInPage(`
function testFunction()
{
  debugger;
}
//# sourceURL=foo.js`);

InspectorTest.sendCommand("Debugger.enable", {});

InspectorTest.eventHandler["Debugger.paused"] = handleDebuggerPausedOne;

InspectorTest.sendCommand("Runtime.evaluate", { "expression": "setTimeout(testFunction, 0)" });

var obsoleteTopFrameId;

function handleDebuggerPausedOne(messageObject)
{
  InspectorTest.log("Paused on 'debugger;'");

  var topFrame = messageObject.params.callFrames[0];
  obsoleteTopFrameId = topFrame.callFrameId;

  InspectorTest.eventHandler["Debugger.paused"] = undefined;

  InspectorTest.sendCommand("Debugger.resume", { }, callbackResume);
}

function callbackResume(response)
{
  InspectorTest.log("resume");
  InspectorTest.log("restartFrame");
  InspectorTest.sendCommand("Debugger.restartFrame", { callFrameId: obsoleteTopFrameId }, callbackRestartFrame);
}

function callbackRestartFrame(response)
{
  logErrorResponse(response);
  InspectorTest.log("evaluateOnFrame");
  InspectorTest.sendCommand("Debugger.evaluateOnCallFrame", { callFrameId: obsoleteTopFrameId, expression: "0"} , callbackEvaluate);
}

function callbackEvaluate(response)
{
  logErrorResponse(response);
  InspectorTest.log("setVariableValue");
  InspectorTest.sendCommand("Debugger.setVariableValue", { callFrameId: obsoleteTopFrameId, scopeNumber: 0, variableName: "a", newValue: { value: 0 } }, callbackSetVariableValue);
}

function callbackSetVariableValue(response)
{
  logErrorResponse(response);
  InspectorTest.completeTest();
}

function logErrorResponse(response)
{
  if (response.error) {
    if (response.error.message.indexOf("Can only perform operation while paused.") !== -1) {
      InspectorTest.log("PASS, error message as expected");
      return;
    }
  }
  InspectorTest.log("FAIL, unexpected error message");
  InspectorTest.log(JSON.stringify(response));
}
