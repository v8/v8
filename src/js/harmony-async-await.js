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
var IsPromise;
var NewPromiseCapability;
var PerformPromiseThen;
var PromiseCreate;
var RejectPromise;
var ResolvePromise;

utils.Import(function(from) {
  AsyncFunctionNext = from.AsyncFunctionNext;
  AsyncFunctionThrow = from.AsyncFunctionThrow;
  IsPromise = from.IsPromise;
  GlobalPromise = from.GlobalPromise;
  NewPromiseCapability = from.NewPromiseCapability;
  PromiseCreate = from.PromiseCreate;
  PerformPromiseThen = from.PerformPromiseThen;
  RejectPromise = from.RejectPromise;
  ResolvePromise = from.ResolvePromise;
});

var promiseAwaitHandlerSymbol = utils.ImportNow("promise_await_handler_symbol");
var promiseHandledHintSymbol =
    utils.ImportNow("promise_handled_hint_symbol");

// -------------------------------------------------------------------

function PromiseCastResolved(value) {
  if (IsPromise(value)) {
    return value;
  } else {
    var promise = PromiseCreate();
    ResolvePromise(promise, value);
    return promise;
  }
}

// ES#abstract-ops-async-function-await
// AsyncFunctionAwait ( value )
// Shared logic for the core of await. The parser desugars
//   await awaited
// into
//   yield AsyncFunctionAwait{Caught,Uncaught}(.generator, awaited)
// The 'awaited' parameter is the value; the generator stands in
// for the asyncContext, and mark is metadata for debugging
function AsyncFunctionAwait(generator, awaited, mark) {
  // Promise.resolve(awaited).then(
  //     value => AsyncFunctionNext(value),
  //     error => AsyncFunctionThrow(error)
  // );
  var promise = PromiseCastResolved(awaited);

  var onFulfilled =
      (sentValue) => %_Call(AsyncFunctionNext, generator, sentValue);
  var onRejected =
      (sentError) => %_Call(AsyncFunctionThrow, generator, sentError);

  if (mark && DEBUG_IS_ACTIVE && IsPromise(awaited)) {
    // Mark the reject handler callback such that it does not influence
    // catch prediction.
    SET_PRIVATE(onRejected, promiseAwaitHandlerSymbol, true);
  }

  // Just forwarding the exception, so no debugEvent for throwawayCapability
  var throwawayCapability = NewPromiseCapability(GlobalPromise, false);
  return PerformPromiseThen(promise, onFulfilled, onRejected,
                            throwawayCapability);
}

// Called by the parser from the desugaring of 'await' when catch
// prediction indicates no locally surrounding catch block
function AsyncFunctionAwaitUncaught(generator, awaited) {
  // TODO(littledan): Install a dependency edge from awaited to outerPromise
  return AsyncFunctionAwait(generator, awaited, true);
}

// Called by the parser from the desugaring of 'await' when catch
// prediction indicates that there is a locally surrounding catch block
function AsyncFunctionAwaitCaught(generator, awaited) {
  if (DEBUG_IS_ACTIVE && IsPromise(awaited)) {
    SET_PRIVATE(awaited, promiseHandledHintSymbol, true);
  }
  return AsyncFunctionAwait(generator, awaited, false);
}

// How the parser rejects promises from async/await desugaring
function RejectPromiseNoDebugEvent(promise, reason) {
  return RejectPromise(promise, reason, false);
}

%InstallToContext([
  "async_function_await_caught", AsyncFunctionAwaitCaught,
  "async_function_await_uncaught", AsyncFunctionAwaitUncaught,
  "reject_promise_no_debug_event", RejectPromiseNoDebugEvent,
]);

})
