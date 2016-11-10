// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"

namespace v8 {
namespace internal {

enum PromiseResolvingFunctionContextSlot {
  kAlreadyVisitedSlot = Context::MIN_CONTEXT_SLOTS,
  kPromiseSlot,
  kDebugEventSlot,
  kPromiseContextLength,
};

// ES#sec-promise-resolve-functions
// Promise Resolve Functions
BUILTIN(PromiseResolveClosure) {
  HandleScope scope(isolate);

  Handle<Context> context(isolate->context(), isolate);
  Handle<Smi> already_visited(Smi::cast(context->get(kAlreadyVisitedSlot)),
                              isolate);

  if (already_visited->value() != 0) {
    return isolate->heap()->undefined_value();
  }

  context->set(kAlreadyVisitedSlot, Smi::FromInt(1));
  Handle<JSObject> promise(JSObject::cast(context->get(kPromiseSlot)), isolate);
  Handle<Object> value = args.atOrUndefined(isolate, 1);

  MaybeHandle<Object> maybe_result;
  Handle<Object> argv[] = {promise, value};
  RETURN_FAILURE_ON_EXCEPTION(
      isolate, Execution::Call(isolate, isolate->promise_resolve(),
                               isolate->factory()->undefined_value(),
                               arraysize(argv), argv));
  return isolate->heap()->undefined_value();
}

// ES#sec-promise-reject-functions
// Promise Reject Functions
BUILTIN(PromiseRejectClosure) {
  HandleScope scope(isolate);

  Handle<Context> context(isolate->context(), isolate);
  Handle<Smi> already_visited(Smi::cast(context->get(kAlreadyVisitedSlot)),
                              isolate);

  if (already_visited->value() != 0) {
    return isolate->heap()->undefined_value();
  }

  context->set(kAlreadyVisitedSlot, Smi::FromInt(1));

  Handle<Object> value = args.atOrUndefined(isolate, 1);
  Handle<JSObject> promise(JSObject::cast(context->get(kPromiseSlot)), isolate);
  Handle<Object> debug_event(context->get(kDebugEventSlot), isolate);
  MaybeHandle<Object> maybe_result;
  Handle<Object> argv[] = {promise, value, debug_event};
  RETURN_FAILURE_ON_EXCEPTION(
      isolate, Execution::Call(isolate, isolate->promise_internal_reject(),
                               isolate->factory()->undefined_value(),
                               arraysize(argv), argv));
  return isolate->heap()->undefined_value();
}

// ES#sec-createresolvingfunctions
// CreateResolvingFunctions ( promise )
BUILTIN(CreateResolvingFunctions) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());

  Handle<JSObject> promise = args.at<JSObject>(1);
  Handle<Object> debug_event = args.at<Object>(2);

  Handle<Context> context =
      isolate->factory()->NewPromiseResolvingFunctionContext(
          kPromiseContextLength);
  context->set_native_context(*isolate->native_context());
  context->set(kAlreadyVisitedSlot, Smi::kZero);
  context->set(kPromiseSlot, *promise);
  context->set(kDebugEventSlot, *debug_event);

  Handle<SharedFunctionInfo> resolve_shared_fun(
      isolate->native_context()->promise_resolve_shared_fun(), isolate);
  Handle<JSFunction> resolve =
      isolate->factory()->NewFunctionFromSharedFunctionInfo(
          isolate->sloppy_function_without_prototype_map(), resolve_shared_fun,
          isolate->native_context(), TENURED);

  Handle<SharedFunctionInfo> reject_shared_fun(
      isolate->native_context()->promise_reject_shared_fun(), isolate);
  Handle<JSFunction> reject =
      isolate->factory()->NewFunctionFromSharedFunctionInfo(
          isolate->sloppy_function_without_prototype_map(), reject_shared_fun,
          isolate->native_context(), TENURED);

  resolve->set_context(*context);
  reject->set_context(*context);

  Handle<FixedArray> result = isolate->factory()->NewFixedArray(2);
  result->set(0, *resolve);
  result->set(1, *reject);

  return *isolate->factory()->NewJSArrayWithElements(result, FAST_ELEMENTS, 2,
                                                     NOT_TENURED);
}

}  // namespace internal
}  // namespace v8
