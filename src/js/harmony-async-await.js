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

var promiseHandledBySymbol =
    utils.ImportNow("promise_handled_by_symbol");
var promiseForwardingHandlerSymbol =
    utils.ImportNow("promise_forwarding_handler_symbol");
var promiseHandledHintSymbol =
    utils.ImportNow("promise_handled_hint_symbol");
var promiseHasHandlerSymbol =
    utils.ImportNow("promise_has_handler_symbol");

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
//   yield AsyncFunctionAwait{Caught,Uncaught}(.generator, awaited, .promise)
// The 'awaited' parameter is the value; the generator stands in
// for the asyncContext, and .promise is the larger promise under
// construction by the enclosing async function.
function AsyncFunctionAwait(generator, awaited, outerPromise) {
  // Promise.resolve(awaited).then(
  //     value => AsyncFunctionNext(value),
  //     error => AsyncFunctionThrow(error)
  // );
  var promise = PromiseCastResolved(awaited);

  var onFulfilled = sentValue => {
    %_Call(AsyncFunctionNext, generator, sentValue);
    // The resulting Promise is a throwaway, so it doesn't matter what it
    // resolves to. What is important is that we don't end up keeping the
    // whole chain of intermediate Promises alive by returning the value
    // of AsyncFunctionNext, as that would create a memory leak.
    return;
  };
  var onRejected = sentError => {
    %_Call(AsyncFunctionThrow, generator, sentError);
    // Similarly, returning the huge Promise here would cause a long
    // resolution chain to find what the exception to throw is, and
    // create a similar memory leak, and it does not matter what
    // sort of rejection this intermediate Promise becomes.
    return;
  }

  // Just forwarding the exception, so no debugEvent for throwawayCapability
  var throwawayCapability = NewPromiseCapability(GlobalPromise, false);

  // The Promise will be thrown away and not handled, but it shouldn't trigger
  // unhandled reject events as its work is done
  SET_PRIVATE(throwawayCapability.promise, promiseHasHandlerSymbol, true);

  PerformPromiseThen(promise, onFulfilled, onRejected, throwawayCapability);

  if (DEBUG_IS_ACTIVE && !IS_UNDEFINED(outerPromise)) {
    if (IsPromise(awaited)) {
      // Mark the reject handler callback to be a forwarding edge, rather
      // than a meaningful catch handler
      SET_PRIVATE(onRejected, promiseForwardingHandlerSymbol, true);
    }

    // Mark the dependency to outerPromise in case the throwaway Promise is
    // found on the Promise stack
    SET_PRIVATE(throwawayCapability.promise, promiseHandledBySymbol,
                outerPromise);
  }
}

// Called by the parser from the desugaring of 'await' when catch
// prediction indicates no locally surrounding catch block
function AsyncFunctionAwaitUncaught(generator, awaited, outerPromise) {
  AsyncFunctionAwait(generator, awaited, outerPromise);
}

// Called by the parser from the desugaring of 'await' when catch
// prediction indicates that there is a locally surrounding catch block
function AsyncFunctionAwaitCaught(generator, awaited, outerPromise) {
  if (DEBUG_IS_ACTIVE && IsPromise(awaited)) {
    SET_PRIVATE(awaited, promiseHandledHintSymbol, true);
  }
  // Pass undefined for the outer Promise to not waste time setting up
  // or following the dependency chain when this Promise is already marked
  // as handled
  AsyncFunctionAwait(generator, awaited, UNDEFINED);
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
