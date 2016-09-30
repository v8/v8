// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.evaluateInPage(
`function testFunction()
{
  function foo()
  {
    try {
      throw new Error();
    } catch (e) {
    }
  }
  debugger;
  foo();
  console.log("completed");
}`);

InspectorTest.sendCommandOrDie("Debugger.enable", {});
InspectorTest.sendCommandOrDie("Runtime.enable", {});
step1();

function step1()
{
  InspectorTest.sendCommandOrDie("Runtime.evaluate", { "expression": "setTimeout(testFunction, 0);"});
  var commands = [ "Print", "stepOver", "stepOver", "Print", "resume" ];
  InspectorTest.eventHandler["Debugger.paused"] = function(messageObject)
  {
    var command = commands.shift();
    if (command === "Print") {
      var callFrames = messageObject.params.callFrames;
      for (var callFrame of callFrames)
        InspectorTest.log(callFrame.functionName + ":" + callFrame.location.lineNumber);
      command = commands.shift();
    }
    if (command)
      InspectorTest.sendCommandOrDie("Debugger." + command, {});
  }

  InspectorTest.eventHandler["Runtime.consoleAPICalled"] = function(messageObject)
  {
    if (messageObject.params.args[0].value === "completed") {
      if (commands.length)
        InspectorTest.log("[FAIL]: execution was resumed too earlier.")
      step2();
    }
  }
}

function step2()
{
  InspectorTest.sendCommandOrDie("Runtime.evaluate", { "expression": "setTimeout(testFunction, 0);"});
  var commands = [ "Print", "stepOver", "stepInto", "stepOver", "stepOver", "Print", "resume" ];
  InspectorTest.eventHandler["Debugger.paused"] = function(messageObject)
  {
    var command = commands.shift();
    if (command === "Print") {
      var callFrames = messageObject.params.callFrames;
      for (var callFrame of callFrames)
        InspectorTest.log(callFrame.functionName + ":" + callFrame.location.lineNumber);
      command = commands.shift();
    }
    if (command)
      InspectorTest.sendCommandOrDie("Debugger." + command, {});
  }

  InspectorTest.eventHandler["Runtime.consoleAPICalled"] = function(messageObject)
  {
    if (messageObject.params.args[0].value === "completed") {
      if (commands.length)
        InspectorTest.log("[FAIL]: execution was resumed too earlier.")
      InspectorTest.completeTest();
    }
  }
}
