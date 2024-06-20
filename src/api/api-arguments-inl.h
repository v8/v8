// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_API_API_ARGUMENTS_INL_H_
#define V8_API_API_ARGUMENTS_INL_H_

#include "src/api/api-arguments.h"
#include "src/api/api-inl.h"
#include "src/debug/debug.h"
#include "src/execution/vm-state-inl.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/instance-type.h"
#include "src/objects/slots-inl.h"

namespace v8 {
namespace internal {

CustomArgumentsBase::CustomArgumentsBase(Isolate* isolate)
    : Relocatable(isolate) {}

template <typename T>
CustomArguments<T>::~CustomArguments() {
  slot_at(kReturnValueIndex).store(Tagged<Object>(kHandleZapValue));
}

template <typename T>
template <typename V>
Handle<V> CustomArguments<T>::GetReturnValue(Isolate* isolate) const {
  // Check the ReturnValue.
  FullObjectSlot slot = slot_at(kReturnValueIndex);
  // Nothing was set, return empty handle as per previous behaviour.
  Tagged<Object> raw_object = *slot;
  if (IsTheHole(raw_object, isolate)) return Handle<V>();
  DCHECK(Is<JSAny>(raw_object));
  return Cast<V>(Handle<Object>(slot.location()));
}

template <typename T>
template <typename V>
Handle<V> CustomArguments<T>::GetReturnValueNoHoleCheck(
    Isolate* isolate) const {
  // Check the ReturnValue.
  FullObjectSlot slot = slot_at(kReturnValueIndex);
  // TODO(ishell): remove the hole check once it's no longer possible to set
  // return value to the hole.
  CHECK(!IsTheHole(*slot, isolate));
  DCHECK(Is<JSAny>(*slot));
  return Cast<V>(Handle<Object>(slot.location()));
}

inline Tagged<JSObject> PropertyCallbackArguments::holder() const {
  return Cast<JSObject>(*slot_at(T::kHolderIndex));
}

inline Tagged<Object> PropertyCallbackArguments::receiver() const {
  return *slot_at(T::kThisIndex);
}

inline Tagged<JSReceiver> FunctionCallbackArguments::holder() const {
  return Cast<JSReceiver>(*slot_at(T::kHolderIndex));
}

#define DCHECK_NAME_COMPATIBLE(interceptor, name) \
  DCHECK(interceptor->is_named());                \
  DCHECK(!name->IsPrivate());                     \
  DCHECK_IMPLIES(IsSymbol(*name), interceptor->can_intercept_symbols());

#define PREPARE_CALLBACK_INFO_ACCESSOR(ISOLATE, F, API_RETURN_TYPE,            \
                                       ACCESSOR_INFO, RECEIVER, ACCESSOR_KIND) \
  if (ISOLATE->should_check_side_effects() &&                                  \
      !ISOLATE->debug()->PerformSideEffectCheckForAccessor(                    \
          ACCESSOR_INFO, RECEIVER, ACCESSOR_KIND)) {                           \
    return {};                                                                 \
  }                                                                            \
  ExternalCallbackScope call_scope(ISOLATE, FUNCTION_ADDR(F));                 \
  /* TODO(ishell): cleanup this hack by embedding the PropertyCallbackInfo */  \
  /* into PropertyCallbackArguments object. */                                 \
  PropertyCallbackInfo<API_RETURN_TYPE>& callback_info = *(                    \
      reinterpret_cast<PropertyCallbackInfo<API_RETURN_TYPE>*>(&values_[0]));

#define PREPARE_CALLBACK_INFO_INTERCEPTOR(ISOLATE, F, API_RETURN_TYPE,        \
                                          INTERCEPTOR_INFO)                   \
  if (ISOLATE->should_check_side_effects() &&                                 \
      !ISOLATE->debug()->PerformSideEffectCheckForInterceptor(                \
          INTERCEPTOR_INFO)) {                                                \
    return {};                                                                \
  }                                                                           \
  ExternalCallbackScope call_scope(ISOLATE, FUNCTION_ADDR(F));                \
  /* TODO(ishell): cleanup this hack by embedding the PropertyCallbackInfo */ \
  /* into PropertyCallbackArguments object. */                                \
  PropertyCallbackInfo<API_RETURN_TYPE>& callback_info = *(                   \
      reinterpret_cast<PropertyCallbackInfo<API_RETURN_TYPE>*>(&values_[0]));

Handle<Object> FunctionCallbackArguments::Call(
    Tagged<FunctionTemplateInfo> function) {
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kFunctionCallback);
  v8::FunctionCallback f =
      reinterpret_cast<v8::FunctionCallback>(function->callback(isolate));
  if (isolate->should_check_side_effects() &&
      !isolate->debug()->PerformSideEffectCheckForCallback(
          handle(function, isolate))) {
    return {};
  }
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  FunctionCallbackInfo<v8::Value> info(values_, argv_, argc_);
  f(info);
  return GetReturnValue<Object>(isolate);
}

PropertyCallbackArguments::~PropertyCallbackArguments(){
#ifdef DEBUG
// TODO(chromium:1310062): enable this check.
// if (javascript_execution_counter_) {
//   CHECK_WITH_MSG(javascript_execution_counter_ ==
//                      isolate()->javascript_execution_counter(),
//                  "Unexpected side effect detected");
// }
#endif  // DEBUG
}

// -------------------------------------------------------------------------
// Named Interceptor callbacks.

Handle<JSObjectOrUndefined> PropertyCallbackArguments::CallNamedEnumerator(
    Handle<InterceptorInfo> interceptor) {
  DCHECK(interceptor->is_named());
  RCS_SCOPE(isolate(), RuntimeCallCounterId::kNamedEnumeratorCallback);
  return CallPropertyEnumerator(interceptor);
}

// TODO(ishell): return std::optional<PropertyAttributes>.
Handle<Object> PropertyCallbackArguments::CallNamedQuery(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedQueryCallback);
  // TODO(ishell, 328104148): avoid double initalization of this slot.
  slot_at(T::kPropertyKeyIndex).store(*name);
  NamedPropertyQueryCallback f =
      ToCData<NamedPropertyQueryCallback>(interceptor->query());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Integer, interceptor);
  // Constructor sets the return value to undefined, while this callback
  // must return v8::Integer, set default value to v8::None.
  callback_info.GetReturnValue().Set(static_cast<uint16_t>(v8::None));
  v8::Intercepted intercepted = f(v8::Utils::ToLocal(name), callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValueNoHoleCheck<Object>(isolate);
}

Handle<JSAny> PropertyCallbackArguments::CallNamedGetter(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedGetterCallback);
  // TODO(ishell, 328104148): avoid double initalization of this slot.
  slot_at(T::kPropertyKeyIndex).store(*name);
  NamedPropertyGetterCallback f =
      ToCData<NamedPropertyGetterCallback>(interceptor->getter());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor);
  v8::Intercepted intercepted = f(v8::Utils::ToLocal(name), callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValueNoHoleCheck<JSAny>(isolate);
}

Handle<JSAny> PropertyCallbackArguments::CallNamedDescriptor(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedDescriptorCallback);
  // TODO(ishell, 328104148): avoid double initalization of this slot.
  slot_at(T::kPropertyKeyIndex).store(*name);
  NamedPropertyDescriptorCallback f =
      ToCData<NamedPropertyDescriptorCallback>(interceptor->descriptor());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor);
  v8::Intercepted intercepted = f(v8::Utils::ToLocal(name), callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValueNoHoleCheck<JSAny>(isolate);
}

// TODO(ishell): just return v8::Intercepted.
Handle<Object> PropertyCallbackArguments::CallNamedSetter(
    DirectHandle<InterceptorInfo> interceptor, Handle<Name> name,
    Handle<Object> value) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedSetterCallback);
  // TODO(ishell, 328104148): avoid double initalization of this slot.
  slot_at(T::kPropertyKeyIndex).store(*name);
  NamedPropertySetterCallback f =
      ToCData<NamedPropertySetterCallback>(interceptor->setter());
  Handle<InterceptorInfo> has_side_effects;
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, void, has_side_effects);
  v8::Intercepted intercepted =
      f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  // Non-empty handle indicates that the request was intercepted.
  return isolate->factory()->undefined_value();
}

// TODO(ishell): just return v8::Intercepted.
Handle<Object> PropertyCallbackArguments::CallNamedDefiner(
    DirectHandle<InterceptorInfo> interceptor, Handle<Name> name,
    const v8::PropertyDescriptor& desc) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedDefinerCallback);
  // TODO(ishell, 328104148): avoid double initalization of this slot.
  slot_at(T::kPropertyKeyIndex).store(*name);
  NamedPropertyDefinerCallback f =
      ToCData<NamedPropertyDefinerCallback>(interceptor->definer());
  Handle<InterceptorInfo> has_side_effects;
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, void, has_side_effects);
  v8::Intercepted intercepted =
      f(v8::Utils::ToLocal(name), desc, callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  // Non-empty handle indicates that the request was intercepted.
  return isolate->factory()->undefined_value();
}

// TODO(ishell): return Handle<Boolean>
Handle<Object> PropertyCallbackArguments::CallNamedDeleter(
    DirectHandle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedDeleterCallback);
  // TODO(ishell, 328104148): avoid double initalization of this slot.
  slot_at(T::kPropertyKeyIndex).store(*name);
  // The constructor sets the return value to undefined, while this callback
  // must return v8::Boolean.
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).false_value());
  NamedPropertyDeleterCallback f =
      ToCData<NamedPropertyDeleterCallback>(interceptor->deleter());
  Handle<InterceptorInfo> has_side_effects;
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Boolean, has_side_effects);
  v8::Intercepted intercepted = f(v8::Utils::ToLocal(name), callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValue<Object>(isolate);
}

// -------------------------------------------------------------------------
// Indexed Interceptor callbacks.

Handle<JSObjectOrUndefined> PropertyCallbackArguments::CallIndexedEnumerator(
    Handle<InterceptorInfo> interceptor) {
  DCHECK(!interceptor->is_named());
  RCS_SCOPE(isolate(), RuntimeCallCounterId::kIndexedEnumeratorCallback);
  return CallPropertyEnumerator(interceptor);
}

// TODO(ishell): return std::optional<PropertyAttributes>.
Handle<Object> PropertyCallbackArguments::CallIndexedQuery(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedQueryCallback);
  IndexedPropertyQueryCallbackV2 f =
      ToCData<IndexedPropertyQueryCallbackV2>(interceptor->query());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Integer, interceptor);
  // Constructor sets the return value to undefined, while this callback
  // must return v8::Integer, set default value to v8::None.
  callback_info.GetReturnValue().Set(static_cast<uint16_t>(v8::None));
  v8::Intercepted intercepted = f(index, callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValueNoHoleCheck<Object>(isolate);
}

Handle<JSAny> PropertyCallbackArguments::CallIndexedGetter(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedGetterCallback);
  IndexedPropertyGetterCallbackV2 f =
      ToCData<IndexedPropertyGetterCallbackV2>(interceptor->getter());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor);
  v8::Intercepted intercepted = f(index, callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValueNoHoleCheck<JSAny>(isolate);
}

Handle<JSAny> PropertyCallbackArguments::CallIndexedDescriptor(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedDescriptorCallback);
  IndexedPropertyDescriptorCallbackV2 f =
      ToCData<IndexedPropertyDescriptorCallbackV2>(interceptor->descriptor());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor);
  v8::Intercepted intercepted = f(index, callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValueNoHoleCheck<JSAny>(isolate);
}

// TODO(ishell): just return v8::Intercepted.
Handle<Object> PropertyCallbackArguments::CallIndexedSetter(
    DirectHandle<InterceptorInfo> interceptor, uint32_t index,
    Handle<Object> value) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedSetterCallback);
  IndexedPropertySetterCallbackV2 f =
      ToCData<IndexedPropertySetterCallbackV2>(interceptor->setter());
  Handle<InterceptorInfo> has_side_effects;
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, void, has_side_effects);
  v8::Intercepted intercepted =
      f(index, v8::Utils::ToLocal(value), callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  // Non-empty handle indicates that the request was intercepted.
  return isolate->factory()->undefined_value();
}

// TODO(ishell): just return v8::Intercepted.
Handle<Object> PropertyCallbackArguments::CallIndexedDefiner(
    DirectHandle<InterceptorInfo> interceptor, uint32_t index,
    const v8::PropertyDescriptor& desc) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedDefinerCallback);
  IndexedPropertyDefinerCallbackV2 f =
      ToCData<IndexedPropertyDefinerCallbackV2>(interceptor->definer());
  Handle<InterceptorInfo> has_side_effects;
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, void, has_side_effects);
  v8::Intercepted intercepted = f(index, desc, callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  // Non-empty handle indicates that the request was intercepted.
  return isolate->factory()->undefined_value();
}

// TODO(ishell): return Handle<Boolean>
Handle<Object> PropertyCallbackArguments::CallIndexedDeleter(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedDeleterCallback);
  // The constructor sets the return value to undefined, while this callback
  // must return v8::Boolean.
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).false_value());
  IndexedPropertyDeleterCallbackV2 f =
      ToCData<IndexedPropertyDeleterCallbackV2>(interceptor->deleter());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Boolean, interceptor);
  v8::Intercepted intercepted = f(index, callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValueNoHoleCheck<Object>(isolate);
}

Handle<JSObjectOrUndefined> PropertyCallbackArguments::CallPropertyEnumerator(
    Handle<InterceptorInfo> interceptor) {
  // Named and indexed enumerator callbacks have same signatures.
  static_assert(std::is_same<NamedPropertyEnumeratorCallback,
                             IndexedPropertyEnumeratorCallback>::value);
  // Enumerator callback's return value is initialized with undefined even
  // though it's supposed to return v8::Array.
  // TODO(ishell): consider making it return v8::Intercepted to indicate
  // whether the result was set or not.
  IndexedPropertyEnumeratorCallback f =
      v8::ToCData<IndexedPropertyEnumeratorCallback>(interceptor->enumerator());
  Isolate* isolate = this->isolate();
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Array, interceptor);
  f(callback_info);
  Handle<JSAny> result = GetReturnValue<JSAny>(isolate);
  if (result.is_null()) return isolate->factory()->undefined_value();
  DCHECK(IsUndefined(*result) || IsJSObject(*result));
  return Cast<JSObjectOrUndefined>(result);
}

// -------------------------------------------------------------------------
// Accessors

Handle<JSAny> PropertyCallbackArguments::CallAccessorGetter(
    DirectHandle<AccessorInfo> info, Handle<Name> name) {
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kAccessorGetterCallback);
  // Unlike interceptor callbacks we know that the property exists, so
  // the callback is allowed to have side effects.
  AcceptSideEffects();

  // TODO(ishell, 328104148): avoid double initalization of this slot.
  slot_at(T::kPropertyKeyIndex).store(*name);
  AccessorNameGetterCallback f =
      reinterpret_cast<AccessorNameGetterCallback>(info->getter(isolate));
  PREPARE_CALLBACK_INFO_ACCESSOR(isolate, f, v8::Value, info,
                                 handle(receiver(), isolate), ACCESSOR_GETTER);
  f(v8::Utils::ToLocal(name), callback_info);
  return GetReturnValue<JSAny>(isolate);
}

bool PropertyCallbackArguments::CallAccessorSetter(
    DirectHandle<AccessorInfo> accessor_info, Handle<Name> name,
    Handle<Object> value) {
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kAccessorSetterCallback);
  // Unlike interceptor callbacks we know that the property exists, so
  // the callback is allowed to have side effects.
  AcceptSideEffects();

  // TODO(ishell, 328104148): avoid double initalization of this slot.
  slot_at(T::kPropertyKeyIndex).store(*name);
  // The actual type of setter callback is either
  // v8::AccessorNameSetterCallback or
  // i::Accesors::AccessorNameBooleanSetterCallback, depending on whether the
  // AccessorInfo was created by the API or internally (see accessors.cc).
  // Here we handle both cases using the AccessorNameSetterCallback signature
  // and checking whether the returned result is set to default value
  // (the undefined value).
  // TODO(ishell): update V8 Api to allow setter callbacks provide the result
  // of [[Set]] operation according to JavaScript semantics.
  AccessorNameSetterCallback f = reinterpret_cast<AccessorNameSetterCallback>(
      accessor_info->setter(isolate));
  PREPARE_CALLBACK_INFO_ACCESSOR(isolate, f, void, accessor_info,
                                 handle(receiver(), isolate), ACCESSOR_SETTER);
  // Constructor sets the return value to undefined, while for API callbacks
  // we still need to detect the "result was never set" or the "result
  // value was set to empty handle" cases to treat them as a successful
  // completion. So, keep on initializing the default value with "the_hole".
  // TODO(ishell, 328490288): avoid the need to deal with empty handles by
  // using "true_value" as the defaut value for PropertyCallbackInfo<void>'s
  // result slot.
  slot_at(T::kReturnValueIndex).store(ReadOnlyRoots(isolate).the_hole_value());
  f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), callback_info);
  Handle<JSAny> result = GetReturnValue<JSAny>(isolate);
  // In case of v8::AccessorNameSetterCallback, we know that the result
  // value cannot be set, so the result slot will always contain the
  // default value (the undefined_value) indicating successful completion.
  // In case of AccessorNameBooleanSetterCallback, the result will either be
  // set to v8::Boolean or an exception will be thrown (in which case the
  // result is ignored anyway).
  // We've been treating the "result was never set" or the "result value was
  // set to empty handle" case as a successful completion for API callbacks.
  // TODO(ishell, 328490288): avoid the need to deal with empty handles and
  // handle random result values here.
  return result.is_null() || Object::BooleanValue(*result, isolate);
}

#undef PREPARE_CALLBACK_INFO_ACCESSOR
#undef PREPARE_CALLBACK_INFO_INTERCEPTOR

}  // namespace internal
}  // namespace v8

#endif  // V8_API_API_ARGUMENTS_INL_H_
