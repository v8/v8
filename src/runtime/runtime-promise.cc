// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/runtime/runtime-utils.h"

#include "src/debug/debug.h"
#include "src/elements.h"
#include "src/promise-utils.h"

namespace v8 {
namespace internal {

namespace {

void PromiseRejectEvent(Isolate* isolate, Handle<JSPromise> promise,
                        Handle<Object> rejected_promise, Handle<Object> value,
                        bool debug_event) {
  if (isolate->debug()->is_active() && debug_event) {
    isolate->debug()->OnPromiseReject(rejected_promise, value);
  }

  // Report only if we don't actually have a handler.
  if (!promise->has_handler()) {
    isolate->ReportPromiseReject(Handle<JSObject>::cast(promise), value,
                                 v8::kPromiseRejectWithNoHandler);
  }
}

}  // namespace

RUNTIME_FUNCTION(Runtime_PromiseRejectEventFromStack) {
  DCHECK(args.length() == 2);
  HandleScope scope(isolate);
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, value, 1);

  Handle<Object> rejected_promise = promise;
  if (isolate->debug()->is_active()) {
    // If the Promise.reject call is caught, then this will return
    // undefined, which will be interpreted by PromiseRejectEvent
    // as being a caught exception event.
    rejected_promise = isolate->GetPromiseOnStackOnThrow();
  }
  PromiseRejectEvent(isolate, promise, rejected_promise, value, true);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseRevokeReject) {
  DCHECK(args.length() == 1);
  HandleScope scope(isolate);
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  // At this point, no revocation has been issued before
  CHECK(!promise->has_handler());
  isolate->ReportPromiseReject(promise, Handle<Object>(),
                               v8::kPromiseHandlerAddedAfterReject);
  return isolate->heap()->undefined_value();
}

namespace {
void EnqueuePromiseReactionJob(Isolate* isolate, Handle<Object> value,
                               Handle<Object> tasks, Handle<Object> deferred,
                               Handle<Object> status) {
  Handle<Object> debug_id = isolate->factory()->undefined_value();
  Handle<Object> debug_name = isolate->factory()->undefined_value();
  if (isolate->debug()->is_active()) {
    MaybeHandle<Object> maybe_result;
    Handle<Object> deferred_obj(deferred);

    if (deferred->IsFixedArray()) {
      deferred_obj = isolate->factory()->undefined_value();
    }

    Handle<Object> argv[] = {deferred_obj, status};
    maybe_result = Execution::TryCall(
        isolate, isolate->promise_debug_get_info(),
        isolate->factory()->undefined_value(), arraysize(argv), argv);

    Handle<Object> result;
    if ((maybe_result).ToHandle(&result)) {
      CHECK(result->IsJSArray());
      Handle<JSArray> array = Handle<JSArray>::cast(result);
      ElementsAccessor* accessor = array->GetElementsAccessor();
      DCHECK(accessor->HasElement(array, 0));
      DCHECK(accessor->HasElement(array, 1));
      debug_id = accessor->Get(array, 0);
      debug_name = accessor->Get(array, 1);
    }
  }
  Handle<PromiseReactionJobInfo> info =
      isolate->factory()->NewPromiseReactionJobInfo(value, tasks, deferred,
                                                    debug_id, debug_name,
                                                    isolate->native_context());
  isolate->EnqueueMicrotask(info);
}

void PromiseSet(Isolate* isolate, Handle<JSPromise> promise, int status,
                Handle<Object> result) {
  promise->set_status(status);
  promise->set_result(*result);
  promise->set_deferred(isolate->heap()->undefined_value());
  promise->set_fulfill_reactions(isolate->heap()->undefined_value());
  promise->set_reject_reactions(isolate->heap()->undefined_value());
}

void PromiseFulfill(Isolate* isolate, Handle<JSPromise> promise,
                    Handle<Smi> status, Handle<Object> value) {
  // Check if there are any callbacks.
  if (!promise->deferred()->IsUndefined(isolate)) {
    Handle<Object> tasks((status->value() == kPromiseFulfilled)
                             ? promise->fulfill_reactions()
                             : promise->reject_reactions(),
                         isolate);
    Handle<Object> deferred(promise->deferred(), isolate);
    EnqueuePromiseReactionJob(isolate, value, tasks, deferred, status);
  }

  PromiseSet(isolate, promise, status->value(), value);
}

}  // namespace

RUNTIME_FUNCTION(Runtime_PromiseReject) {
  DCHECK(args.length() == 3);
  HandleScope scope(isolate);
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, reason, 1);
  CONVERT_BOOLEAN_ARG_CHECKED(debug_event, 2);

  PromiseRejectEvent(isolate, promise, promise, reason, debug_event);

  Handle<Smi> status(Smi::FromInt(kPromiseRejected), isolate);
  PromiseFulfill(isolate, promise, status, reason);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseFulfill) {
  DCHECK(args.length() == 3);
  HandleScope scope(isolate);
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  CONVERT_ARG_HANDLE_CHECKED(Smi, status, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, value, 2);
  PromiseFulfill(isolate, promise, status, value);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_EnqueuePromiseReactionJob) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 4);
  CONVERT_ARG_HANDLE_CHECKED(Object, value, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, tasks, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, deferred, 2);
  CONVERT_ARG_HANDLE_CHECKED(Object, status, 3);
  EnqueuePromiseReactionJob(isolate, value, tasks, deferred, status);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_EnqueuePromiseResolveThenableJob) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);
  CONVERT_ARG_HANDLE_CHECKED(JSObject, promise, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSReceiver, resolution, 1);
  CONVERT_ARG_HANDLE_CHECKED(JSReceiver, then, 2);

  // TODO(gsathya): Add fast path for native promises with unmodified
  // PromiseThen (which don't need these resolving functions, but
  // instead can just call resolve/reject directly).
  Handle<JSFunction> resolve, reject;
  PromiseUtils::CreateResolvingFunctions(
      isolate, promise, isolate->factory()->false_value(), &resolve, &reject);

  Handle<Object> debug_id, debug_name;
  if (isolate->debug()->is_active()) {
    debug_id =
        handle(Smi::FromInt(isolate->GetNextDebugMicrotaskId()), isolate);
    debug_name = isolate->factory()->PromiseResolveThenableJob_string();
    isolate->debug()->OnAsyncTaskEvent(isolate->factory()->enqueue_string(),
                                       debug_id,
                                       Handle<String>::cast(debug_name));
  } else {
    debug_id = isolate->factory()->undefined_value();
    debug_name = isolate->factory()->undefined_value();
  }

  Handle<PromiseResolveThenableJobInfo> info =
      isolate->factory()->NewPromiseResolveThenableJobInfo(
          resolution, then, resolve, reject, debug_id, debug_name,
          isolate->native_context());
  isolate->EnqueueMicrotask(info);

  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_EnqueueMicrotask) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);
  CONVERT_ARG_HANDLE_CHECKED(JSFunction, microtask, 0);
  isolate->EnqueueMicrotask(microtask);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_RunMicrotasks) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 0);
  isolate->RunMicrotasks();
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_CreateResolvingFunctions) {
  HandleScope scope(isolate);
  CONVERT_ARG_HANDLE_CHECKED(JSObject, promise, 0);
  DCHECK(args.length() == 1);
  Handle<JSFunction> resolve, reject;

  PromiseUtils::CreateResolvingFunctions(
      isolate, promise, isolate->factory()->true_value(), &resolve, &reject);

  Handle<FixedArray> result = isolate->factory()->NewFixedArray(2);
  result->set(0, *resolve);
  result->set(1, *reject);

  return *result;
}

RUNTIME_FUNCTION(Runtime_PromiseStatus) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);

  return Smi::FromInt(promise->status());
}

RUNTIME_FUNCTION(Runtime_PromiseResult) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  return promise->result();
}

RUNTIME_FUNCTION(Runtime_PromiseDeferred) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);

  Handle<Object> deferred(promise->deferred(), isolate);
  if (deferred->IsUndefined(isolate)) {
    return isolate->heap()->undefined_value();
  }

  if (deferred->IsJSObject()) {
    return *deferred;
  }

  DCHECK(deferred->IsFixedArray());
  return *isolate->factory()->NewJSArrayWithElements(
      Handle<FixedArray>::cast(deferred));
}

RUNTIME_FUNCTION(Runtime_PromiseRejectReactions) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);

  Handle<Object> reject_reactions(promise->reject_reactions(), isolate);
  if (reject_reactions->IsUndefined(isolate)) {
    return isolate->heap()->undefined_value();
  }

  if (reject_reactions->IsJSObject()) {
    return *reject_reactions;
  }

  DCHECK(reject_reactions->IsFixedArray());
  return *isolate->factory()->NewJSArrayWithElements(
      Handle<FixedArray>::cast(reject_reactions));
}

RUNTIME_FUNCTION(Runtime_PromiseMarkAsHandled) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);

  promise->set_has_handler(true);
  return isolate->heap()->undefined_value();
}

}  // namespace internal
}  // namespace v8
