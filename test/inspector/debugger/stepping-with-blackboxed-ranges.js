// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.evaluateInPage(
`function blackboxedBoo()
{
    var a = 42;
    var b = foo();
    return a + b;
}
//# sourceURL=blackboxed-script.js`);

InspectorTest.evaluateInPage(
`function notBlackboxedFoo()
{
    var a = 42;
    var b = blackboxedBoo();
    return a + b;
}

function blackboxedFoo()
{
    var a = 42;
    var b = notBlackboxedFoo();
    return a + b;
}

function notBlackboxedBoo()
{
    var a = 42;
    var b = blackboxedFoo();
    return a + b;
}
//# sourceURL=mixed-source.js`);

InspectorTest.evaluateInPage(
`function testFunction()
{
    notBlackboxedBoo(); // for setup ranges and stepOut
    notBlackboxedBoo(); // for stepIn
}

function foo()
{
    debugger;
    return 239;
}`);

InspectorTest.eventHandler["Debugger.paused"] = setBlackboxedScriptRanges;
InspectorTest.sendCommandOrDie("Debugger.enable", {}, callTestFunction);

function callTestFunction(response)
{
  InspectorTest.sendCommand("Runtime.evaluate", { expression: "setTimeout(testFunction, 0);"});
}

function setBlackboxedScriptRanges(response)
{
  var callFrames = response.params.callFrames;
  printCallFrames(callFrames);
  InspectorTest.sendCommand("Debugger.setBlackboxedRanges", {
    scriptId: callFrames[1].location.scriptId,
    positions: [ { lineNumber: 0, columnNumber: 0 } ] // blackbox ranges for blackboxed.js
  }, setIncorrectRanges.bind(null, callFrames[2].location.scriptId));
}

var incorrectPositions = [
    [ { lineNumber: 0, columnNumber: 0 }, { lineNumber: 0, columnNumber: 0 } ],
    [ { lineNumber: 0, columnNumber: 1 }, { lineNumber: 0, columnNumber: 0 } ],
    [ { lineNumber: 0, columnNumber: -1 } ],
];

function setIncorrectRanges(scriptId, response)
{
  if (response.error)
    InspectorTest.log(response.error.message);
  var positions = incorrectPositions.shift();
  if (!positions) {
    setMixedSourceRanges(scriptId);
    return;
  }
  InspectorTest.log("Try to set positions: " + JSON.stringify(positions));
  InspectorTest.sendCommand("Debugger.setBlackboxedRanges", {
    scriptId: scriptId,
    positions: positions
  }, setIncorrectRanges.bind(null, scriptId));
}

function setMixedSourceRanges(scriptId)
{
  InspectorTest.eventHandler["Debugger.paused"] = runAction;
  InspectorTest.sendCommandOrDie("Debugger.setBlackboxedRanges", {
    scriptId: scriptId,
    positions: [ { lineNumber: 8, columnNumber: 0 }, { lineNumber: 15, columnNumber: 0 } ] // blackbox ranges for mixed.js
  }, runAction);
}

var actions = [ "stepOut", "print", "stepOut", "print", "stepOut", "print",
    "stepInto", "print", "stepOver", "stepInto", "print", "stepOver", "stepInto", "print",
    "stepOver", "stepInto", "print" ];

function runAction(response)
{
  var action = actions.shift();
  if (!action)
    InspectorTest.completeTest();

  if (action === "print") {
    printCallFrames(response.params.callFrames);
    runAction({});
  } else {
    InspectorTest.log("action: " + action);
    InspectorTest.sendCommandOrDie("Debugger." + action, {});
  }
}

function printCallFrames(callFrames)
{
  var topCallFrame = callFrames[0];
  if (topCallFrame.functionName.startsWith("blackboxed"))
    InspectorTest.log("FAIL: blackboxed function in top call frame");
  for (var callFrame of callFrames)
    InspectorTest.log(callFrame.functionName + ": " + callFrame.location.lineNumber + ":" + callFrame.location.columnNumber);
  InspectorTest.log("");
}
