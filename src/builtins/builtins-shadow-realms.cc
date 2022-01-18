// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/logging/counters.h"
#include "src/objects/js-shadow-realms-inl.h"

namespace v8 {
namespace internal {

// https://tc39.es/proposal-shadowrealm/#sec-shadowrealm-constructor
BUILTIN(ShadowRealmConstructor) {
  HandleScope scope(isolate);
  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (args.new_target()->IsUndefined(isolate)) {  // [[Call]]
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kConstructorNotFunction,
                              isolate->factory()->ShadowRealm_string()));
  }
  // [[Construct]]
  Handle<JSFunction> target = args.target();
  Handle<JSReceiver> new_target = Handle<JSReceiver>::cast(args.new_target());

  // 3. Let realmRec be CreateRealm().
  // 5. Let context be a new execution context.
  // 6. Set the Function of context to null.
  // 7. Set the Realm of context to realmRec.
  // 8. Set the ScriptOrModule of context to null.
  // 10. Perform ? SetRealmGlobalObject(realmRec, undefined, undefined).
  // 11. Perform ? SetDefaultGlobalBindings(O.[[ShadowRealm]]).
  // 12. Perform ? HostInitializeShadowRealm(O.[[ShadowRealm]]).
  // These steps are combined in
  // Isolate::RunHostCreateShadowRealmContextCallback and Context::New.
  // The host operation is hoisted for not creating a half-initialized
  // ShadowRealm object, which can fail the heap verification.
  Handle<NativeContext> native_context;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, native_context,
      isolate->RunHostCreateShadowRealmContextCallback());

  // 2. Let O be ? OrdinaryCreateFromConstructor(NewTarget,
  // "%ShadowRealm.prototype%", « [[ShadowRealm]], [[ExecutionContext]] »).
  Handle<JSObject> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      JSObject::New(target, new_target, Handle<AllocationSite>::null()));
  Handle<JSShadowRealm> O = Handle<JSShadowRealm>::cast(result);

  // 4. Set O.[[ShadowRealm]] to realmRec.
  // 9. Set O.[[ExecutionContext]] to context.
  O->set_native_context(*native_context);

  // 13. Return O.
  return *O;
}

// https://tc39.es/proposal-shadowrealm/#sec-shadowrealm.prototype.evaluate
BUILTIN(ShadowRealmPrototypeEvaluate) {
  HandleScope scope(isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

// https://tc39.es/proposal-shadowrealm/#sec-shadowrealm.prototype.importvalue
BUILTIN(ShadowRealmPrototypeImportValue) {
  HandleScope scope(isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

}  // namespace internal
}  // namespace v8
