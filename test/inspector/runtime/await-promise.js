// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --expose_gc

print("Tests that Runtime.awaitPromise works.");

InspectorTest.evaluateInPage(
`
var resolveCallback;
var rejectCallback;
function createPromise()
{
    return new Promise((resolve, reject) => { resolveCallback = resolve; rejectCallback = reject });
}

function resolvePromise()
{
    resolveCallback(239);
    resolveCallback = undefined;
    rejectCallback = undefined;
}

function rejectPromise()
{
    rejectCallback(239);
    resolveCallback = undefined;
    rejectCallback = undefined;
}

//# sourceURL=test.js`);

InspectorTest.sendCommandPromise("Debugger.enable", {})
    .then(() => InspectorTest.sendCommandPromise("Debugger.setAsyncCallStackDepth", { maxDepth: 128 }))
    .then(() => testSuite());

function dumpResult(result)
{
  if (result.exceptionDetails) {
    if (result.exceptionDetails.stackTrace && result.exceptionDetails.stackTrace.parent) {
      for (var frame of result.exceptionDetails.stackTrace.parent.callFrames) {
        frame.scriptId = 0;
        if (!frame.url)
          frame.url = "(empty)";
        if (!frame.functionName)
          frame.functionName = "(anonymous)";
      }
    }
    result.exceptionDetails.exceptionId = 0;
    if (result.exceptionDetails.exception)
      result.exceptionDetails.exception.objectId = 0;
  }
  InspectorTest.logObject(result);
}

function testSuite()
{
  InspectorTest.runTestSuite([
    function testResolvedPromise(next)
    {
      InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "Promise.resolve(239)"})
        .then((result) => InspectorTest.sendCommandPromise("Runtime.awaitPromise", { promiseObjectId: result.result.result.objectId, returnByValue: false, generatePreview: true }))
        .then((result) => dumpResult(result.result))
        .then(() => next());
    },

    function testRejectedPromise(next)
    {
      InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "Promise.reject({ a : 1 })"})
        .then((result) => InspectorTest.sendCommandPromise("Runtime.awaitPromise", { promiseObjectId: result.result.result.objectId, returnByValue: true, generatePreview: false }))
        .then((result) => dumpResult(result.result))
        .then(() => next());
    },

    function testRejectedPromiseWithStack(next)
    {
      InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "createPromise()"})
        .then((result) => scheduleRejectAndAwaitPromise(result))
        .then((result) => dumpResult(result.result))
        .then(() => next());

      function scheduleRejectAndAwaitPromise(result)
      {
        var promise = InspectorTest.sendCommandPromise("Runtime.awaitPromise", { promiseObjectId: result.result.result.objectId });
        InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "rejectPromise()" });
        return promise;
      }
    },

    function testPendingPromise(next)
    {
      InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "createPromise()"})
        .then((result) => scheduleFulfillAndAwaitPromise(result))
        .then((result) => dumpResult(result.result))
        .then(() => next());

      function scheduleFulfillAndAwaitPromise(result)
      {
        var promise = InspectorTest.sendCommandPromise("Runtime.awaitPromise", { promiseObjectId: result.result.result.objectId });
        InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "resolvePromise()" });
        return promise;
      }
    },

    function testResolvedWithoutArgsPromise(next)
    {
      InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "Promise.resolve()"})
        .then((result) => InspectorTest.sendCommandPromise("Runtime.awaitPromise", { promiseObjectId: result.result.result.objectId, returnByValue: true, generatePreview: false }))
        .then((result) => dumpResult(result.result))
        .then(() => next());
    },

    function testGarbageCollectedPromise(next)
    {
      InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "new Promise(() => undefined)" })
        .then((result) => scheduleGCAndawaitPromise(result))
        .then((result) => InspectorTest.logObject(result.error))
        .then(() => next());

      function scheduleGCAndawaitPromise(result)
      {
        var objectId = result.result.result.objectId;
        var promise = InspectorTest.sendCommandPromise("Runtime.awaitPromise", { promiseObjectId: objectId });
        gcPromise(objectId);
        return promise;
      }

      function gcPromise(objectId)
      {
        InspectorTest.sendCommandPromise("Runtime.releaseObject", { objectId: objectId})
          .then(() => InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "gc()" }));
      }
    }
  ]);
}
