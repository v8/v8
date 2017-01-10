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
  isolate->RunPromiseHook(PromiseHookType::kResolve, promise,
                          isolate->factory()->undefined_value());

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
  DCHECK_EQ(2, args.length());
  HandleScope scope(isolate);
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, value, 1);

  Handle<Object> rejected_promise = promise;
  if (isolate->debug()->is_active()) {
    // If the Promise.reject call is caught, then this will return
    // undefined, which will be interpreted by PromiseRejectEvent
    // as being a caught exception event.
    rejected_promise = isolate->GetPromiseOnStackOnThrow();
    isolate->debug()->OnAsyncTaskEvent(
        debug::kDebugEnqueueRecurring,
        isolate->debug()->NextAsyncTaskId(promise), kDebugPromiseReject);
  }
  PromiseRejectEvent(isolate, promise, rejected_promise, value, true);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseRevokeReject) {
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  // At this point, no revocation has been issued before
  CHECK(!promise->has_handler());
  isolate->ReportPromiseReject(promise, Handle<Object>(),
                               v8::kPromiseHandlerAddedAfterReject);
  return isolate->heap()->undefined_value();
}

namespace {

// In an async function, reuse the existing stack related to the outer
// Promise. Otherwise, e.g. in a direct call to then, save a new stack.
// Promises with multiple reactions with one or more of them being async
// functions will not get a good stack trace, as async functions require
// different stacks from direct Promise use, but we save and restore a
// stack once for all reactions.
//
// If this isn't a case of async function, we return false, otherwise
// we set the correct id and return true.
//
// TODO(littledan): Improve this case.
bool GetDebugIdForAsyncFunction(Isolate* isolate,
                                Handle<PromiseReactionJobInfo> info,
                                int* debug_id) {
  // deferred_promise can be Undefined, FixedArray or userland promise object.
  if (!info->deferred_promise()->IsJSPromise()) {
    return false;
  }

  Handle<JSPromise> deferred_promise(JSPromise::cast(info->deferred_promise()),
                                     isolate);
  Handle<Symbol> handled_by_symbol =
      isolate->factory()->promise_handled_by_symbol();
  Handle<Object> handled_by_promise =
      JSObject::GetDataProperty(deferred_promise, handled_by_symbol);

  if (!handled_by_promise->IsJSPromise()) {
    return false;
  }

  Handle<JSPromise> handled_by_promise_js =
      Handle<JSPromise>::cast(handled_by_promise);
  Handle<Symbol> async_stack_id_symbol =
      isolate->factory()->promise_async_stack_id_symbol();
  Handle<Object> id =
      JSObject::GetDataProperty(handled_by_promise_js, async_stack_id_symbol);

  // id can be Undefined or Smi.
  if (!id->IsSmi()) {
    return false;
  }

  *debug_id = Handle<Smi>::cast(id)->value();
  return true;
}

void SetDebugInfo(Isolate* isolate, Handle<PromiseReactionJobInfo> info,
                  int status) {
  int id;
  PromiseDebugActionName name;

  if (GetDebugIdForAsyncFunction(isolate, info, &id)) {
    name = kDebugAsyncFunction;
  } else {
    id = isolate->debug()->NextAsyncTaskId(handle(info->promise(), isolate));
    DCHECK(status != v8::Promise::kPending);
    name = status == v8::Promise::kFulfilled ? kDebugPromiseResolve
                                             : kDebugPromiseReject;
  }

  info->set_debug_id(id);
  info->set_debug_name(name);
}

void EnqueuePromiseReactionJob(Isolate* isolate,
                               Handle<PromiseReactionJobInfo> info,
                               int status) {
  if (isolate->debug()->is_active()) {
    SetDebugInfo(isolate, info, status);
  }

  isolate->EnqueueMicrotask(info);
}

void PromiseSet(Isolate* isolate, Handle<JSPromise> promise, int status,
                Handle<Object> result) {
  promise->set_status(status);
  promise->set_result(*result);
  promise->set_deferred_promise(isolate->heap()->undefined_value());
  promise->set_deferred_on_resolve(isolate->heap()->undefined_value());
  promise->set_deferred_on_reject(isolate->heap()->undefined_value());
  promise->set_fulfill_reactions(isolate->heap()->undefined_value());
  promise->set_reject_reactions(isolate->heap()->undefined_value());
}

void PromiseFulfill(Isolate* isolate, Handle<JSPromise> promise, int status,
                    Handle<Object> value) {
  if (isolate->debug()->is_active()) {
    isolate->debug()->OnAsyncTaskEvent(
        debug::kDebugEnqueueRecurring,
        isolate->debug()->NextAsyncTaskId(promise),
        status == v8::Promise::kFulfilled ? kDebugPromiseResolve
                                          : kDebugPromiseReject);
  }
  // Check if there are any callbacks.
  if (!promise->deferred_promise()->IsUndefined(isolate)) {
    Handle<Object> tasks((status == v8::Promise::kFulfilled)
                             ? promise->fulfill_reactions()
                             : promise->reject_reactions(),
                         isolate);
    Handle<PromiseReactionJobInfo> info =
        isolate->factory()->NewPromiseReactionJobInfo(
            promise, value, tasks, handle(promise->deferred_promise(), isolate),
            handle(promise->deferred_on_resolve(), isolate),
            handle(promise->deferred_on_reject(), isolate),
            isolate->native_context());
    EnqueuePromiseReactionJob(isolate, info, status);
  }

  PromiseSet(isolate, promise, status, value);
}

}  // namespace

RUNTIME_FUNCTION(Runtime_PromiseReject) {
  DCHECK_EQ(3, args.length());
  HandleScope scope(isolate);
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, reason, 1);
  CONVERT_BOOLEAN_ARG_CHECKED(debug_event, 2);

  PromiseRejectEvent(isolate, promise, promise, reason, debug_event);
  PromiseFulfill(isolate, promise, v8::Promise::kRejected, reason);

  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_EnqueuePromiseReactionJob) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(PromiseReactionJobInfo, info, 0);
  CONVERT_SMI_ARG_CHECKED(status, 1);
  EnqueuePromiseReactionJob(isolate, info, status);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_EnqueuePromiseResolveThenableJob) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);
  CONVERT_ARG_HANDLE_CHECKED(PromiseResolveThenableJobInfo, info, 0);
  isolate->EnqueueMicrotask(info);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_EnqueueMicrotask) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSFunction, microtask, 0);
  isolate->EnqueueMicrotask(microtask);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_RunMicrotasks) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  isolate->RunMicrotasks();
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseStatus) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);

  return Smi::FromInt(promise->status());
}

RUNTIME_FUNCTION(Runtime_PromiseResult) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  return promise->result();
}

RUNTIME_FUNCTION(Runtime_PromiseMarkAsHandled) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_CHECKED(JSPromise, promise, 0);

  promise->set_has_handler(true);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseMarkHandledHint) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_CHECKED(JSPromise, promise, 0);

  promise->set_handled_hint(true);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseHookInit) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, parent, 1);
  isolate->RunPromiseHook(PromiseHookType::kInit, promise, parent);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseHookResolve) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  isolate->RunPromiseHook(PromiseHookType::kResolve, promise,
                          isolate->factory()->undefined_value());
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseHookBefore) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  isolate->RunPromiseHook(PromiseHookType::kBefore, promise,
                          isolate->factory()->undefined_value());
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseHookAfter) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSPromise, promise, 0);
  isolate->RunPromiseHook(PromiseHookType::kAfter, promise,
                          isolate->factory()->undefined_value());
  return isolate->heap()->undefined_value();
}

}  // namespace internal
}  // namespace v8
