// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_API_ARGUMENTS_INL_H_
#define V8_API_ARGUMENTS_INL_H_

#include "src/api-arguments.h"

#include "src/tracing/trace-event.h"
#include "src/vm-state-inl.h"

namespace v8 {
namespace internal {

#define FOR_EACH_CALLBACK(F)           \
  F(Query, query, Object, v8::Integer) \
  F(Deleter, deleter, Object, v8::Boolean)

#define DCHECK_NAME_COMPATIBLE(interceptor, name) \
  DCHECK(interceptor->is_named());                \
  DCHECK(!name->IsPrivate());                     \
  DCHECK_IMPLIES(name->IsSymbol(), interceptor->can_intercept_symbols());

#define PREPARE_CALLBACK_INFO(ISOLATE, F, RETURN_VALUE, API_RETURN_TYPE, \
                              CALLBACK_INFO)                             \
  if (ISOLATE->needs_side_effect_check() &&                              \
      !PerformSideEffectCheck(ISOLATE, CALLBACK_INFO)) {                 \
    return RETURN_VALUE();                                               \
  }                                                                      \
  VMState<EXTERNAL> state(ISOLATE);                                      \
  ExternalCallbackScope call_scope(ISOLATE, FUNCTION_ADDR(F));           \
  PropertyCallbackInfo<API_RETURN_TYPE> callback_info(begin());

#define CREATE_NAMED_CALLBACK(Function, type, ReturnType, ApiReturnType)      \
  Handle<ReturnType> PropertyCallbackArguments::CallNamed##Function(          \
      Handle<InterceptorInfo> interceptor, Handle<Name> name) {               \
    DCHECK_NAME_COMPATIBLE(interceptor, name);                                \
    Isolate* isolate = this->isolate();                                       \
    RuntimeCallTimerScope timer(                                              \
        isolate, RuntimeCallCounterId::kNamed##Function##Callback);           \
    GenericNamedProperty##Function##Callback f =                              \
        ToCData<GenericNamedProperty##Function##Callback>(                    \
            interceptor->type());                                             \
    Handle<Object> side_effect_check_not_supported;                           \
    PREPARE_CALLBACK_INFO(isolate, f, Handle<ReturnType>, ApiReturnType,      \
                          side_effect_check_not_supported);                   \
    LOG(isolate,                                                              \
        ApiNamedPropertyAccess("interceptor-named-" #type, holder(), *name)); \
    f(v8::Utils::ToLocal(name), callback_info);                               \
    return GetReturnValue<ReturnType>(isolate);                               \
  }

FOR_EACH_CALLBACK(CREATE_NAMED_CALLBACK)
#undef CREATE_NAMED_CALLBACK

#define CREATE_INDEXED_CALLBACK(Function, type, ReturnType, ApiReturnType) \
  Handle<ReturnType> PropertyCallbackArguments::CallIndexed##Function(     \
      Handle<InterceptorInfo> interceptor, uint32_t index) {               \
    DCHECK(!interceptor->is_named());                                      \
    Isolate* isolate = this->isolate();                                    \
    RuntimeCallTimerScope timer(                                           \
        isolate, RuntimeCallCounterId::kIndexed##Function##Callback);      \
    IndexedProperty##Function##Callback f =                                \
        ToCData<IndexedProperty##Function##Callback>(interceptor->type()); \
    Handle<Object> side_effect_check_not_supported;                        \
    PREPARE_CALLBACK_INFO(isolate, f, Handle<ReturnType>, ApiReturnType,   \
                          side_effect_check_not_supported);                \
    LOG(isolate, ApiIndexedPropertyAccess("interceptor-indexed-" #type,    \
                                          holder(), index));               \
    f(index, callback_info);                                               \
    return GetReturnValue<ReturnType>(isolate);                            \
  }

FOR_EACH_CALLBACK(CREATE_INDEXED_CALLBACK)

#undef FOR_EACH_CALLBACK
#undef CREATE_INDEXED_CALLBACK

Handle<Object> PropertyCallbackArguments::CallNamedGetter(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              RuntimeCallCounterId::kNamedGetterCallback);
  LOG(isolate,
      ApiNamedPropertyAccess("interceptor-named-getter", holder(), *name));
  GenericNamedPropertyGetterCallback f =
      ToCData<GenericNamedPropertyGetterCallback>(interceptor->getter());
  Handle<Object> side_effect_check_not_supported;
  return BasicCallNamedGetterCallback(f, name, side_effect_check_not_supported);
}

Handle<Object> PropertyCallbackArguments::CallNamedDescriptor(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              RuntimeCallCounterId::kNamedDescriptorCallback);
  LOG(isolate,
      ApiNamedPropertyAccess("interceptor-named-descriptor", holder(), *name));
  GenericNamedPropertyDescriptorCallback f =
      ToCData<GenericNamedPropertyDescriptorCallback>(
          interceptor->descriptor());
  Handle<Object> side_effect_check_not_supported;
  return BasicCallNamedGetterCallback(f, name, side_effect_check_not_supported);
}

Handle<Object> PropertyCallbackArguments::BasicCallNamedGetterCallback(
    GenericNamedPropertyGetterCallback f, Handle<Name> name,
    Handle<Object> info) {
  DCHECK(!name->IsPrivate());
  Isolate* isolate = this->isolate();
  PREPARE_CALLBACK_INFO(isolate, f, Handle<Object>, v8::Value, info);
  f(v8::Utils::ToLocal(name), callback_info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallNamedSetter(
    Handle<InterceptorInfo> interceptor, Handle<Name> name,
    Handle<Object> value) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  GenericNamedPropertySetterCallback f =
      ToCData<GenericNamedPropertySetterCallback>(interceptor->setter());
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              RuntimeCallCounterId::kNamedSetterCallback);
  DCHECK(!isolate->needs_side_effect_check());
  Handle<Object> side_effect_check_not_supported;
  PREPARE_CALLBACK_INFO(isolate, f, Handle<Object>, v8::Value,
                        side_effect_check_not_supported);
  LOG(isolate,
      ApiNamedPropertyAccess("interceptor-named-set", holder(), *name));
  f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), callback_info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallNamedDefiner(
    Handle<InterceptorInfo> interceptor, Handle<Name> name,
    const v8::PropertyDescriptor& desc) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              RuntimeCallCounterId::kNamedDefinerCallback);
  GenericNamedPropertyDefinerCallback f =
      ToCData<GenericNamedPropertyDefinerCallback>(interceptor->definer());
  // We should not have come this far when side effect checks are enabled.
  DCHECK(!isolate->needs_side_effect_check());
  Handle<Object> side_effect_check_not_supported;
  PREPARE_CALLBACK_INFO(isolate, f, Handle<Object>, v8::Value,
                        side_effect_check_not_supported);
  LOG(isolate,
      ApiNamedPropertyAccess("interceptor-named-define", holder(), *name));
  f(v8::Utils::ToLocal(name), desc, callback_info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallIndexedSetter(
    Handle<InterceptorInfo> interceptor, uint32_t index, Handle<Object> value) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              RuntimeCallCounterId::kIndexedSetterCallback);
  IndexedPropertySetterCallback f =
      ToCData<IndexedPropertySetterCallback>(interceptor->setter());
  // We should not have come this far when side effect checks are enabled.
  DCHECK(!isolate->needs_side_effect_check());
  Handle<Object> side_effect_check_not_supported;
  PREPARE_CALLBACK_INFO(isolate, f, Handle<Object>, v8::Value,
                        side_effect_check_not_supported);
  LOG(isolate,
      ApiIndexedPropertyAccess("interceptor-indexed-set", holder(), index));
  f(index, v8::Utils::ToLocal(value), callback_info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallIndexedDefiner(
    Handle<InterceptorInfo> interceptor, uint32_t index,
    const v8::PropertyDescriptor& desc) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              RuntimeCallCounterId::kIndexedDefinerCallback);
  IndexedPropertyDefinerCallback f =
      ToCData<IndexedPropertyDefinerCallback>(interceptor->definer());
  // We should not have come this far when side effect checks are enabled.
  DCHECK(!isolate->needs_side_effect_check());
  Handle<Object> side_effect_check_not_supported;
  PREPARE_CALLBACK_INFO(isolate, f, Handle<Object>, v8::Value,
                        side_effect_check_not_supported);
  LOG(isolate,
      ApiIndexedPropertyAccess("interceptor-indexed-define", holder(), index));
  f(index, desc, callback_info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallIndexedGetter(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              RuntimeCallCounterId::kNamedGetterCallback);
  LOG(isolate,
      ApiIndexedPropertyAccess("interceptor-indexed-getter", holder(), index));
  IndexedPropertyGetterCallback f =
      ToCData<IndexedPropertyGetterCallback>(interceptor->getter());
  return BasicCallIndexedGetterCallback(f, index);
}

Handle<Object> PropertyCallbackArguments::CallIndexedDescriptor(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              RuntimeCallCounterId::kIndexedDescriptorCallback);
  LOG(isolate, ApiIndexedPropertyAccess("interceptor-indexed-descriptor",
                                        holder(), index));
  IndexedPropertyDescriptorCallback f =
      ToCData<IndexedPropertyDescriptorCallback>(interceptor->descriptor());
  return BasicCallIndexedGetterCallback(f, index);
}

Handle<Object> PropertyCallbackArguments::BasicCallIndexedGetterCallback(
    IndexedPropertyGetterCallback f, uint32_t index) {
  Isolate* isolate = this->isolate();
  Handle<Object> side_effect_check_not_supported;
  PREPARE_CALLBACK_INFO(isolate, f, Handle<Object>, v8::Value,
                        side_effect_check_not_supported);
  f(index, callback_info);
  return GetReturnValue<Object>(isolate);
}

Handle<JSObject> PropertyCallbackArguments::CallPropertyEnumerator(
    Handle<InterceptorInfo> interceptor) {
  // For now there is a single enumerator for indexed and named properties.
  IndexedPropertyEnumeratorCallback f =
      v8::ToCData<IndexedPropertyEnumeratorCallback>(interceptor->enumerator());
  // TODO(cbruni): assert same type for indexed and named callback.
  Isolate* isolate = this->isolate();
  Handle<Object> side_effect_check_not_supported;
  PREPARE_CALLBACK_INFO(isolate, f, Handle<JSObject>, v8::Array,
                        side_effect_check_not_supported);
  f(callback_info);
  return GetReturnValue<JSObject>(isolate);
}

// -------------------------------------------------------------------------
// Accessors

Handle<Object> PropertyCallbackArguments::CallAccessorGetter(
    Handle<AccessorInfo> info, Handle<Name> name) {
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              RuntimeCallCounterId::kAccessorGetterCallback);
  LOG(isolate, ApiNamedPropertyAccess("accessor-getter", holder(), *name));
  AccessorNameGetterCallback f =
      ToCData<AccessorNameGetterCallback>(info->getter());
  return BasicCallNamedGetterCallback(f, name, info);
}

Handle<Object> PropertyCallbackArguments::CallAccessorSetter(
    Handle<AccessorInfo> accessor_info, Handle<Name> name,
    Handle<Object> value) {
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              RuntimeCallCounterId::kAccessorSetterCallback);
  AccessorNameSetterCallback f =
      ToCData<AccessorNameSetterCallback>(accessor_info->setter());
  // We should not have come this far when side effect checks are enabled.
  DCHECK(!isolate->needs_side_effect_check());
  Handle<Object> side_effect_check_not_supported;
  PREPARE_CALLBACK_INFO(isolate, f, Handle<Object>, void,
                        side_effect_check_not_supported);
  LOG(isolate, ApiNamedPropertyAccess("accessor-setter", holder(), *name));
  f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), callback_info);
  return GetReturnValue<Object>(isolate);
}

#undef PREPARE_CALLBACK_INFO

}  // namespace internal
}  // namespace v8

#endif  // V8_API_ARGUMENTS_INL_H_
