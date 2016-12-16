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
var promiseHandledHintSymbol =
    utils.ImportNow("promise_handled_hint_symbol");
var promiseRawSymbol = utils.ImportNow("promise_raw_symbol");
var promiseStateSymbol = utils.ImportNow("promise_state_symbol");
var promiseResultSymbol = utils.ImportNow("promise_result_symbol");
var SpeciesConstructor;
var speciesSymbol = utils.ImportNow("species_symbol");
var toStringTagSymbol = utils.ImportNow("to_string_tag_symbol");
var ObjectHasOwnProperty;
var GlobalPromise = global.Promise;
var PromiseThen = GlobalPromise.prototype.then;

utils.Import(function(from) {
  ObjectHasOwnProperty = from.ObjectHasOwnProperty;
  SpeciesConstructor = from.SpeciesConstructor;
});

// -------------------------------------------------------------------

// Core functionality.

function PromiseDebugGetInfo(deferreds, status) {
  var id, name, instrumenting = DEBUG_IS_ACTIVE;

  if (instrumenting) {
    // In an async function, reuse the existing stack related to the outer
    // Promise. Otherwise, e.g. in a direct call to then, save a new stack.
    // Promises with multiple reactions with one or more of them being async
    // functions will not get a good stack trace, as async functions require
    // different stacks from direct Promise use, but we save and restore a
    // stack once for all reactions. TODO(littledan): Improve this case.
    if (!IS_UNDEFINED(deferreds) &&
        HAS_PRIVATE(deferreds.promise, promiseHandledBySymbol) &&
        HAS_PRIVATE(GET_PRIVATE(deferreds.promise, promiseHandledBySymbol),
                    promiseAsyncStackIDSymbol)) {
      id = GET_PRIVATE(GET_PRIVATE(deferreds.promise, promiseHandledBySymbol),
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
function PromiseCreate() {
  return %promise_internal_constructor(UNDEFINED);
}

// Only used by async-await.js
function RejectPromise(promise, reason, debugEvent) {
  %PromiseReject(promise, reason, debugEvent);
}

// Export to bindings
function DoRejectPromise(promise, reason) {
  %PromiseReject(promise, reason, true);
}

// The resultCapability.promise is only ever fulfilled internally,
// so we don't need the closures to protect against accidentally
// calling them multiple times.
function CreateInternalPromiseCapability(parent) {
  return {
    promise: %promise_internal_constructor(parent),
    resolve: UNDEFINED,
    reject: UNDEFINED
  };
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
    throw %make_type_error(kCalledOnNonObject, PromiseResolve);
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

// ES#sec-promise.prototype.catch
// Promise.prototype.catch ( onRejected )
function PromiseCatch(onReject) {
  return this.then(UNDEFINED, onReject);
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


// Utility for debugger

function PromiseHasUserDefinedRejectHandlerCheck(handler, deferred) {
  // Recurse to the forwarding Promise, if any. This may be due to
  //  - await reaction forwarding to the throwaway Promise, which has
  //    a dependency edge to the outer Promise.
  //  - PromiseIdResolveHandler forwarding to the output of .then
  //  - Promise.all/Promise.race forwarding to a throwaway Promise, which
  //    has a dependency edge to the generated outer Promise.
  if (GET_PRIVATE(handler, promiseForwardingHandlerSymbol)) {
    return PromiseHasUserDefinedRejectHandlerRecursive(deferred.promise);
  }

  // Otherwise, this is a real reject handler for the Promise
  return true;
}

function PromiseHasUserDefinedRejectHandlerRecursive(promise) {
  // If this promise was marked as being handled by a catch block
  // in an async function, then it has a user-defined reject handler.
  if (GET_PRIVATE(promise, promiseHandledHintSymbol)) return true;

  // If this Promise is subsumed by another Promise (a Promise resolved
  // with another Promise, or an intermediate, hidden, throwaway Promise
  // within async/await), then recurse on the outer Promise.
  // In this case, the dependency is one possible way that the Promise
  // could be resolved, so it does not subsume the other following cases.
  var outerPromise = GET_PRIVATE(promise, promiseHandledBySymbol);
  if (outerPromise &&
      PromiseHasUserDefinedRejectHandlerRecursive(outerPromise)) {
    return true;
  }

  if (!%is_promise(promise)) return false;

  var queue = %PromiseRejectReactions(promise);
  var deferred = %PromiseDeferred(promise);

  if (IS_UNDEFINED(queue)) return false;

  if (!IS_ARRAY(queue)) {
    return PromiseHasUserDefinedRejectHandlerCheck(queue, deferred);
  }

  for (var i = 0; i < queue.length; i++) {
    if (PromiseHasUserDefinedRejectHandlerCheck(queue[i], deferred[i])) {
      return true;
    }
  }
  return false;
}

// Return whether the promise will be handled by a user-defined reject
// handler somewhere down the promise chain. For this, we do a depth-first
// search for a reject handler that's not the default PromiseIdRejectHandler.
// This function also traverses dependencies of one Promise on another,
// set up through async/await and Promises resolved with Promises.
function PromiseHasUserDefinedRejectHandler() {
  return PromiseHasUserDefinedRejectHandlerRecursive(this);
};

function MarkPromiseAsHandled(promise) {
  %PromiseMarkAsHandled(promise);
}


function PromiseSpecies() {
  return this;
}

// -------------------------------------------------------------------
// Install exported functions.

utils.InstallFunctions(GlobalPromise, DONT_ENUM, [
  "reject", PromiseReject,
  "all", PromiseAll,
  "race", PromiseRace,
  "resolve", PromiseResolve
]);

utils.InstallGetter(GlobalPromise, speciesSymbol, PromiseSpecies);

%SetCode(GlobalPromise.prototype.catch, PromiseCatch);

%InstallToContext([
  "promise_catch", GlobalPromise.prototype.catch,
  "promise_create", PromiseCreate,
  "promise_has_user_defined_reject_handler", PromiseHasUserDefinedRejectHandler,
  "promise_reject", DoRejectPromise,
  // TODO(gsathya): Remove this once we update the promise builtin.
  "promise_internal_reject", RejectPromise,
  "promise_debug_get_info", PromiseDebugGetInfo,
  "new_promise_capability", NewPromiseCapability,
  "internal_promise_capability", CreateInternalPromiseCapability,
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
  to.PromiseThen = PromiseThen;

  to.CreateInternalPromiseCapability = CreateInternalPromiseCapability;
  to.RejectPromise = RejectPromise;
});

})
