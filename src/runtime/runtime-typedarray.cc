// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/elements.h"
#include "src/factory.h"
#include "src/messages.h"
#include "src/objects-inl.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_ArrayBufferGetByteLength) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_CHECKED(JSArrayBuffer, holder, 0);
  return holder->byte_length();
}


RUNTIME_FUNCTION(Runtime_ArrayBufferNeuter) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSArrayBuffer, array_buffer, 0);
  if (array_buffer->backing_store() == NULL) {
    CHECK(Smi::kZero == array_buffer->byte_length());
    return isolate->heap()->undefined_value();
  }
  // Shared array buffers should never be neutered.
  CHECK(!array_buffer->is_shared());
  DCHECK(!array_buffer->is_external());
  void* backing_store = array_buffer->backing_store();
  size_t byte_length = NumberToSize(array_buffer->byte_length());
  array_buffer->set_is_external(true);
  isolate->heap()->UnregisterArrayBuffer(*array_buffer);
  array_buffer->Neuter();
  isolate->array_buffer_allocator()->Free(backing_store, byte_length);
  return isolate->heap()->undefined_value();
}

namespace {

Object* CopyElements(Isolate* isolate, Handle<JSTypedArray> holder,
                     Handle<JSReceiver> source, size_t length) {
  ElementsAccessor* holder_accessor = holder->GetElementsAccessor();
  for (uint32_t i = 0; i < length; i++) {
    LookupIterator get_it(isolate, source, i);
    Handle<Object> element;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, element,
                                       Object::GetProperty(&get_it));
    // Convert the incoming value to a number for storing into typed arrays.
    if (!element->IsNumber() && !element->IsUndefined(isolate)) {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, element,
                                         Object::ToNumber(element));
    }
    holder_accessor->Set(holder, i, *element);
  }

  return isolate->heap()->undefined_value();
}

}  // namespace

RUNTIME_FUNCTION(Runtime_TypedArrayCopyElements) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSTypedArray, holder, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSReceiver, source, 1);
  CONVERT_NUMBER_ARG_HANDLE_CHECKED(length_obj, 2);

  size_t length;
  CHECK(TryNumberToSize(*length_obj, &length));

  return CopyElements(isolate, holder, source, length);
}

// Initializes a typed array from an array-like object, and its backing store as
// well.
RUNTIME_FUNCTION(Runtime_TypedArrayInitializeFromArrayLike) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSTypedArray, holder, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSReceiver, source, 1);
  CONVERT_NUMBER_ARG_HANDLE_CHECKED(length_obj, 2);

  ElementsKind fixed_elements_kind = holder->map()->elements_kind();
  ExternalArrayType array_type =
      isolate->factory()->GetArrayTypeFromElementsKind(fixed_elements_kind);
  size_t element_size =
      isolate->factory()->GetExternalArrayElementSize(array_type);

  Handle<JSArrayBuffer> buffer = isolate->factory()->NewJSArrayBuffer();
  size_t length = 0;
  if (source->IsJSTypedArray() &&
      JSTypedArray::cast(*source)->type() == array_type) {
    length = JSTypedArray::cast(*source)->length_value();
  } else {
    CHECK(TryNumberToSize(*length_obj, &length));
  }

  if ((length > static_cast<unsigned>(Smi::kMaxValue)) ||
      (length > (kMaxInt / element_size))) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kInvalidTypedArrayLength));
  }
  size_t byte_length = length * element_size;

  DCHECK_EQ(v8::ArrayBufferView::kEmbedderFieldCount,
            holder->GetEmbedderFieldCount());
  for (int i = 0; i < v8::ArrayBufferView::kEmbedderFieldCount; i++) {
    holder->SetEmbedderField(i, Smi::kZero);
  }

  if (!JSArrayBuffer::SetupAllocatingData(buffer, isolate, byte_length,
                                          false)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kInvalidArrayBufferLength));
  }

  holder->set_buffer(*buffer);
  holder->set_byte_offset(Smi::kZero);
  Handle<Object> byte_length_obj(
      isolate->factory()->NewNumberFromSize(byte_length));
  holder->set_byte_length(*byte_length_obj);
  length_obj = isolate->factory()->NewNumberFromSize(length);
  holder->set_length(*length_obj);

  Handle<FixedTypedArrayBase> elements =
      isolate->factory()->NewFixedTypedArrayWithExternalPointer(
          static_cast<int>(length), array_type,
          static_cast<uint8_t*>(buffer->backing_store()));
  holder->set_elements(*elements);

  // Initialize the backing store. We can use a special path for typed arrays of
  // the same type, but we need to make sure everything is properly observable
  // for other types.
  if (source->IsJSTypedArray()) {
    Handle<JSTypedArray> typed_array(JSTypedArray::cast(*source));

    if (typed_array->type() == holder->type()) {
      uint8_t* backing_store =
          static_cast<uint8_t*>(typed_array->GetBuffer()->backing_store());
      size_t source_byte_offset = NumberToSize(typed_array->byte_offset());
      memcpy(buffer->backing_store(), backing_store + source_byte_offset,
             byte_length);
      return isolate->heap()->true_value();
    }
  }
  return CopyElements(isolate, holder, source, length);
}


#define BUFFER_VIEW_GETTER(Type, getter, accessor)   \
  RUNTIME_FUNCTION(Runtime_##Type##Get##getter) {    \
    HandleScope scope(isolate);                      \
    DCHECK_EQ(1, args.length());                     \
    CONVERT_ARG_HANDLE_CHECKED(JS##Type, holder, 0); \
    return holder->accessor();                       \
  }

BUFFER_VIEW_GETTER(ArrayBufferView, ByteLength, byte_length)
BUFFER_VIEW_GETTER(ArrayBufferView, ByteOffset, byte_offset)
BUFFER_VIEW_GETTER(TypedArray, Length, length)

#undef BUFFER_VIEW_GETTER

RUNTIME_FUNCTION(Runtime_TypedArrayGetBuffer) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSTypedArray, holder, 0);
  return *holder->GetBuffer();
}


// Return codes for Runtime_TypedArraySetFastCases.
// Should be synchronized with typedarray.js natives.
enum TypedArraySetResultCodes {
  // Set from typed array of the same type.
  // This is processed by TypedArraySetFastCases
  TYPED_ARRAY_SET_TYPED_ARRAY_SAME_TYPE = 0,
  // Set from typed array of the different type, overlapping in memory.
  TYPED_ARRAY_SET_TYPED_ARRAY_OVERLAPPING = 1,
  // Set from typed array of the different type, non-overlapping.
  TYPED_ARRAY_SET_TYPED_ARRAY_NONOVERLAPPING = 2,
  // Set from non-typed array.
  TYPED_ARRAY_SET_NON_TYPED_ARRAY = 3
};


RUNTIME_FUNCTION(Runtime_TypedArraySetFastCases) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  if (!args[0]->IsJSTypedArray()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotTypedArray));
  }

  if (!args[1]->IsJSTypedArray())
    return Smi::FromInt(TYPED_ARRAY_SET_NON_TYPED_ARRAY);

  CONVERT_ARG_HANDLE_CHECKED(JSTypedArray, target_obj, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSTypedArray, source_obj, 1);
  CONVERT_NUMBER_ARG_HANDLE_CHECKED(offset_obj, 2);

  Handle<JSTypedArray> target(JSTypedArray::cast(*target_obj));
  Handle<JSTypedArray> source(JSTypedArray::cast(*source_obj));
  size_t offset = 0;
  CHECK(TryNumberToSize(*offset_obj, &offset));
  size_t target_length = target->length_value();
  size_t source_length = source->length_value();
  size_t target_byte_length = NumberToSize(target->byte_length());
  size_t source_byte_length = NumberToSize(source->byte_length());
  if (offset > target_length || offset + source_length > target_length ||
      offset + source_length < offset) {  // overflow
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kTypedArraySetSourceTooLarge));
  }

  size_t target_offset = NumberToSize(target->byte_offset());
  size_t source_offset = NumberToSize(source->byte_offset());
  uint8_t* target_base =
      static_cast<uint8_t*>(target->GetBuffer()->backing_store()) +
      target_offset;
  uint8_t* source_base =
      static_cast<uint8_t*>(source->GetBuffer()->backing_store()) +
      source_offset;

  // Typed arrays of the same type: use memmove.
  if (target->type() == source->type()) {
    memmove(target_base + offset * target->element_size(), source_base,
            source_byte_length);
    return Smi::FromInt(TYPED_ARRAY_SET_TYPED_ARRAY_SAME_TYPE);
  }

  // Typed arrays of different types over the same backing store
  if ((source_base <= target_base &&
       source_base + source_byte_length > target_base) ||
      (target_base <= source_base &&
       target_base + target_byte_length > source_base)) {
    // We do not support overlapping ArrayBuffers
    DCHECK(target->GetBuffer()->backing_store() ==
           source->GetBuffer()->backing_store());
    return Smi::FromInt(TYPED_ARRAY_SET_TYPED_ARRAY_OVERLAPPING);
  } else {  // Non-overlapping typed arrays
    return Smi::FromInt(TYPED_ARRAY_SET_TYPED_ARRAY_NONOVERLAPPING);
  }
}

namespace {

template <typename T>
bool CompareNum(T x, T y) {
  if (x < y) {
    return true;
  } else if (x > y) {
    return false;
  } else if (!std::is_integral<T>::value) {
    double _x = x, _y = y;
    if (x == 0 && x == y) {
      /* -0.0 is less than +0.0 */
      return std::signbit(_x) && !std::signbit(_y);
    } else if (!std::isnan(_x) && std::isnan(_y)) {
      /* number is less than NaN */
      return true;
    }
  }
  return false;
}

}  // namespace

RUNTIME_FUNCTION(Runtime_TypedArraySortFast) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());

  CONVERT_ARG_HANDLE_CHECKED(Object, target_obj, 0);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.sort";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, target_obj, method));

  // This line can be removed when JSTypedArray::Validate throws
  // if array.[[ViewedArrayBuffer]] is neutered(v8:4648)
  if (V8_UNLIKELY(array->WasNeutered())) return *array;

  size_t length = array->length_value();
  if (length <= 1) return *array;

  Handle<FixedTypedArrayBase> elements(
      FixedTypedArrayBase::cast(array->elements()));
  switch (array->type()) {
#define TYPED_ARRAY_SORT(Type, type, TYPE, ctype, size)     \
  case kExternal##Type##Array: {                            \
    ctype* data = static_cast<ctype*>(elements->DataPtr()); \
    if (kExternal##Type##Array == kExternalFloat64Array ||  \
        kExternal##Type##Array == kExternalFloat32Array)    \
      std::sort(data, data + length, CompareNum<ctype>);    \
    else                                                    \
      std::sort(data, data + length);                       \
    break;                                                  \
  }

    TYPED_ARRAYS(TYPED_ARRAY_SORT)
#undef TYPED_ARRAY_SORT
  }

  return *array;
}

RUNTIME_FUNCTION(Runtime_TypedArrayMaxSizeInHeap) {
  DCHECK_EQ(0, args.length());
  DCHECK_OBJECT_SIZE(FLAG_typed_array_max_size_in_heap +
                     FixedTypedArrayBase::kDataOffset);
  return Smi::FromInt(FLAG_typed_array_max_size_in_heap);
}


RUNTIME_FUNCTION(Runtime_IsTypedArray) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  return isolate->heap()->ToBoolean(args[0]->IsJSTypedArray());
}


RUNTIME_FUNCTION(Runtime_IsSharedTypedArray) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  return isolate->heap()->ToBoolean(
      args[0]->IsJSTypedArray() &&
      JSTypedArray::cast(args[0])->GetBuffer()->is_shared());
}


RUNTIME_FUNCTION(Runtime_IsSharedIntegerTypedArray) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  if (!args[0]->IsJSTypedArray()) {
    return isolate->heap()->false_value();
  }

  Handle<JSTypedArray> obj(JSTypedArray::cast(args[0]));
  return isolate->heap()->ToBoolean(obj->GetBuffer()->is_shared() &&
                                    obj->type() != kExternalFloat32Array &&
                                    obj->type() != kExternalFloat64Array &&
                                    obj->type() != kExternalUint8ClampedArray);
}


RUNTIME_FUNCTION(Runtime_IsSharedInteger32TypedArray) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  if (!args[0]->IsJSTypedArray()) {
    return isolate->heap()->false_value();
  }

  Handle<JSTypedArray> obj(JSTypedArray::cast(args[0]));
  return isolate->heap()->ToBoolean(obj->GetBuffer()->is_shared() &&
                                    obj->type() == kExternalInt32Array);
}

}  // namespace internal
}  // namespace v8
