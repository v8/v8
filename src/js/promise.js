// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils, extrasUtils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var InternalArray = utils.InternalArray;
var promiseAsyncStackIDSymbol =
    utils.ImportNow("promise_async_stack_id_symbol");
var promiseHandledBySymbol =
    utils.ImportNow("promise_handled_by_symbol");
var promiseForwardingHandlerSymbol =
    utils.ImportNow("promise_forwarding_handler_symbol");
var ObjectHasOwnProperty; // Used by HAS_PRIVATE.
var GlobalPromise = global.Promise;

utils.Import(function(from) {
  ObjectHasOwnProperty = from.ObjectHasOwnProperty;
});

// -------------------------------------------------------------------

// Core functionality.

function PromiseDebugGetInfo(deferred_promise, status) {
  var id, name, instrumenting = DEBUG_IS_ACTIVE;

  if (instrumenting) {
    // In an async function, reuse the existing stack related to the outer
    // Promise. Otherwise, e.g. in a direct call to then, save a new stack.
    // Promises with multiple reactions with one or more of them being async
    // functions will not get a good stack trace, as async functions require
    // different stacks from direct Promise use, but we save and restore a
    // stack once for all reactions. TODO(littledan): Improve this case.
    if (!IS_UNDEFINED(deferred_promise) &&
        HAS_PRIVATE(deferred_promise, promiseHandledBySymbol) &&
        HAS_PRIVATE(GET_PRIVATE(deferred_promise, promiseHandledBySymbol),
                    promiseAsyncStackIDSymbol)) {
      id = GET_PRIVATE(GET_PRIVATE(deferred_promise, promiseHandledBySymbol),
                       promiseAsyncStackIDSymbol);
      name = "async function";
    } else {
      id = %DebugNextMicrotaskId();
      name = status === kFulfilled ? "Promise.resolve" : "Promise.reject";
      %DebugAsyncTaskEvent("enqueue", id, name);
    }
  }
  return [id, name];
}

function PromiseIdResolveHandler(x) { return x; }
function PromiseIdRejectHandler(r) { %_ReThrow(r); }
SET_PRIVATE(PromiseIdRejectHandler, promiseForwardingHandlerSymbol, true);

// -------------------------------------------------------------------
// Define exported functions.

// For bootstrapper.

// This is used by utils and v8-extras.
function PromiseCreate(parent) {
  return %promise_internal_constructor(parent);
}

// Only used by async-await.js
function RejectPromise(promise, reason, debugEvent) {
  %PromiseReject(promise, reason, debugEvent);
}

// Export to bindings
function DoRejectPromise(promise, reason) {
  %PromiseReject(promise, reason, true);
}

// ES#sec-newpromisecapability
// NewPromiseCapability ( C )
function NewPromiseCapability(C, debugEvent) {
  if (C === GlobalPromise) {
    // Optimized case, avoid extra closure.
    var promise = %promise_internal_constructor(UNDEFINED);
    // TODO(gsathya): Remove container for callbacks when this is
    // moved to CPP/TF.
    var callbacks = %create_resolving_functions(promise, debugEvent);
    return {
      promise: promise,
      resolve: callbacks[kResolveCallback],
      reject: callbacks[kRejectCallback]
    };
  }

  var result = {promise: UNDEFINED, resolve: UNDEFINED, reject: UNDEFINED };
  result.promise = new C((resolve, reject) => {
    if (!IS_UNDEFINED(result.resolve) || !IS_UNDEFINED(result.reject))
        throw %make_type_error(kPromiseExecutorAlreadyInvoked);
    result.resolve = resolve;
    result.reject = reject;
  });

  if (!IS_CALLABLE(result.resolve) || !IS_CALLABLE(result.reject))
      throw %make_type_error(kPromiseNonCallable);

  return result;
}

// ES#sec-promise.reject
// Promise.reject ( x )
function PromiseReject(r) {
  if (!IS_RECEIVER(this)) {
    throw %make_type_error(kCalledOnNonObject, PromiseReject);
  }
  if (this === GlobalPromise) {
    // Optimized case, avoid extra closure.
    var promise = %promise_create_and_set(kRejected, r);
    // Trigger debug events if the debugger is on, as Promise.reject is
    // equivalent to throwing an exception directly.
    %PromiseRejectEventFromStack(promise, r);
    return promise;
  } else {
    var promiseCapability = NewPromiseCapability(this, true);
    %_Call(promiseCapability.reject, UNDEFINED, r);
    return promiseCapability.promise;
  }
}

// Combinators.

// ES#sec-promise.resolve
// Promise.resolve ( x )
function PromiseResolve(x) {
  if (!IS_RECEIVER(this)) {
    throw %make_type_error(kCalledOnNonObject, PromiseResolve);
  }
  if (%is_promise(x) && x.constructor === this) return x;

  // Avoid creating resolving functions.
  if (this === GlobalPromise) {
    var promise = %promise_internal_constructor(UNDEFINED);
    %promise_resolve(promise, x);
    return promise;
  }

  // debugEvent is not so meaningful here as it will be resolved
  var promiseCapability = NewPromiseCapability(this, true);
  %_Call(promiseCapability.resolve, UNDEFINED, x);
  return promiseCapability.promise;
}

// ES#sec-promise.all
// Promise.all ( iterable )
function PromiseAll(iterable) {
  if (!IS_RECEIVER(this)) {
    throw %make_type_error(kCalledOnNonObject, "Promise.all");
  }

  // false debugEvent so that forwarding the rejection through all does not
  // trigger redundant ExceptionEvents
  var deferred = NewPromiseCapability(this, false);
  var resolutions = new InternalArray();
  var count;

  // For catch prediction, don't treat the .then calls as handling it;
  // instead, recurse outwards.
  var instrumenting = DEBUG_IS_ACTIVE;
  if (instrumenting) {
    SET_PRIVATE(deferred.reject, promiseForwardingHandlerSymbol, true);
  }

  function CreateResolveElementFunction(index, values, promiseCapability) {
    var alreadyCalled = false;
    return (x) => {
      if (alreadyCalled === true) return;
      alreadyCalled = true;
      values[index] = x;
      if (--count === 0) {
        var valuesArray = [];
        %MoveArrayContents(values, valuesArray);
        %_Call(promiseCapability.resolve, UNDEFINED, valuesArray);
      }
    };
  }

  try {
    var i = 0;
    count = 1;
    for (var value of iterable) {
      var nextPromise = this.resolve(value);
      ++count;
      var throwawayPromise = nextPromise.then(
          CreateResolveElementFunction(i, resolutions, deferred),
          deferred.reject);
      // For catch prediction, mark that rejections here are semantically
      // handled by the combined Promise.
      if (instrumenting && %is_promise(throwawayPromise)) {
        SET_PRIVATE(throwawayPromise, promiseHandledBySymbol, deferred.promise);
      }
      ++i;
    }

    // 6.d
    if (--count === 0) {
      var valuesArray = [];
      %MoveArrayContents(resolutions, valuesArray);
      %_Call(deferred.resolve, UNDEFINED, valuesArray);
    }

  } catch (e) {
    %_Call(deferred.reject, UNDEFINED, e);
  }
  return deferred.promise;
}

// ES#sec-promise.race
// Promise.race ( iterable )
function PromiseRace(iterable) {
  if (!IS_RECEIVER(this)) {
    throw %make_type_error(kCalledOnNonObject, PromiseRace);
  }

  // false debugEvent so that forwarding the rejection through race does not
  // trigger redundant ExceptionEvents
  var deferred = NewPromiseCapability(this, false);

  // For catch prediction, don't treat the .then calls as handling it;
  // instead, recurse outwards.
  var instrumenting = DEBUG_IS_ACTIVE;
  if (instrumenting) {
    SET_PRIVATE(deferred.reject, promiseForwardingHandlerSymbol, true);
  }

  try {
    for (var value of iterable) {
      var throwawayPromise = this.resolve(value).then(deferred.resolve,
                                                      deferred.reject);
      // For catch prediction, mark that rejections here are semantically
      // handled by the combined Promise.
      if (instrumenting && %is_promise(throwawayPromise)) {
        SET_PRIVATE(throwawayPromise, promiseHandledBySymbol, deferred.promise);
      }
    }
  } catch (e) {
    %_Call(deferred.reject, UNDEFINED, e);
  }
  return deferred.promise;
}

function MarkPromiseAsHandled(promise) {
  %PromiseMarkAsHandled(promise);
}

// -------------------------------------------------------------------
// Install exported functions.

utils.InstallFunctions(GlobalPromise, DONT_ENUM, [
  "reject", PromiseReject,
  "all", PromiseAll,
  "race", PromiseRace,
  "resolve", PromiseResolve
]);

%InstallToContext([
  "promise_create", PromiseCreate,
  "promise_reject", DoRejectPromise,
  // TODO(gsathya): Remove this once we update the promise builtin.
  "promise_internal_reject", RejectPromise,
  "promise_debug_get_info", PromiseDebugGetInfo,
  "new_promise_capability", NewPromiseCapability,
  "promise_id_resolve_handler", PromiseIdResolveHandler,
  "promise_id_reject_handler", PromiseIdRejectHandler
]);

// This allows extras to create promises quickly without building extra
// resolve/reject closures, and allows them to later resolve and reject any
// promise without having to hold on to those closures forever.
utils.InstallFunctions(extrasUtils, 0, [
  "createPromise", PromiseCreate,
  "rejectPromise", DoRejectPromise,
  "markPromiseAsHandled", MarkPromiseAsHandled
]);

utils.Export(function(to) {
  to.PromiseCreate = PromiseCreate;

  to.RejectPromise = RejectPromise;
});

})
