// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

print("Check that stepInto at then end of the script go to next user script instead InjectedScriptSource.js.");

InspectorTest.evaluateInPage(
`function foo()
{
    return 239;
}`);

InspectorTest.sendCommandOrDie("Debugger.enable", {});
InspectorTest.eventHandler["Debugger.paused"] = debuggerPaused;
InspectorTest.sendCommandOrDie("Runtime.evaluate", { "expression": "(function boo() { setTimeout(foo, 0); debugger; })()" });

var actions = [ "stepInto", "stepInto", "stepInto" ];
function debuggerPaused(result)
{
  InspectorTest.log("Stack trace:");
  for (var callFrame of result.params.callFrames)
    InspectorTest.log(callFrame.functionName + ":" + callFrame.location.lineNumber + ":" + callFrame.location.columnNumber);
  InspectorTest.log("");

  var action = actions.shift();
  if (!action) {
    InspectorTest.sendCommandOrDie("Debugger.resume", {}, () => InspectorTest.completeTest());
    return;
  }
  InspectorTest.log("Perform " + action);
  InspectorTest.sendCommandOrDie("Debugger." + action, {});
}
