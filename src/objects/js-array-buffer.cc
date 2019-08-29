// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-array-buffer.h"
#include "src/objects/js-array-buffer-inl.h"

#include "src/logging/counters.h"
#include "src/objects/property-descriptor.h"

namespace v8 {
namespace internal {

namespace {

bool CanonicalNumericIndexString(Isolate* isolate, Handle<Object> s,
                                 Handle<Object>* index) {
  DCHECK(s->IsString() || s->IsSmi());

  Handle<Object> result;
  if (s->IsSmi()) {
    result = s;
  } else {
    result = String::ToNumber(isolate, Handle<String>::cast(s));
    if (!result->IsMinusZero()) {
      Handle<String> str = Object::ToString(isolate, result).ToHandleChecked();
      // Avoid treating strings like "2E1" and "20" as the same key.
      if (!str->SameValue(*s)) return false;
    }
  }
  *index = result;
  return true;
}
}  // anonymous namespace

void JSArrayBuffer::SetupEmpty(SharedFlag shared) {
  clear_padding();
  set_bit_field(0);
  set_is_shared(shared == SharedFlag::kShared);
  set_is_detachable(shared != SharedFlag::kShared);
  set_backing_store(nullptr);
  set_byte_length(0);
}

std::shared_ptr<BackingStore> JSArrayBuffer::Detach(
    bool force_for_wasm_memory) {
  if (was_detached()) return nullptr;

  if (force_for_wasm_memory) {
    // Skip the is_detachable() check.
  } else if (!is_detachable()) {
    // Not detachable, do nothing.
    return nullptr;
  }

  Isolate* const isolate = GetIsolate();
  auto backing_store = isolate->heap()->UnregisterBackingStore(*this);
  CHECK_IMPLIES(force_for_wasm_memory && backing_store,
                backing_store->is_wasm_memory());

  if (isolate->IsArrayBufferDetachingIntact()) {
    isolate->InvalidateArrayBufferDetachingProtector();
  }

  DCHECK(!is_shared());
  DCHECK(!is_asmjs_memory());
  set_backing_store(nullptr);
  set_byte_length(0);
  set_was_detached(true);

  return backing_store;
}

void JSArrayBuffer::Attach(std::shared_ptr<BackingStore> backing_store) {
  SetupEmpty(backing_store->is_shared() ? SharedFlag::kShared
                                        : SharedFlag::kNotShared);

  if (backing_store->is_wasm_memory()) set_is_detachable(false);

  set_backing_store(backing_store->buffer_start());
  set_byte_length(backing_store->byte_length());
  if (!backing_store->free_on_destruct()) set_is_external(true);

  GetIsolate()->heap()->RegisterBackingStore(*this, std::move(backing_store));
}

std::shared_ptr<BackingStore> JSArrayBuffer::GetBackingStore() {
  return GetIsolate()->heap()->LookupBackingStore(*this);
}

Handle<JSArrayBuffer> JSTypedArray::GetBuffer() {
  Isolate* isolate = GetIsolate();
  Handle<JSTypedArray> self(*this, isolate);
  DCHECK(IsTypedArrayElementsKind(self->GetElementsKind()));

  Handle<JSArrayBuffer> array_buffer(JSArrayBuffer::cast(self->buffer()),
                                     isolate);
  if (!is_on_heap()) {
    // Already is off heap, so return the existing buffer.
    return array_buffer;
  }

  // The existing array buffer should be empty.
  DCHECK_NULL(array_buffer->backing_store());

  // Allocate a new backing store and attach it to the existing array buffer.
  size_t byte_length = self->byte_length();
  auto backing_store =
      BackingStore::Allocate(isolate, byte_length, SharedFlag::kNotShared,
                             InitializedFlag::kUninitialized);

  if (!backing_store) {
    isolate->heap()->FatalProcessOutOfMemory("JSTypedArray::GetBuffer");
  }

  // Copy the elements into the backing store of the array buffer.
  if (byte_length > 0) {
    memcpy(backing_store->buffer_start(), self->DataPtr(), byte_length);
  }

  // Attach the backing store to the array buffer.
  array_buffer->Attach(std::move(backing_store));

  // Clear the elements of the typed array.
  self->set_elements(ReadOnlyRoots(isolate).empty_byte_array());
  self->set_external_pointer(array_buffer->backing_store());
  self->set_base_pointer(Smi::kZero);
  DCHECK(!self->is_on_heap());

  return array_buffer;
}

// ES#sec-integer-indexed-exotic-objects-defineownproperty-p-desc
// static
Maybe<bool> JSTypedArray::DefineOwnProperty(Isolate* isolate,
                                            Handle<JSTypedArray> o,
                                            Handle<Object> key,
                                            PropertyDescriptor* desc,
                                            Maybe<ShouldThrow> should_throw) {
  // 1. Assert: IsPropertyKey(P) is true.
  DCHECK(key->IsName() || key->IsNumber());
  // 2. Assert: O is an Object that has a [[ViewedArrayBuffer]] internal slot.
  // 3. If Type(P) is String, then
  if (key->IsString() || key->IsSmi()) {
    // 3a. Let numericIndex be ! CanonicalNumericIndexString(P)
    // 3b. If numericIndex is not undefined, then
    Handle<Object> numeric_index;
    if (CanonicalNumericIndexString(isolate, key, &numeric_index)) {
      // 3b i. If IsInteger(numericIndex) is false, return false.
      // 3b ii. If numericIndex = -0, return false.
      // 3b iii. If numericIndex < 0, return false.
      // FIXME: the standard allows up to 2^53 elements.
      uint32_t index;
      if (numeric_index->IsMinusZero() || !numeric_index->ToUint32(&index)) {
        RETURN_FAILURE(isolate, GetShouldThrow(isolate, should_throw),
                       NewTypeError(MessageTemplate::kInvalidTypedArrayIndex));
      }
      // 3b iv. Let length be O.[[ArrayLength]].
      size_t length = o->length();
      // 3b v. If numericIndex ≥ length, return false.
      if (o->WasDetached() || index >= length) {
        RETURN_FAILURE(isolate, GetShouldThrow(isolate, should_throw),
                       NewTypeError(MessageTemplate::kInvalidTypedArrayIndex));
      }
      // 3b vi. If IsAccessorDescriptor(Desc) is true, return false.
      if (PropertyDescriptor::IsAccessorDescriptor(desc)) {
        RETURN_FAILURE(isolate, GetShouldThrow(isolate, should_throw),
                       NewTypeError(MessageTemplate::kRedefineDisallowed, key));
      }
      // 3b vii. If Desc has a [[Configurable]] field and if
      //         Desc.[[Configurable]] is true, return false.
      // 3b viii. If Desc has an [[Enumerable]] field and if Desc.[[Enumerable]]
      //          is false, return false.
      // 3b ix. If Desc has a [[Writable]] field and if Desc.[[Writable]] is
      //        false, return false.
      if ((desc->has_configurable() && desc->configurable()) ||
          (desc->has_enumerable() && !desc->enumerable()) ||
          (desc->has_writable() && !desc->writable())) {
        RETURN_FAILURE(isolate, GetShouldThrow(isolate, should_throw),
                       NewTypeError(MessageTemplate::kRedefineDisallowed, key));
      }
      // 3b x. If Desc has a [[Value]] field, then
      //   3b x 1. Let value be Desc.[[Value]].
      //   3b x 2. Return ? IntegerIndexedElementSet(O, numericIndex, value).
      if (desc->has_value()) {
        if (!desc->has_configurable()) desc->set_configurable(false);
        if (!desc->has_enumerable()) desc->set_enumerable(true);
        if (!desc->has_writable()) desc->set_writable(true);
        Handle<Object> value = desc->value();
        RETURN_ON_EXCEPTION_VALUE(isolate,
                                  SetOwnElementIgnoreAttributes(
                                      o, index, value, desc->ToAttributes()),
                                  Nothing<bool>());
      }
      // 3b xi. Return true.
      return Just(true);
    }
  }
  // 4. Return ! OrdinaryDefineOwnProperty(O, P, Desc).
  return OrdinaryDefineOwnProperty(isolate, o, key, desc, should_throw);
}

ExternalArrayType JSTypedArray::type() {
  switch (map().elements_kind()) {
#define ELEMENTS_KIND_TO_ARRAY_TYPE(Type, type, TYPE, ctype) \
  case TYPE##_ELEMENTS:                                      \
    return kExternal##Type##Array;

    TYPED_ARRAYS(ELEMENTS_KIND_TO_ARRAY_TYPE)
#undef ELEMENTS_KIND_TO_ARRAY_TYPE

    default:
      UNREACHABLE();
  }
}

size_t JSTypedArray::element_size() {
  switch (map().elements_kind()) {
#define ELEMENTS_KIND_TO_ELEMENT_SIZE(Type, type, TYPE, ctype) \
  case TYPE##_ELEMENTS:                                        \
    return sizeof(ctype);

    TYPED_ARRAYS(ELEMENTS_KIND_TO_ELEMENT_SIZE)
#undef ELEMENTS_KIND_TO_ELEMENT_SIZE

    default:
      UNREACHABLE();
  }
}

}  // namespace internal
}  // namespace v8
