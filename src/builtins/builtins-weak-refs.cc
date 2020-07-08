// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/logging/counters.h"
#include "src/objects/js-weak-refs-inl.h"

namespace v8 {
namespace internal {

BUILTIN(FinalizationRegistryRegister) {
  HandleScope scope(isolate);
  const char* method_name = "FinalizationRegistry.prototype.register";

  //  1. Let finalizationGroup be the this value.
  //
  //  2. If Type(finalizationGroup) is not Object, throw a TypeError
  //  exception.
  //
  //  4. If finalizationGroup does not have a [[Cells]] internal slot,
  //  throw a TypeError exception.
  CHECK_RECEIVER(JSFinalizationRegistry, finalization_registry, method_name);

  Handle<Object> target = args.atOrUndefined(isolate, 1);

  //  3. If Type(target) is not Object, throw a TypeError exception.
  if (!target->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kWeakRefsRegisterTargetMustBeObject));
  }

  Handle<Object> holdings = args.atOrUndefined(isolate, 2);
  if (target->SameValue(*holdings)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(
            MessageTemplate::kWeakRefsRegisterTargetAndHoldingsMustNotBeSame));
  }

  Handle<Object> unregister_token = args.atOrUndefined(isolate, 3);

  //  5. If Type(unregisterToken) is not Object,
  //    a. If unregisterToken is not undefined, throw a TypeError exception.
  if (!unregister_token->IsJSReceiver() && !unregister_token->IsUndefined()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kWeakRefsUnregisterTokenMustBeObject,
                     unregister_token));
  }
  // TODO(marja): Realms.

  JSFinalizationRegistry::Register(finalization_registry,
                                   Handle<JSReceiver>::cast(target), holdings,
                                   unregister_token, isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

BUILTIN(FinalizationRegistryUnregister) {
  HandleScope scope(isolate);
  const char* method_name = "FinalizationRegistry.prototype.unregister";

  // 1. Let finalizationGroup be the this value.
  //
  // 2. If Type(finalizationGroup) is not Object, throw a TypeError
  //    exception.
  //
  // 3. If finalizationGroup does not have a [[Cells]] internal slot,
  //    throw a TypeError exception.
  CHECK_RECEIVER(JSFinalizationRegistry, finalization_registry, method_name);

  Handle<Object> unregister_token = args.atOrUndefined(isolate, 1);

  // 4. If Type(unregisterToken) is not Object, throw a TypeError exception.
  if (!unregister_token->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kWeakRefsUnregisterTokenMustBeObject,
                     unregister_token));
  }

  bool success = JSFinalizationRegistry::Unregister(
      finalization_registry, Handle<JSReceiver>::cast(unregister_token),
      isolate);

  return *isolate->factory()->ToBoolean(success);
}

}  // namespace internal
}  // namespace v8
