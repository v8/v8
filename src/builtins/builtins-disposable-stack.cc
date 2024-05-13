// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-disposable-stack-inl.h"
#include "src/objects/js-disposable-stack.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {

// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-disposablestack
BUILTIN(DisposableStackConstructor) {
  const char* const kMethodName = "DisposableStack";
  HandleScope scope(isolate);

  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (IsUndefined(*args.new_target(), isolate)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kConstructorNotFunction,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  kMethodName)));
  }

  // 2. Let disposableStack be ? OrdinaryCreateFromConstructor(NewTarget,
  //    "%DisposableStack.prototype%", « [[DisposableState]],
  //    [[DisposeCapability]] »).
  // 3. Set disposableStack.[[DisposableState]] to pending.
  // 4. Set disposableStack.[[DisposeCapability]] to NewDisposeCapability().
  // 5. Return disposableStack.
  return *isolate->factory()->NewJSDisposableStack();
}

// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-disposablestack.prototype.use
BUILTIN(DisposableStackPrototypeUse) {
  const char* const kMethodName = "DisposableStack.prototype.use";
  HandleScope scope(isolate);

  // 1. Let disposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(disposableStack, [[DisposableState]]).
  CHECK_RECEIVER(JSDisposableStack, disposableStack, kMethodName);
  Handle<Object> value = args.at(1);

  // use(value) does nothing when the value is null or undefined, so return
  // early.
  if (IsNullOrUndefined(*value)) {
    return *value;
  }

  // 3. If disposableStack.[[DisposableState]] is disposed, throw a
  //    ReferenceError exception.
  if (disposableStack->state() == DisposableStackState::kDisposed) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewReferenceError(MessageTemplate::kDisposableStackIsDisposed));
  }

  Handle<Object> method;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, method,
      JSDisposableStack::CheckValueAndGetDisposeMethod(isolate, value));

  // 4. Perform ? AddDisposableResource(disposableStack.[[DisposeCapability]],
  //    value, sync-dispose).
  JSDisposableStack::Add(isolate, disposableStack, value, method,
                         DisposeMethodCallType::kValueIsReceiver);

  // 5. Return value.
  return *value;
}

BUILTIN(DisposableStackPrototypeDispose) {
  const char* const kMethodName = "DisposableStack.prototype.dispose";
  HandleScope scope(isolate);

  // 1. Let disposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(disposableStack, [[DisposableState]]).
  CHECK_RECEIVER(JSDisposableStack, disposableStack, kMethodName);

  // 3. If disposableStack.[[DisposableState]] is disposed, return undefined.
  if (disposableStack->state() == DisposableStackState::kDisposed) {
    return ReadOnlyRoots(isolate).undefined_value();
  }

  // 4. Set disposableStack.[[DisposableState]] to disposed.
  // Will be done in DisposeResources call.

  // 5. Return ? DisposeResources(disposableStack.[[DisposeCapability]],
  //    NormalCompletion(undefined)).
  MAYBE_RETURN(JSDisposableStack::DisposeResources(isolate, disposableStack,
                                                   MaybeHandle<Object>()),
               ReadOnlyRoots(isolate).exception());
  return ReadOnlyRoots(isolate).undefined_value();
}

// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-disposablestack.prototype.adopt
BUILTIN(DisposableStackPrototypeAdopt) {
  const char* const kMethodName = "DisposableStack.prototype.adopt";
  HandleScope scope(isolate);
  Handle<Object> value = args.at(1);
  Handle<Object> onDispose = args.at(2);

  // 1. Let disposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(disposableStack, [[DisposableState]]).
  CHECK_RECEIVER(JSDisposableStack, disposableStack, kMethodName);

  // 3. If disposableStack.[[DisposableState]] is disposed, throw a
  //    ReferenceError exception.
  if (disposableStack->state() == DisposableStackState::kDisposed) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewReferenceError(MessageTemplate::kDisposableStackIsDisposed));
  }

  // 4. If IsCallable(onDispose) is false, throw a TypeError exception.
  if (!IsCallable(*onDispose)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, onDispose));
  }

  // 5. Let closure be a new Abstract Closure with no parameters that captures
  //    value and onDispose and performs the following steps when called:
  //      a. Return ? Call(onDispose, undefined, « value »).
  // 6. Let F be CreateBuiltinFunction(closure, 0, "", « »).
  // 7. Perform ? AddDisposableResource(disposableStack.[[DisposeCapability]],
  //    undefined, sync-dispose, F).
  // Instead of creating an abstract closure and a function, we pass
  // DisposeMethodCallType::kArgument so at the time of disposal, the value will
  // be passed as the argument to the method.
  JSDisposableStack::Add(isolate, disposableStack, value, onDispose,
                         DisposeMethodCallType::kValueIsArgument);

  // 8. Return value.
  return *value;
}

// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-disposablestack.prototype.defer
BUILTIN(DisposableStackPrototypeDefer) {
  const char* const kMethodName = "DisposableStack.prototype.defer";
  HandleScope scope(isolate);
  Handle<Object> onDispose = args.at(1);

  // 1. Let disposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(disposableStack, [[DisposableState]]).
  CHECK_RECEIVER(JSDisposableStack, disposableStack, kMethodName);

  // 3. If disposableStack.[[DisposableState]] is disposed, throw a
  // ReferenceError exception.
  if (disposableStack->state() == DisposableStackState::kDisposed) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewReferenceError(MessageTemplate::kDisposableStackIsDisposed));
  }

  // 4. If IsCallable(onDispose) is false, throw a TypeError exception.
  if (!IsCallable(*onDispose)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, onDispose));
  }

  // 5. Perform ? AddDisposableResource(disposableStack.[[DisposeCapability]],
  // undefined, sync-dispose, onDispose).
  JSDisposableStack::Add(isolate, disposableStack,
                         ReadOnlyRoots(isolate).undefined_value_handle(),
                         onDispose, DisposeMethodCallType::kValueIsReceiver);

  // 6. Return undefined.
  return ReadOnlyRoots(isolate).undefined_value();
}

}  // namespace internal
}  // namespace v8
