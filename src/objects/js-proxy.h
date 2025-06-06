// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_PROXY_H_
#define V8_OBJECTS_JS_PROXY_H_

#include "src/objects/js-objects.h"
#include "src/objects/oddball.h"
#include "torque-generated/builtin-definitions.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class KeyAccumulator;

#include "torque-generated/src/objects/js-proxy-tq.inc"

// The JSProxy describes ECMAScript Harmony proxies
class JSProxy : public TorqueGeneratedJSProxy<JSProxy, JSReceiver> {
 public:
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSProxy> New(
      Isolate* isolate, DirectHandle<Object>, DirectHandle<Object>,
      bool revocable);

  V8_INLINE bool is_revocable() const;
  V8_INLINE bool IsRevoked() const;
  static void Revoke(DirectHandle<JSProxy> proxy);

  // ES6 9.5.1
  static MaybeDirectHandle<JSPrototype> GetPrototype(
      DirectHandle<JSProxy> receiver);

  // ES6 9.5.2
  V8_WARN_UNUSED_RESULT static Maybe<bool> SetPrototype(
      Isolate* isolate, DirectHandle<JSProxy> proxy, DirectHandle<Object> value,
      bool from_javascript, ShouldThrow should_throw);
  // ES6 9.5.3
  V8_WARN_UNUSED_RESULT static Maybe<bool> IsExtensible(
      DirectHandle<JSProxy> proxy);

  // ES6, #sec-isarray.  NOT to be confused with %_IsArray.
  V8_WARN_UNUSED_RESULT static Maybe<bool> IsArray(DirectHandle<JSProxy> proxy);

  // ES6 9.5.4 (when passed kDontThrow)
  V8_WARN_UNUSED_RESULT static Maybe<bool> PreventExtensions(
      DirectHandle<JSProxy> proxy, ShouldThrow should_throw);

  // ES6 9.5.5
  V8_WARN_UNUSED_RESULT static Maybe<bool> GetOwnPropertyDescriptor(
      Isolate* isolate, DirectHandle<JSProxy> proxy, DirectHandle<Name> name,
      PropertyDescriptor* desc);

  // ES6 9.5.6
  V8_WARN_UNUSED_RESULT static Maybe<bool> DefineOwnProperty(
      Isolate* isolate, DirectHandle<JSProxy> object, DirectHandle<Object> key,
      PropertyDescriptor* desc, Maybe<ShouldThrow> should_throw);

  // ES6 9.5.7
  V8_WARN_UNUSED_RESULT static Maybe<bool> HasProperty(
      Isolate* isolate, DirectHandle<JSProxy> proxy, DirectHandle<Name> name);

  // This function never returns false.
  // It returns either true or throws.
  V8_WARN_UNUSED_RESULT static Maybe<bool> CheckHasTrap(
      Isolate* isolate, DirectHandle<Name> name,
      DirectHandle<JSReceiver> target);

  // ES6 9.5.10
  V8_WARN_UNUSED_RESULT static Maybe<bool> CheckDeleteTrap(
      Isolate* isolate, DirectHandle<Name> name,
      DirectHandle<JSReceiver> target);

  // ES6 9.5.8
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSAny> GetProperty(
      Isolate* isolate, DirectHandle<JSProxy> proxy, DirectHandle<Name> name,
      DirectHandle<JSAny> receiver, bool* was_found);

  enum AccessKind { kGet, kSet };

  static MaybeHandle<JSAny> CheckGetSetTrapResult(
      Isolate* isolate, DirectHandle<Name> name,
      DirectHandle<JSReceiver> target, DirectHandle<Object> trap_result,
      AccessKind access_kind);

  // ES6 9.5.9
  V8_WARN_UNUSED_RESULT static Maybe<bool> SetProperty(
      DirectHandle<JSProxy> proxy, DirectHandle<Name> name,
      DirectHandle<Object> value, DirectHandle<JSAny> receiver,
      Maybe<ShouldThrow> should_throw);

  // ES6 9.5.10 (when passed LanguageMode::kSloppy)
  V8_WARN_UNUSED_RESULT static Maybe<bool> DeletePropertyOrElement(
      DirectHandle<JSProxy> proxy, DirectHandle<Name> name,
      LanguageMode language_mode);

  V8_WARN_UNUSED_RESULT static Maybe<PropertyAttributes> GetPropertyAttributes(
      LookupIterator* it);

  // Dispatched behavior.
  DECL_VERIFIER(JSProxy)

  static const int kMaxIterationLimit = 100 * 1024;

  // kTargetOffset aliases with the elements of JSObject. The fact that
  // JSProxy::target is a Javascript value which cannot be confused with an
  // elements backing store is exploited by loading from this offset from an
  // unknown JSReceiver.
  static_assert(static_cast<int>(JSObject::kElementsOffset) ==
                static_cast<int>(JSProxy::kTargetOffset));

  class BodyDescriptor;

  static Maybe<bool> SetPrivateSymbol(Isolate* isolate,
                                      DirectHandle<JSProxy> proxy,
                                      DirectHandle<Symbol> private_name,
                                      PropertyDescriptor* desc,
                                      Maybe<ShouldThrow> should_throw);

  using IsRevocableBit = base::BitField<bool, 0, 1>;

  static constexpr int kIsRevocableBit = IsRevocableBit::encode(true);

  TQ_OBJECT_CONSTRUCTORS(JSProxy)
};

// JSProxyRevocableResult is just a JSObject with a specific initial map.
// This initial map adds in-object properties for "proxy" and "revoke".
// See https://tc39.github.io/ecma262/#sec-proxy.revocable
class JSProxyRevocableResult
    : public TorqueGeneratedJSProxyRevocableResult<JSProxyRevocableResult,
                                                   JSObject> {
 public:
  // Indices of in-object properties.
  static const int kProxyIndex = 0;
  static const int kRevokeIndex = 1;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSProxyRevocableResult);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_PROXY_H_
