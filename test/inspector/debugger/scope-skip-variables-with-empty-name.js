// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.evaluateInPage(
`function testFunction()
{
    for (var a of [1]) {
        ++a;
        debugger;
    }
}`);

InspectorTest.sendCommandOrDie("Debugger.enable", {});
InspectorTest.eventHandler["Debugger.paused"] = dumpScopeOnPause;
InspectorTest.sendCommandOrDie("Runtime.evaluate", { "expression": "testFunction()" });

var waitScopeObjects = 0;
function dumpScopeOnPause(message)
{
  var scopeChain = message.params.callFrames[0].scopeChain;
  var localScopeObjectIds = [];
  for (var scope of scopeChain) {
    if (scope.type === "local")
      localScopeObjectIds.push(scope.object.objectId);
  }
  waitScopeObjects = localScopeObjectIds.length;
  if (!waitScopeObjects) {
    InspectorTest.completeTest();
  } else {
    for (var objectId of localScopeObjectIds)
      InspectorTest.sendCommandOrDie("Runtime.getProperties", { "objectId" : objectId }, dumpProperties);
  }
}

function dumpProperties(message)
{
  InspectorTest.logObject(message);
  --waitScopeObjects;
  if (!waitScopeObjects)
    InspectorTest.sendCommandOrDie("Debugger.resume", {}, () => InspectorTest.completeTest());
}
