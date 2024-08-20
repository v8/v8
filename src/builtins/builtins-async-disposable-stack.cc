// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/builtins/builtins-utils-inl.h"
#include "src/handles/maybe-handles.h"
#include "src/objects/js-disposable-stack-inl.h"
#include "src/objects/js-disposable-stack.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-promise-inl.h"
#include "src/objects/js-promise.h"
#include "src/objects/objects.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

// Part of
// https://tc39.es/proposal-explicit-resource-management/#sec-getdisposemethod
BUILTIN(AsyncDisposeFromSyncDispose) {
  HandleScope scope(isolate);
  // 1. If hint is async-dispose
  //   b. If GetMethod(V, @@asyncDispose) is undefined,
  //    i. If GetMethod(V, @@dispose) is not undefined, then
  //      1. Let closure be a new Abstract Closure with no parameters that
  //      captures method and performs the following steps when called:
  //        a. Let O be the this value.
  //        b. Let promiseCapability be ! NewPromiseCapability(%Promise%).
  Handle<JSPromise> promise = isolate->factory()->NewJSPromise();

  //        c. Let result be Completion(Call(method, O)).
  Handle<JSFunction> sync_method = Handle<JSFunction>(
      Cast<JSFunction>(isolate->context()->get(static_cast<int>(
          JSDisposableStackBase::AsyncDisposeFromSyncDisposeContextSlots::
              kMethod))),
      isolate);

  v8::TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));
  try_catch.SetVerbose(false);
  try_catch.SetCaptureMessage(false);

  MaybeHandle<Object> result = Execution::Call(
      isolate, sync_method, ReadOnlyRoots(isolate).undefined_value_handle(), 0,
      nullptr);

  Handle<Object> result_handle;

  if (result.ToHandle(&result_handle)) {
    //        e. Perform ? Call(promiseCapability.[[Resolve]], undefined, «
    //        undefined »).
    JSPromise::Resolve(promise, result_handle).ToHandleChecked();
  } else {
    Tagged<Object> exception = isolate->exception();
    if (!isolate->is_catchable_by_javascript(exception)) {
      return {};
    }
    //        d. IfAbruptRejectPromise(result, promiseCapability).
    DCHECK(try_catch.HasCaught());
    JSPromise::Reject(promise, handle(exception, isolate));
  }

  //        f. Return promiseCapability.[[Promise]].
  return *promise;
}

}  // namespace internal
}  // namespace v8
