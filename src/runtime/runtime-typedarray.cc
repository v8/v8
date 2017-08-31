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
  Handle<Object> argument = args.at(0);
  // This runtime function is exposed in ClusterFuzz and as such has to
  // support arbitrary arguments.
  if (!argument->IsJSArrayBuffer()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotTypedArray));
  }
  Handle<JSArrayBuffer> array_buffer = Handle<JSArrayBuffer>::cast(argument);
  if (!array_buffer->is_neuterable()) {
    return isolate->heap()->undefined_value();
  }
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
Object* TypedArrayCopyElements(Handle<JSTypedArray> target,
                               Handle<JSReceiver> source, Object* length_obj) {
  size_t length;
  CHECK(TryNumberToSize(length_obj, &length));

  ElementsAccessor* accessor = target->GetElementsAccessor();
  return accessor->CopyElements(source, target, length);
}
}  // anonymous namespace

RUNTIME_FUNCTION(Runtime_TypedArrayCopyElements) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSTypedArray, target, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSReceiver, source, 1);
  CONVERT_NUMBER_ARG_HANDLE_CHECKED(length_obj, 2);

  return TypedArrayCopyElements(target, source, *length_obj);
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

RUNTIME_FUNCTION(Runtime_ArrayBufferViewWasNeutered) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  return isolate->heap()->ToBoolean(JSTypedArray::cast(args[0])->WasNeutered());
}

RUNTIME_FUNCTION(Runtime_TypedArrayGetBuffer) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSTypedArray, holder, 0);
  return *holder->GetBuffer();
}

enum TypedArraySetResultCodes {
  // Set from typed array of the same type.
  // This is processed by TypedArraySetFastCases
  TYPED_ARRAY_SET_TYPED_ARRAY_SAME_TYPE,
  // Set from typed array of the different type, overlapping in memory.
  TYPED_ARRAY_SET_TYPED_ARRAY_OVERLAPPING,
  // Set from typed array of the different type, non-overlapping.
  TYPED_ARRAY_SET_TYPED_ARRAY_NONOVERLAPPING,
  // Set from non-typed array.
  TYPED_ARRAY_SET_NON_TYPED_ARRAY
};

namespace {
MaybeHandle<Object> TypedArraySetFromArrayLike(Isolate* isolate,
                                               Handle<JSTypedArray> target,
                                               Handle<Object> source,
                                               int source_length, int offset) {
  DCHECK_GE(source_length, 0);
  DCHECK_GE(offset, 0);

  for (int i = 0; i < source_length; i++) {
    Handle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                               Object::GetElement(isolate, source, i), Object);
    ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                               Object::SetElement(isolate, target, offset + i,
                                                  value, LanguageMode::STRICT),
                               Object);
  }

  return target;
}

MaybeHandle<Object> TypedArraySetFromOverlapping(Isolate* isolate,
                                                 Handle<JSTypedArray> target,
                                                 Handle<JSTypedArray> source,
                                                 int offset) {
  DCHECK_GE(offset, 0);

  size_t sourceElementSize = source->element_size();
  size_t targetElementSize = target->element_size();

  uint32_t source_length = source->length_value();
  if (source_length == 0) return target;

  // Copy left part.

  // First un-mutated byte after the next write
  uint32_t target_ptr = 0;
  CHECK(target->byte_offset()->ToUint32(&target_ptr));
  target_ptr += (offset + 1) * targetElementSize;

  // Next read at sourcePtr. We do not care for memory changing before
  // sourcePtr - we have already copied it.
  uint32_t source_ptr = 0;
  CHECK(source->byte_offset()->ToUint32(&source_ptr));

  uint32_t left_index;
  for (left_index = 0; left_index < source_length && target_ptr <= source_ptr;
       left_index++) {
    Handle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                               Object::GetElement(isolate, source, left_index),
                               Object);
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, value,
        Object::SetElement(isolate, target, offset + left_index, value,
                           LanguageMode::STRICT),
        Object);

    target_ptr += targetElementSize;
    source_ptr += sourceElementSize;
  }

  // Copy right part;
  // First unmutated byte before the next write
  CHECK(target->byte_offset()->ToUint32(&target_ptr));
  target_ptr += (offset + source_length - 1) * targetElementSize;

  // Next read before sourcePtr. We do not care for memory changing after
  // sourcePtr - we have already copied it.
  CHECK(target->byte_offset()->ToUint32(&source_ptr));
  source_ptr += source_length * sourceElementSize;

  uint32_t right_index;
  DCHECK_GE(source_length, 1);
  for (right_index = source_length - 1;
       right_index > left_index && target_ptr >= source_ptr; right_index--) {
    Handle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                               Object::GetElement(isolate, source, right_index),
                               Object);
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, value,
        Object::SetElement(isolate, target, offset + right_index, value,
                           LanguageMode::STRICT),
        Object);

    target_ptr -= targetElementSize;
    source_ptr -= sourceElementSize;
  }

  std::vector<Handle<Object>> temp(right_index + 1 - left_index);

  for (uint32_t i = left_index; i <= right_index; i++) {
    Handle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                               Object::GetElement(isolate, source, i), Object);
    temp[i - left_index] = value;
  }

  for (uint32_t i = left_index; i <= right_index; i++) {
    Handle<Object> value;

    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, value,
        Object::SetElement(isolate, target, offset + i, temp[i - left_index],
                           LanguageMode::STRICT),
        Object);
  }

  return target;
}

MaybeHandle<Smi> TypedArraySetFastCases(Isolate* isolate,
                                        Handle<JSTypedArray> target,
                                        Handle<Object> source_obj,
                                        Handle<Object> offset_obj) {
  if (!source_obj->IsJSTypedArray()) {
    return MaybeHandle<Smi>(Smi::FromEnum(TYPED_ARRAY_SET_NON_TYPED_ARRAY),
                            isolate);
  }

  Handle<JSTypedArray> source = Handle<JSTypedArray>::cast(source_obj);

  size_t offset = 0;
  CHECK(TryNumberToSize(*offset_obj, &offset));
  size_t target_length = target->length_value();
  size_t source_length = source->length_value();
  size_t target_byte_length = NumberToSize(target->byte_length());
  size_t source_byte_length = NumberToSize(source->byte_length());
  if (offset > target_length || offset + source_length > target_length ||
      offset + source_length < offset) {  // overflow
    THROW_NEW_ERROR(
        isolate, NewRangeError(MessageTemplate::kTypedArraySetSourceTooLarge),
        Smi);
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
    return MaybeHandle<Smi>(
        Smi::FromEnum(TYPED_ARRAY_SET_TYPED_ARRAY_SAME_TYPE), isolate);
  }

  // Typed arrays of different types over the same backing store
  if ((source_base <= target_base &&
       source_base + source_byte_length > target_base) ||
      (target_base <= source_base &&
       target_base + target_byte_length > source_base)) {
    // We do not support overlapping ArrayBuffers
    DCHECK(target->GetBuffer()->backing_store() ==
           source->GetBuffer()->backing_store());
    return MaybeHandle<Smi>(
        Smi::FromEnum(TYPED_ARRAY_SET_TYPED_ARRAY_OVERLAPPING), isolate);
  } else {  // Non-overlapping typed arrays
    return MaybeHandle<Smi>(
        Smi::FromEnum(TYPED_ARRAY_SET_TYPED_ARRAY_NONOVERLAPPING), isolate);
  }
}

}  // anonymous namespace

// 22.2.3.23%TypedArray%.prototype.set ( overloaded [ , offset ] )
RUNTIME_FUNCTION(Runtime_TypedArrayPrototypeSet) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, target, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, obj, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, offset, 2);

  if (offset->IsUndefined(isolate)) {
    offset = Handle<Object>(Smi::kZero, isolate);
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, offset,
                                       Object::ToInteger(isolate, offset));
  }

  if (offset->Number() < 0) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kTypedArraySetNegativeOffset));
  }

  if (offset->Number() > Smi::kMaxValue) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kTypedArraySetSourceTooLarge));
  }

  if (!target->IsJSTypedArray()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotTypedArray));
  }
  auto int_offset = static_cast<int>(offset->Number());

  Handle<Smi> result_code;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result_code,
      TypedArraySetFastCases(isolate, Handle<JSTypedArray>::cast(target), obj,
                             offset));

  switch (static_cast<TypedArraySetResultCodes>(result_code->value())) {
    case TYPED_ARRAY_SET_TYPED_ARRAY_SAME_TYPE: {
      break;
    }
    case TYPED_ARRAY_SET_TYPED_ARRAY_OVERLAPPING: {
      RETURN_FAILURE_ON_EXCEPTION(
          isolate, TypedArraySetFromOverlapping(
                       isolate, Handle<JSTypedArray>::cast(target),
                       Handle<JSTypedArray>::cast(obj), int_offset));
      break;
    }
    case TYPED_ARRAY_SET_TYPED_ARRAY_NONOVERLAPPING: {
      if (int_offset == 0) {
        TypedArrayCopyElements(Handle<JSTypedArray>::cast(target),
                               Handle<JSTypedArray>::cast(obj),
                               Handle<JSTypedArray>::cast(obj)->length());
      } else {
        RETURN_FAILURE_ON_EXCEPTION(
            isolate,
            TypedArraySetFromArrayLike(
                isolate, Handle<JSTypedArray>::cast(target), obj,
                Handle<JSTypedArray>::cast(obj)->length_value(), int_offset));
      }
      break;
    }
    case TYPED_ARRAY_SET_NON_TYPED_ARRAY: {
      if (obj->IsNumber()) {
        // For number as a first argument, throw TypeError
        // instead of silently ignoring the call, so that
        // users know they did something wrong.
        // (Consistent with Firefox and Blink/WebKit)
        THROW_NEW_ERROR_RETURN_FAILURE(
            isolate, NewTypeError(MessageTemplate::kInvalidArgument));
      }

      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, obj,
                                         Object::ToObject(isolate, obj));

      Handle<Object> len;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, len,
          Object::GetProperty(obj, isolate->factory()->length_string()));
      if (len->IsUndefined(isolate)) {
        break;
      }
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, len,
                                         Object::ToLength(isolate, len));

      uint32_t int_l;
      CHECK(len->ToUint32(&int_l));
      DCHECK_GE(int_offset, 0);
      if (static_cast<uint32_t>(int_offset) + int_l >
          Handle<JSTypedArray>::cast(target)->length_value()) {
        THROW_NEW_ERROR_RETURN_FAILURE(
            isolate,
            NewRangeError(MessageTemplate::kTypedArraySetSourceTooLarge));
      }
      RETURN_FAILURE_ON_EXCEPTION(
          isolate, TypedArraySetFromArrayLike(
                       isolate, Handle<JSTypedArray>::cast(target), obj, int_l,
                       int_offset));
    } break;
    default:
      UNREACHABLE();
  }

  return *target;
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

RUNTIME_FUNCTION(Runtime_TypedArraySpeciesCreateByLength) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 2);
  Handle<JSTypedArray> exemplar = args.at<JSTypedArray>(0);
  Handle<Object> length = args.at(1);
  int argc = 1;
  ScopedVector<Handle<Object>> argv(argc);
  argv[0] = length;
  Handle<JSTypedArray> result_array;
  // TODO(tebbi): Pass correct method name.
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result_array,
      JSTypedArray::SpeciesCreate(isolate, exemplar, argc, argv.start(), ""));
  return *result_array;
}

}  // namespace internal
}  // namespace v8
