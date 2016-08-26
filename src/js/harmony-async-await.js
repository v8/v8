// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils, extrasUtils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var AsyncFunctionNext;
var AsyncFunctionThrow;
var GlobalPromise;
var NewPromiseCapability;
var PerformPromiseThen;
var PromiseCastResolved;
var RejectPromise;

utils.Import(function(from) {
  AsyncFunctionNext = from.AsyncFunctionNext;
  AsyncFunctionThrow = from.AsyncFunctionThrow;
  GlobalPromise = from.GlobalPromise;
  NewPromiseCapability = from.NewPromiseCapability;
  PromiseCastResolved = from.PromiseCastResolved;
  PerformPromiseThen = from.PerformPromiseThen;
  RejectPromise = from.RejectPromise;
});

// -------------------------------------------------------------------

function AsyncFunctionAwait(generator, value) {
  // Promise.resolve(value).then(
  //     value => AsyncFunctionNext(value),
  //     error => AsyncFunctionThrow(error)
  // );
  var promise = PromiseCastResolved(value);

  var onFulfilled =
      (sentValue) => %_Call(AsyncFunctionNext, generator, sentValue);
  var onRejected =
      (sentError) => %_Call(AsyncFunctionThrow, generator, sentError);

  // false debugEvent to avoid redundant ExceptionEvents
  var throwawayCapability = NewPromiseCapability(GlobalPromise, false);
  return PerformPromiseThen(promise, onFulfilled, onRejected,
                            throwawayCapability);
}

// How the parser rejects promises from async/await desugaring
function RejectPromiseNoDebugEvent(promise, reason) {
  return RejectPromise(promise, reason, false);
}

%InstallToContext([
  "async_function_await", AsyncFunctionAwait,
  "reject_promise_no_debug_event", RejectPromiseNoDebugEvent,
]);

})
