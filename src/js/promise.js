// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils, extrasUtils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var InternalArray = utils.InternalArray;
var promiseHandledBySymbol =
    utils.ImportNow("promise_handled_by_symbol");
var promiseForwardingHandlerSymbol =
    utils.ImportNow("promise_forwarding_handler_symbol");
var GlobalPromise = global.Promise;

// -------------------------------------------------------------------
// Define exported functions.

// Combinators.

// ES#sec-promise.race
// Promise.race ( iterable )
DEFINE_METHOD(
  GlobalPromise,
  race(iterable) {
    if (!IS_RECEIVER(this)) {
      throw %make_type_error(kCalledOnNonObject, this);
    }

    // false debugEvent so that forwarding the rejection through race does not
    // trigger redundant ExceptionEvents
    var deferred = %new_promise_capability(this, false);

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
);

})
