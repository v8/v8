// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.evaluateInPage(
`function bar()
{
    return 42;
}`);

InspectorTest.evaluateInPage(
`function foo()
{
    var a = bar();
    return a + 1;
}
//# sourceURL=foo.js`);

InspectorTest.evaluateInPage(
`function qwe()
{
    var a = foo();
    return a + 1;
}
//# sourceURL=qwe.js`);

InspectorTest.evaluateInPage(
`function baz()
{
    var a = qwe();
    return a + 1;
}
//# sourceURL=baz.js`);

InspectorTest.sendCommand("Debugger.enable", {});
InspectorTest.sendCommand("Debugger.setBlackboxPatterns", { patterns: [ "foo([" ] }, dumpError);

function dumpError(message)
{
  InspectorTest.log(message.error.message);
  InspectorTest.eventHandler["Debugger.paused"] = dumpStackAndRunNextCommand;
  InspectorTest.sendCommandOrDie("Debugger.setBlackboxPatterns", { patterns: [ "baz\.js", "foo\.js" ] });
  InspectorTest.sendCommandOrDie("Runtime.evaluate", { "expression": "debugger;baz()" });
}

var commands = [ "stepInto", "stepInto", "stepInto", "stepOut", "stepInto", "stepInto" ];
function dumpStackAndRunNextCommand(message)
{
  InspectorTest.log("Paused in");
  var callFrames = message.params.callFrames;
  for (var callFrame of callFrames)
    InspectorTest.log((callFrame.functionName || "(...)") + ":" + (callFrame.location.lineNumber + 1));
  var command = commands.shift();
  if (!command) {
    InspectorTest.completeTest();
    return;
  }
  InspectorTest.sendCommandOrDie("Debugger." + command, {});
}
