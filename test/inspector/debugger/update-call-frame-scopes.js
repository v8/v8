// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.evaluateInPage(
`function TestFunction()
{
    var a = 2;
    debugger;
    debugger;
}`);

var newVariableValue = 55;

InspectorTest.sendCommand("Debugger.enable", {});

InspectorTest.eventHandler["Debugger.paused"] = handleDebuggerPaused;

InspectorTest.sendCommand("Runtime.evaluate", { "expression": "setTimeout(TestFunction, 0)" });

function handleDebuggerPaused(messageObject)
{
  InspectorTest.log("Paused on 'debugger;'");
  InspectorTest.eventHandler["Debugger.paused"] = undefined;

  var topFrame = messageObject.params.callFrames[0];
  var topFrameId = topFrame.callFrameId;
  InspectorTest.sendCommand("Debugger.evaluateOnCallFrame", { "callFrameId": topFrameId, "expression": "a = " + newVariableValue }, callbackChangeValue);
}

function callbackChangeValue(response)
{
  InspectorTest.log("Variable value changed");
  InspectorTest.eventHandler["Debugger.paused"] = callbackGetBacktrace;
  InspectorTest.sendCommand("Debugger.resume", { });
}

function callbackGetBacktrace(response)
{
  InspectorTest.log("Stacktrace re-read again");
  var localScope = response.params.callFrames[0].scopeChain[0];
  InspectorTest.sendCommand("Runtime.getProperties", { "objectId": localScope.object.objectId }, callbackGetProperties);
}

function callbackGetProperties(response)
{
  InspectorTest.log("Scope variables downloaded anew");
  var varNamedA;
  var propertyList = response.result.result;
  for (var i = 0; i < propertyList.length; i++) {
    if (propertyList[i].name === "a") {
      varNamedA = propertyList[i];
      break;
    }
  }
  if (varNamedA) {
    var actualValue = varNamedA.value.value;
    InspectorTest.log("New variable is " + actualValue + ", expected is " + newVariableValue + ", old was: 2");
    InspectorTest.log(actualValue === newVariableValue ? "SUCCESS" : "FAIL");
  } else {
    InspectorTest.log("Failed to find variable in scope");
  }
  InspectorTest.completeTest();
}
