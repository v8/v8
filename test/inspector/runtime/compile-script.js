// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var executionContextId;

InspectorTest.sendCommand("Debugger.enable", {}, onDebuggerEnabled);

function onDebuggerEnabled()
{
  InspectorTest.sendCommand("Runtime.enable", {});
  InspectorTest.eventHandler["Debugger.scriptParsed"] = onScriptParsed;
  InspectorTest.eventHandler["Runtime.executionContextCreated"] = onExecutionContextCreated;
}

function onScriptParsed(messageObject)
{
  if (!messageObject.params.url)
    return;
  InspectorTest.log("Debugger.scriptParsed: " + messageObject.params.url);
}

function onExecutionContextCreated(messageObject)
{
  executionContextId = messageObject.params.context.id;
  testCompileScript("\n  (", false, "foo1.js")
    .then(() => testCompileScript("239", true, "foo2.js"))
    .then(() => testCompileScript("239", false, "foo3.js"))
    .then(() => testCompileScript("testfunction f()\n{\n    return 0;\n}\n", false, "foo4.js"))
    .then(() => InspectorTest.completeTest());
}

function testCompileScript(expression, persistScript, sourceURL)
{
  InspectorTest.log("Compiling script: " + sourceURL);
  InspectorTest.log("         persist: " + persistScript);
  var callback;
  var promise = new Promise(resolver => callback = resolver);
  InspectorTest.sendCommand("Runtime.compileScript", {
    expression: expression,
    sourceURL: sourceURL,
    persistScript: persistScript,
    executionContextId: executionContextId
  }, onCompiled);
  return promise;

  function onCompiled(messageObject)
  {
    var result = messageObject.result;
    if (result.exceptionDetails) {
      result.exceptionDetails.exceptionId = 0;
      result.exceptionDetails.exception.objectId = 0;
      result.exceptionDetails.scriptId = 0;
    }
    if (result.scriptId)
      result.scriptId = 0;
    InspectorTest.logObject(result, "compilation result: ");
    InspectorTest.log("-----");
    callback();
  }
}
