// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

print("Check that console.log doesn't run microtasks.");

InspectorTest.evaluateInPage(
`
function testFunction()
{
  Promise.resolve().then(function(){ console.log(239); });
  console.log(42);
  console.log(43);
}`);

InspectorTest.sendCommandOrDie("Runtime.enable", {});
InspectorTest.eventHandler["Runtime.consoleAPICalled"] = messageAdded;
InspectorTest.sendCommandOrDie("Runtime.evaluate", { "expression": "testFunction()" });
InspectorTest.sendCommandOrDie("Runtime.evaluate", { "expression": "setTimeout(() => console.log(\"finished\"), 0)" });

function messageAdded(result)
{
  InspectorTest.logObject(result.params.args[0]);
  if (result.params.args[0].value === "finished")
    InspectorTest.completeTest();
}
