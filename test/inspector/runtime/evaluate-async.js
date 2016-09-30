// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

print("Tests that Runtime.evaluate works with awaitPromise flag.");

InspectorTest.evaluateInPage(`
function createPromiseAndScheduleResolve()
{
  var resolveCallback;
  var promise = new Promise((resolve) => resolveCallback = resolve);
  setTimeout(resolveCallback.bind(null, { a : 239 }), 0);
  return promise;
}`);

function dumpResult(result)
{
  if (result.exceptionDetails) {
    result.exceptionDetails.scriptId = "(scriptId)";
    result.exceptionDetails.exceptionId = 0;
    result.exceptionDetails.exception.objectId = 0;
  }
  InspectorTest.logObject(result);
}

InspectorTest.runTestSuite([
  function testResolvedPromise(next)
  {
    InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "Promise.resolve(239)", awaitPromise: true, generatePreview: true })
      .then((result) => dumpResult(result.result))
      .then(() => next());
  },

  function testRejectedPromise(next)
  {
    InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "Promise.reject(239)", awaitPromise: true })
      .then((result) => dumpResult(result.result))
      .then(() => next());
  },

  function testPrimitiveValueInsteadOfPromise(next)
  {
    InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "true", awaitPromise: true })
      .then((result) => InspectorTest.logObject(result.error))
      .then(() => next());
  },

  function testObjectInsteadOfPromise(next)
  {
    InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "({})", awaitPromise: true })
      .then((result) => InspectorTest.logObject(result.error))
      .then(() => next());
  },

  function testPendingPromise(next)
  {
    InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "createPromiseAndScheduleResolve()", awaitPromise: true, returnByValue: true })
      .then((result) => dumpResult(result.result))
      .then(() => next());
  },

  function testExceptionInEvaluate(next)
  {
    InspectorTest.sendCommandPromise("Runtime.evaluate", { expression: "throw 239", awaitPromise: true })
      .then((result) => dumpResult(result.result))
      .then(() => next());
  }
]);
