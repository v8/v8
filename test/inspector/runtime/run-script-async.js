// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

print("Tests that Runtime.compileScript and Runtime.runScript work with awaitPromise flag.");

InspectorTest.runTestSuite([
  function testRunAndCompileWithoutAgentEnable(next)
  {
    InspectorTest.sendCommandPromise("Runtime.compileScript", { expression: "", sourceURL: "", persistScript: true })
      .then((result) => dumpResult(result))
      .then(() => InspectorTest.sendCommandPromise("Runtime.runScript", { scriptId: "1" }))
      .then((result) => dumpResult(result))
      .then(() => next());
  },

  function testSyntaxErrorInScript(next)
  {
    InspectorTest.sendCommandPromise("Runtime.enable", {})
      .then(() => InspectorTest.sendCommandPromise("Runtime.compileScript", { expression: "\n }", sourceURL: "boo.js", persistScript: true }))
      .then((result) => dumpResult(result))
      .then(() => InspectorTest.sendCommandPromise("Runtime.disable", {}))
      .then(() => next());
  },

  function testSyntaxErrorInEvalInScript(next)
  {
    InspectorTest.sendCommandPromise("Runtime.enable", {})
      .then(() => InspectorTest.sendCommandPromise("Runtime.compileScript", { expression: "{\n eval(\"\\\n}\")\n}", sourceURL: "boo.js", persistScript: true }))
      .then((result) => InspectorTest.sendCommandPromise("Runtime.runScript", { scriptId: result.result.scriptId }))
      .then((result) => dumpResult(result))
      .then(() => InspectorTest.sendCommandPromise("Runtime.disable", {}))
      .then(() => next());
  },

  function testRunNotCompiledScript(next)
  {
    InspectorTest.sendCommandPromise("Runtime.enable", {})
      .then((result) => InspectorTest.sendCommandPromise("Runtime.runScript", { scriptId: "1" }))
      .then((result) => dumpResult(result))
      .then(() => InspectorTest.sendCommandPromise("Runtime.disable", {}))
      .then(() => next());
  },

  function testRunCompiledScriptAfterAgentWasReenabled(next)
  {
    var scriptId;
    InspectorTest.sendCommandPromise("Runtime.enable", {})
      .then(() => InspectorTest.sendCommandPromise("Runtime.compileScript", { expression: "{\n eval(\"\\\n}\")\n}", sourceURL: "boo.js", persistScript: true }))
      .then((result) => scriptId = result.result.scriptId)
      .then(() => InspectorTest.sendCommandPromise("Runtime.disable", {}))
      .then((result) => InspectorTest.sendCommandPromise("Runtime.runScript", { scriptId: scriptId }))
      .then((result) => dumpResult(result))
      .then(() => InspectorTest.sendCommandPromise("Runtime.enable", {}))
      .then((result) => InspectorTest.sendCommandPromise("Runtime.runScript", { scriptId: scriptId }))
      .then((result) => dumpResult(result))
      .then(() => InspectorTest.sendCommandPromise("Runtime.disable", {}))
      .then(() => next());
  },

  function testRunScriptWithPreview(next)
  {
    InspectorTest.sendCommandPromise("Runtime.enable", {})
      .then(() => InspectorTest.sendCommandPromise("Runtime.compileScript", { expression: "({a:1})", sourceURL: "boo.js", persistScript: true }))
      .then((result) => InspectorTest.sendCommandPromise("Runtime.runScript", { scriptId: result.result.scriptId, generatePreview: true }))
      .then((result) => dumpResult(result))
      .then(() => InspectorTest.sendCommandPromise("Runtime.disable", {}))
      .then(() => next());
  },

  function testRunScriptReturnByValue(next)
  {
    InspectorTest.sendCommandPromise("Runtime.enable", {})
      .then(() => InspectorTest.sendCommandPromise("Runtime.compileScript", { expression: "({a:1})", sourceURL: "boo.js", persistScript: true }))
      .then((result) => InspectorTest.sendCommandPromise("Runtime.runScript", { scriptId: result.result.scriptId, returnByValue: true }))
      .then((result) => dumpResult(result))
      .then(() => InspectorTest.sendCommandPromise("Runtime.disable", {}))
      .then(() => next());
  },

  function testAwaitNotPromise(next)
  {
    InspectorTest.sendCommandPromise("Runtime.enable", {})
      .then(() => InspectorTest.sendCommandPromise("Runtime.compileScript", { expression: "({a:1})", sourceURL: "boo.js", persistScript: true }))
      .then((result) => InspectorTest.sendCommandPromise("Runtime.runScript", { scriptId: result.result.scriptId, awaitPromise: true }))
      .then((result) => dumpResult(result))
      .then(() => InspectorTest.sendCommandPromise("Runtime.disable", {}))
      .then(() => next());
  },

  function testAwaitResolvedPromise(next)
  {
    InspectorTest.sendCommandPromise("Runtime.enable", {})
      .then(() => InspectorTest.sendCommandPromise("Runtime.compileScript", { expression: "Promise.resolve({a:1})", sourceURL: "boo.js", persistScript: true }))
      .then((result) => InspectorTest.sendCommandPromise("Runtime.runScript", { scriptId: result.result.scriptId, awaitPromise: true, returnByValue: true }))
      .then((result) => dumpResult(result))
      .then(() => InspectorTest.sendCommandPromise("Runtime.disable", {}))
      .then(() => next());
  },

  function testAwaitRejectedPromise(next)
  {
    InspectorTest.sendCommandPromise("Runtime.enable", {})
      .then(() => InspectorTest.sendCommandPromise("Runtime.compileScript", { expression: "Promise.reject({a:1})", sourceURL: "boo.js", persistScript: true }))
      .then((result) => InspectorTest.sendCommandPromise("Runtime.runScript", { scriptId: result.result.scriptId, awaitPromise: true, returnByValue: true }))
      .then((result) => dumpResult(result))
      .then(() => InspectorTest.sendCommandPromise("Runtime.disable", {}))
      .then(() => next());
  }
]);

function dumpResult(result)
{
  if (result.error) {
    result.error.code = 0;
    InspectorTest.logObject(result.error);
    return;
  }
  result = result.result;
  if (result.exceptionDetails) {
    result.exceptionDetails.exceptionId = 0;
    result.exceptionDetails.exception.objectId = 0;
  }
  if (result.exceptionDetails && result.exceptionDetails.scriptId)
    result.exceptionDetails.scriptId = 0;
  if (result.exceptionDetails && result.exceptionDetails.stackTrace) {
    for (var frame of result.exceptionDetails.stackTrace.callFrames)
      frame.scriptId = 0;
  }
  if (result.result && result.result.objectId)
    result.result.objectId = "[ObjectId]";
  InspectorTest.logObject(result);
}
