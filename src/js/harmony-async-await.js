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
var IsPromise;
var GlobalPromise;
var NewPromiseCapability;
var PerformPromiseThen;

utils.Import(function(from) {
  AsyncFunctionNext = from.AsyncFunctionNext;
  AsyncFunctionThrow = from.AsyncFunctionThrow;
  IsPromise = from.IsPromise;
  GlobalPromise = from.GlobalPromise;
  NewPromiseCapability = from.NewPromiseCapability;
  PerformPromiseThen = from.PerformPromiseThen;
});

// -------------------------------------------------------------------

function AsyncFunctionAwait(generator, value) {
  // Promise.resolve(value).then(
  //     value => AsyncFunctionNext(value),
  //     error => AsyncFunctionThrow(error)
  // );
  var promise;
  if (IsPromise(value)) {
    promise = value;
  } else {
    var promiseCapability = NewPromiseCapability(GlobalPromise);
    %_Call(promiseCapability.resolve, UNDEFINED, value);
    promise = promiseCapability.promise;
  }

  var onFulfilled =
      (sentValue) => %_Call(AsyncFunctionNext, generator, sentValue);
  var onRejected =
      (sentError) => %_Call(AsyncFunctionThrow, generator, sentError);

  var throwawayCapability = NewPromiseCapability(GlobalPromise);
  return PerformPromiseThen(promise, onFulfilled, onRejected,
                            throwawayCapability);
}

%InstallToContext([ "async_function_await", AsyncFunctionAwait ]);

})
