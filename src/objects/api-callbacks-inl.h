// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_API_CALLBACKS_INL_H_
#define V8_OBJECTS_API_CALLBACKS_INL_H_

#include "src/objects/api-callbacks.h"
// Include the non-inl header before the rest of the headers.

#include "src/heap/heap-write-barrier-inl.h"
#include "src/heap/heap-write-barrier.h"
#include "src/objects/foreign-inl.h"
#include "src/objects/js-objects-inl.h"
#include "src/objects/name.h"
#include "src/objects/oddball.h"
#include "src/objects/templates.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/api-callbacks-tq-inl.inc"

// Make sure that Api can read Data value from both AccessorInfo and
// InterceptorInfo without checking the type.
static_assert(Internals::kCallbackInfoDataOffset == AccessorInfo::kDataOffset);
static_assert(Internals::kCallbackInfoDataOffset ==
              InterceptorInfo::kDataOffset);

TQ_OBJECT_CONSTRUCTORS_IMPL(AccessCheckInfo)
TQ_OBJECT_CONSTRUCTORS_IMPL(AccessorInfo)
TQ_OBJECT_CONSTRUCTORS_IMPL(InterceptorInfo)

REDIRECTED_CALLBACK_ACCESSORS_MAYBE_READ_ONLY_HOST(
    AccessorInfo, getter, Address, kGetterOffset, kAccessorInfoGetterTag,
    ExternalReference::DIRECT_GETTER_CALL)

EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST(AccessorInfo, setter, Address,
                                                kSetterOffset,
                                                kAccessorInfoSetterTag)

bool AccessorInfo::has_getter(Isolate* isolate) {
  return getter(isolate) != kNullAddress;
}

bool AccessorInfo::has_setter(Isolate* isolate) {
  return setter(isolate) != kNullAddress;
}

BIT_FIELD_ACCESSORS(AccessorInfo, flags, replace_on_access,
                    AccessorInfo::ReplaceOnAccessBit)
BIT_FIELD_ACCESSORS(AccessorInfo, flags, is_sloppy, AccessorInfo::IsSloppyBit)
BIT_FIELD_ACCESSORS(AccessorInfo, flags, getter_side_effect_type,
                    AccessorInfo::GetterSideEffectTypeBits)

SideEffectType AccessorInfo::setter_side_effect_type() const {
  return SetterSideEffectTypeBits::decode(flags());
}

void AccessorInfo::set_setter_side_effect_type(SideEffectType value) {
  // We do not support describing setters as having no side effect, since
  // calling set accessors must go through a store bytecode. Store bytecodes
  // support checking receivers for temporary objects, but still expect
  // the receiver to be written to.
  CHECK_NE(value, SideEffectType::kHasNoSideEffect);
  set_flags(SetterSideEffectTypeBits::update(flags(), value));
}

BIT_FIELD_ACCESSORS(AccessorInfo, flags, initial_property_attributes,
                    AccessorInfo::InitialAttributesBits)

void AccessorInfo::RemoveCallbackRedirectionForSerialization(
    IsolateForSandbox isolate) {
  CHECK(USE_SIMULATOR_BOOL);
  remove_getter_redirection(isolate);
}
void AccessorInfo::RestoreCallbackRedirectionAfterDeserialization(
    IsolateForSandbox isolate) {
  CHECK(USE_SIMULATOR_BOOL);
  init_getter_redirection(isolate);
}

void AccessorInfo::clear_padding() {
  if (FIELD_SIZE(kOptionalPaddingOffset) == 0) return;
  memset(reinterpret_cast<void*>(address() + kOptionalPaddingOffset), 0,
         FIELD_SIZE(kOptionalPaddingOffset));
}

// For the purpose of checking whether the respective callback field is
// initialized we can use any of the named/indexed versions.
#define INTERCEPTOR_INFO_HAS_GETTER(name) \
  bool InterceptorInfo::has_##name() const { return has_named_##name(); }

INTERCEPTOR_INFO_HAS_GETTER(getter)
INTERCEPTOR_INFO_HAS_GETTER(setter)
INTERCEPTOR_INFO_HAS_GETTER(query)
INTERCEPTOR_INFO_HAS_GETTER(descriptor)
INTERCEPTOR_INFO_HAS_GETTER(deleter)
INTERCEPTOR_INFO_HAS_GETTER(definer)
INTERCEPTOR_INFO_HAS_GETTER(enumerator)

#undef INTERCEPTOR_INFO_HAS_GETTER

LAZY_REDIRECTED_CALLBACK_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, named_getter, Address, kGetterOffset,
    kApiNamedPropertyGetterCallbackTag, ExternalReference::DIRECT_GETTER_CALL,
    is_named(), is_named() && (value != kNullAddress))
LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, named_setter, Address, kSetterOffset,
    kApiNamedPropertySetterCallbackTag, is_named(),
    is_named() && (value != kNullAddress))
LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, named_query, Address, kQueryOffset,
    kApiNamedPropertyQueryCallbackTag, is_named(),
    is_named() && (value != kNullAddress))
LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, named_descriptor, Address, kDescriptorOffset,
    kApiNamedPropertyDescriptorCallbackTag, is_named(),
    is_named() && (value != kNullAddress))
LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, named_deleter, Address, kDeleterOffset,
    kApiNamedPropertyDeleterCallbackTag, is_named(),
    is_named() && (value != kNullAddress))
LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, named_enumerator, Address, kEnumeratorOffset,
    kApiNamedPropertyEnumeratorCallbackTag, is_named(),
    is_named() && (value != kNullAddress))
LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, named_definer, Address, kDefinerOffset,
    kApiNamedPropertyDefinerCallbackTag, is_named(),
    is_named() && (value != kNullAddress))

LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, indexed_getter, Address, kGetterOffset,
    kApiIndexedPropertyGetterCallbackTag, !is_named(),
    !is_named() && (value != kNullAddress))
LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, indexed_setter, Address, kSetterOffset,
    kApiIndexedPropertySetterCallbackTag, !is_named(),
    !is_named() && (value != kNullAddress))
LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, indexed_query, Address, kQueryOffset,
    kApiIndexedPropertyQueryCallbackTag, !is_named(),
    !is_named() && (value != kNullAddress))
LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, indexed_descriptor, Address, kDescriptorOffset,
    kApiIndexedPropertyDescriptorCallbackTag, !is_named(),
    !is_named() && (value != kNullAddress))
LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, indexed_deleter, Address, kDeleterOffset,
    kApiIndexedPropertyDeleterCallbackTag, !is_named(),
    !is_named() && (value != kNullAddress))
LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, indexed_enumerator, Address, kEnumeratorOffset,
    kApiIndexedPropertyEnumeratorCallbackTag, !is_named(),
    !is_named() && (value != kNullAddress))
LAZY_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST_CHECKED2(
    InterceptorInfo, indexed_definer, Address, kDefinerOffset,
    kApiIndexedPropertyDefinerCallbackTag, !is_named(),
    !is_named() && (value != kNullAddress))

BOOL_ACCESSORS(InterceptorInfo, flags, can_intercept_symbols,
               CanInterceptSymbolsBit::kShift)
BOOL_ACCESSORS(InterceptorInfo, flags, non_masking, NonMaskingBit::kShift)
BOOL_ACCESSORS(InterceptorInfo, flags, is_named, NamedBit::kShift)
BOOL_ACCESSORS(InterceptorInfo, flags, has_no_side_effect,
               HasNoSideEffectBit::kShift)
// TODO(ishell): remove once all the Api changes are done.
BOOL_ACCESSORS(InterceptorInfo, flags, has_new_callbacks_signature,
               HasNewCallbacksSignatureBit::kShift)

void InterceptorInfo::RemoveCallbackRedirectionForSerialization(
    IsolateForSandbox isolate) {
  CHECK(USE_SIMULATOR_BOOL);
  if (is_named()) {
    remove_named_getter_redirection(isolate);
  }
}
void InterceptorInfo::RestoreCallbackRedirectionAfterDeserialization(
    IsolateForSandbox isolate) {
  CHECK(USE_SIMULATOR_BOOL);
  if (is_named()) {
    init_named_getter_redirection(isolate);
  }
}

void InterceptorInfo::clear_padding() {
  if (FIELD_SIZE(kOptionalPaddingOffset) == 0) return;
  memset(reinterpret_cast<void*>(address() + kOptionalPaddingOffset), 0,
         FIELD_SIZE(kOptionalPaddingOffset));
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_API_CALLBACKS_INL_H_
