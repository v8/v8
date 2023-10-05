// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Checks when promise rejections are predicted to be caught.');

// catch-prediction.js

function rejectAfterDelayInPromiseConstructor() {
  return new Promise((pass, reject) => setTimeout(() => reject(new Error('fail')), 0));
}

function promiseReject() {
  return Promise.reject(new Error('fail'));
}

function throwInPromiseConstructor() {
  return new Promise(() => {throw new Error('fail');});
}

function rejectInPromiseConstructor() {
  return new Promise((pass, reject) => {
    reject(new Error('fail'));
    console.log('after reject');
  });
}

function rejectAfterDelayInTryInPromiseConstructor() {
  return new Promise((pass, reject) => setTimeout(() => {
    try {
      reject(new Error('fail'));
    } catch(e) {
      console.log('fake');  // Won't be hit
    }
  }, 0));
}

function rejectBindAfterDelay() {
  // Unbreakable location because there will be nothing on the callstack
  return new Promise((pass, reject) => setTimeout(reject.bind(null, new Error('fail')), 0));
}

async function throwFromAsync() {
  throw new Error('fail');
}

async function throwFromAsyncAfterDelay() {
  await delay();
  throw new Error('fail');
}

function customRejectingThenable() {
  // JS supports awaiting on user defined promise-like objects
  return {
    then(onFulfilled, onRejected) {
      onRejected(new Error('fail'));
    }
  }
}

function customRejectingThenableAfterDelay() {
  // JS supports awaiting on user defined promise-like objects
  return {
    then(onFulfilled, onRejected) {
      setTimeout(() => onRejected(new Error('fail')), 0);
    }
  }
}

async function dontHandleAsync(fn) {
  // Unhandled exception that is trivial to predict,
  // for comparison.
  await fn();
}

async function awaitAndCreateInTry(fn) {
  try {
    // The easy case to get right, for comparison.
    await fn();
  } catch(e) {
    console.log('caught');
  }
}

function catchMethod(fn) {
  // Catch handler may not be attached at the time promise is rejected
  fn().catch((e) => console.log('caught'));
}

function finallyMethod(fn) {
  // Is a finally handler considered caught?
  fn().finally(() => console.log('finally handler'));
}

function catchMethodInTry(fn) {
  try {
    fn().catch((e) => console.log('caught'));
  } catch(e) {
    console.log('fake');  // Won't be hit
  }
}

async function awaitInTry(fn) {
  // No handler when promise is created, but there is when it is awaited
  const promise = fn();
  try {
    await promise;
  } catch(e) {
    console.log('caught');
  }
}

async function awaitAndCreateInTryFinally(fn) {
  try {
    await fn();
  } finally {
    // Is finally handler treated as a catch?
    console.log('finally handler');
  }
}

async function createInTry(fn) {
  let promise = null;
  // In this test case we don't handle the exception,
  // but first we try to convince JS we will.
  try {
    promise = fn();
  } catch(e) {
    console.log('fake');  // Won't be hit
  }

  if(promise) {
    await promise;
  }
}

function fireAndForgetInTry(fn) {
  try {
    // Unhandled exception, but will we mispredict as caught?
    fn();
  } catch(e) {
    console.log('fake');  // Won't be hit
  }
}

function finallyAndCatchMethod(fn) {
  // Will we account for the catch method after the finally?
  fn().finally(() => console.log('finally handler'))
      .catch(() => console.log('caught'));
}

function thenMethod(fn) {
  fn().then(() => console.log('not hit'));
}

function thenMethodWithTwoArgs(fn) {
  fn().then(() => console.log('not hit'), () => console.log('caught'));
}

function delay() {
  return new Promise(resolve => setTimeout(resolve, 0));
}

async function testWrapper(throwFunc, handleFunc) {
  // testWrapper runs testFunc and completes successfully when testFunc
  // completes, but does so in a setTimeout so that it doesn't catch any
  // exceptions thrown by testFunc.
  async function runWithResolution(resolve) {
    try {
      const promise = handleFunc(throwFunc);
      console.log('awaiting promise');
      await promise;
      console.log('finished without error');
    } finally {
      // Leave exceptions uncaught, but the testWrapper continues normally
      // anyway. Needs to be in a setTimeout so that uncaught exceptions
      // are handled before the test is complete.
      setTimeout(resolve, 0);
    }
  }
  await new Promise(resolve => setTimeout(runWithResolution.bind(null, resolve), 0));
}

// -------------------------------------

// Order of functions should match above so that line numbers match
const basicThrowFunctions = [rejectAfterDelayInPromiseConstructor, promiseReject];
const advancedThrowFunctions = [throwInPromiseConstructor, rejectInPromiseConstructor, rejectAfterDelayInTryInPromiseConstructor, rejectBindAfterDelay, throwFromAsync, throwFromAsyncAfterDelay, customRejectingThenable, customRejectingThenableAfterDelay];
const basicCatchFunctions = [dontHandleAsync, awaitAndCreateInTry];
const advancedCatchFunctions = [catchMethod, finallyMethod, catchMethodInTry, awaitInTry, awaitAndCreateInTryFinally, createInTry, fireAndForgetInTry, finallyAndCatchMethod, thenMethod, thenMethodWithTwoArgs];
const helpers = [delay, testWrapper];

const file = [...basicThrowFunctions, ...advancedThrowFunctions, ...basicCatchFunctions, ...advancedCatchFunctions, ...helpers].join('\n\n');
const startLine = 9;  // Should match first line of first function
contextGroup.addScript(file, startLine, 0, 'catch-prediction.js');
session.setupScriptMap();

Protocol.Debugger.onPaused(message => {
  InspectorTest.log('Paused');
  session.logCallFrames(message.params.callFrames);
  session.logAsyncStackTrace(message.params.asyncStackTrace);
  InspectorTest.log('');
  Protocol.Debugger.resume();
});

Protocol.Console.onMessageAdded(event => InspectorTest.log('console: ' + event.params.message.text));
Protocol.Debugger.enable();
Protocol.Debugger.setAsyncCallStackDepth({maxDepth: 6});
Protocol.Console.enable();
Protocol.Runtime.onExceptionRevoked(event => InspectorTest.log('Exception revoked for reason: ' + event.params.reason));
Protocol.Runtime.onExceptionThrown(event => {
  InspectorTest.log(`Uncaught exception: ${event.params.exceptionDetails.text}`);
  session.logAsyncStackTrace(event.params.exceptionDetails.stackTrace);
});
Protocol.Runtime.enable();

const testFunctions = [];

function addTestFunctionsFor(throwFunc, catchFunc) {
  for (const state of ['caught', 'uncaught']) {
    testFunctions.push(async function testCase(next) {
      InspectorTest.log(`> Throwing from ${throwFunc.name}, handling with ${catchFunc.name}, breaking on ${state} exceptions:`);
      await Protocol.Debugger.setPauseOnExceptions({state});
      await Protocol.Runtime.evaluate({expression: `testWrapper(${throwFunc.name}, ${catchFunc.name})//# sourceURL=test_framework.js`, awaitPromise: true});
    });
  }
}

for (const catchFunc of basicCatchFunctions) {
  for (const throwFunc of [...basicThrowFunctions, ...advancedThrowFunctions]) {
    addTestFunctionsFor(throwFunc, catchFunc);
  }
}

for (const catchFunc of advancedCatchFunctions) {
  for (const throwFunc of basicThrowFunctions) {
    addTestFunctionsFor(throwFunc, catchFunc);
  }
}

InspectorTest.runAsyncTestSuite(testFunctions);
