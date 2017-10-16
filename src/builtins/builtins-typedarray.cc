// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/counters.h"
#include "src/elements.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 22.2 TypedArray Objects

// ES6 section 22.2.3.1 get %TypedArray%.prototype.buffer
BUILTIN(TypedArrayPrototypeBuffer) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSTypedArray, typed_array,
                 "get %TypedArray%.prototype.buffer");
  return *typed_array->GetBuffer();
}

namespace {

int64_t CapRelativeIndex(Handle<Object> num, int64_t minimum, int64_t maximum) {
  int64_t relative;
  if (V8_LIKELY(num->IsSmi())) {
    relative = Smi::ToInt(*num);
  } else {
    DCHECK(num->IsHeapNumber());
    double fp = HeapNumber::cast(*num)->value();
    if (V8_UNLIKELY(!std::isfinite(fp))) {
      // +Infinity / -Infinity
      DCHECK(!std::isnan(fp));
      return fp < 0 ? minimum : maximum;
    }
    relative = static_cast<int64_t>(fp);
  }
  return relative < 0 ? std::max<int64_t>(relative + maximum, minimum)
                      : std::min<int64_t>(relative, maximum);
}

MaybeHandle<JSTypedArray> TypedArraySpeciesCreateByLength(
    Isolate* isolate, Handle<JSTypedArray> exemplar, const char* method_name,
    int64_t length) {
  const int argc = 1;
  ScopedVector<Handle<Object>> argv(argc);
  argv[0] = isolate->factory()->NewNumberFromInt64(length);
  return JSTypedArray::SpeciesCreate(isolate, exemplar, argc, argv.start(),
                                     method_name);
}

}  // namespace

BUILTIN(TypedArrayPrototypeCopyWithin) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.copyWithin";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  int64_t len = array->length_value();
  int64_t to = 0;
  int64_t from = 0;
  int64_t final = len;

  if (V8_LIKELY(args.length() > 1)) {
    Handle<Object> num;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num, Object::ToInteger(isolate, args.at<Object>(1)));
    to = CapRelativeIndex(num, 0, len);

    if (args.length() > 2) {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, num, Object::ToInteger(isolate, args.at<Object>(2)));
      from = CapRelativeIndex(num, 0, len);

      Handle<Object> end = args.atOrUndefined(isolate, 3);
      if (!end->IsUndefined(isolate)) {
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, num,
                                           Object::ToInteger(isolate, end));
        final = CapRelativeIndex(num, 0, len);
      }
    }
  }

  int64_t count = std::min<int64_t>(final - from, len - to);
  if (count <= 0) return *array;

  // TypedArray buffer may have been transferred/detached during parameter
  // processing above. Return early in this case, to prevent potential UAF error
  // TODO(caitp): throw here, as though the full algorithm were performed (the
  // throw would have come from ecma262/#sec-integerindexedelementget)
  // (see )
  if (V8_UNLIKELY(array->WasNeutered())) return *array;

  // Ensure processed indexes are within array bounds
  DCHECK_GE(from, 0);
  DCHECK_LT(from, len);
  DCHECK_GE(to, 0);
  DCHECK_LT(to, len);
  DCHECK_GE(len - count, 0);

  Handle<FixedTypedArrayBase> elements(
      FixedTypedArrayBase::cast(array->elements()));
  size_t element_size = array->element_size();
  to = to * element_size;
  from = from * element_size;
  count = count * element_size;

  uint8_t* data = static_cast<uint8_t*>(elements->DataPtr());
  std::memmove(data + to, data + from, count);

  return *array;
}

BUILTIN(TypedArrayPrototypeFill) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.fill";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  Handle<Object> obj_value = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, obj_value, Object::ToNumber(obj_value));

  int64_t len = array->length_value();
  int64_t start = 0;
  int64_t end = len;

  if (args.length() > 2) {
    Handle<Object> num = args.atOrUndefined(isolate, 2);
    if (!num->IsUndefined(isolate)) {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, num, Object::ToInteger(isolate, num));
      start = CapRelativeIndex(num, 0, len);

      num = args.atOrUndefined(isolate, 3);
      if (!num->IsUndefined(isolate)) {
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
            isolate, num, Object::ToInteger(isolate, num));
        end = CapRelativeIndex(num, 0, len);
      }
    }
  }

  int64_t count = end - start;
  if (count <= 0) return *array;

  if (V8_UNLIKELY(array->WasNeutered())) return *array;

  // Ensure processed indexes are within array bounds
  DCHECK_GE(start, 0);
  DCHECK_LT(start, len);
  DCHECK_GE(end, 0);
  DCHECK_LE(end, len);
  DCHECK_LE(count, len);

  return array->GetElementsAccessor()->Fill(isolate, array, obj_value,
                                            static_cast<uint32_t>(start),
                                            static_cast<uint32_t>(end));
}

BUILTIN(TypedArrayPrototypeIncludes) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.includes";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  if (args.length() < 2) return isolate->heap()->false_value();

  int64_t len = array->length_value();
  if (len == 0) return isolate->heap()->false_value();

  int64_t index = 0;
  if (args.length() > 2) {
    Handle<Object> num;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num, Object::ToInteger(isolate, args.at<Object>(2)));
    index = CapRelativeIndex(num, 0, len);
  }

  // TODO(cwhan.tunz): throw. See the above comment in CopyWithin.
  if (V8_UNLIKELY(array->WasNeutered())) return isolate->heap()->false_value();

  Handle<Object> search_element = args.atOrUndefined(isolate, 1);
  ElementsAccessor* elements = array->GetElementsAccessor();
  Maybe<bool> result = elements->IncludesValue(isolate, array, search_element,
                                               static_cast<uint32_t>(index),
                                               static_cast<uint32_t>(len));
  MAYBE_RETURN(result, isolate->heap()->exception());
  return *isolate->factory()->ToBoolean(result.FromJust());
}

BUILTIN(TypedArrayPrototypeIndexOf) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.indexOf";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  int64_t len = array->length_value();
  if (len == 0) return Smi::FromInt(-1);

  int64_t index = 0;
  if (args.length() > 2) {
    Handle<Object> num;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num, Object::ToInteger(isolate, args.at<Object>(2)));
    index = CapRelativeIndex(num, 0, len);
  }

  // TODO(cwhan.tunz): throw. See the above comment in CopyWithin.
  if (V8_UNLIKELY(array->WasNeutered())) return Smi::FromInt(-1);

  Handle<Object> search_element = args.atOrUndefined(isolate, 1);
  ElementsAccessor* elements = array->GetElementsAccessor();
  Maybe<int64_t> result = elements->IndexOfValue(isolate, array, search_element,
                                                 static_cast<uint32_t>(index),
                                                 static_cast<uint32_t>(len));
  MAYBE_RETURN(result, isolate->heap()->exception());
  return *isolate->factory()->NewNumberFromInt64(result.FromJust());
}

BUILTIN(TypedArrayPrototypeLastIndexOf) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.lastIndexOf";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  int64_t len = array->length_value();
  if (len == 0) return Smi::FromInt(-1);

  int64_t index = len - 1;
  if (args.length() > 2) {
    Handle<Object> num;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num, Object::ToInteger(isolate, args.at<Object>(2)));
    // Set a negative value (-1) for returning -1 if num is negative and
    // len + num is still negative. Upper bound is len - 1.
    index = std::min<int64_t>(CapRelativeIndex(num, -1, len), len - 1);
  }

  if (index < 0) return Smi::FromInt(-1);

  // TODO(cwhan.tunz): throw. See the above comment in CopyWithin.
  if (V8_UNLIKELY(array->WasNeutered())) return Smi::FromInt(-1);

  Handle<Object> search_element = args.atOrUndefined(isolate, 1);
  ElementsAccessor* elements = array->GetElementsAccessor();
  Maybe<int64_t> result = elements->LastIndexOfValue(
      isolate, array, search_element, static_cast<uint32_t>(index));
  MAYBE_RETURN(result, isolate->heap()->exception());
  return *isolate->factory()->NewNumberFromInt64(result.FromJust());
}

BUILTIN(TypedArrayPrototypeReverse) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.reverse";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  ElementsAccessor* elements = array->GetElementsAccessor();
  elements->Reverse(*array);
  return *array;
}

namespace {
Object* TypedArrayCopyElements(Handle<JSTypedArray> target,
                               Handle<JSReceiver> source, Object* length_obj) {
  size_t length;
  CHECK(TryNumberToSize(length_obj, &length));

  ElementsAccessor* accessor = target->GetElementsAccessor();
  return accessor->CopyElements(source, target, length);
}

enum class TypedArraySetResultCodes {
  // Set from typed array of the same type.
  // This is processed by TypedArraySetFastCases
  SAME_TYPE,
  // Set from typed array of the different type, overlapping in memory.
  OVERLAPPING,
  // Set from typed array of the different type, non-overlapping.
  NONOVERLAPPING,
  // Set from non-typed array.
  NON_TYPED_ARRAY
};

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
                                                  value, LanguageMode::kStrict),
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
                           LanguageMode::kStrict),
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
                           LanguageMode::kStrict),
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
                           LanguageMode::kStrict),
        Object);
  }

  return target;
}

MaybeHandle<Smi> TypedArraySetFastCases(Isolate* isolate,
                                        Handle<JSTypedArray> target,
                                        Handle<Object> source_obj,
                                        Handle<Object> offset_obj) {
  if (!source_obj->IsJSTypedArray()) {
    return MaybeHandle<Smi>(
        Smi::FromEnum(TypedArraySetResultCodes::NON_TYPED_ARRAY), isolate);
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
    return MaybeHandle<Smi>(Smi::FromEnum(TypedArraySetResultCodes::SAME_TYPE),
                            isolate);
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
        Smi::FromEnum(TypedArraySetResultCodes::OVERLAPPING), isolate);
  } else {  // Non-overlapping typed arrays
    return MaybeHandle<Smi>(
        Smi::FromEnum(TypedArraySetResultCodes::NONOVERLAPPING), isolate);
  }
}

}  // anonymous namespace

// 22.2.3.23%TypedArray%.prototype.set ( overloaded [ , offset ] )
BUILTIN(TypedArrayPrototypeSet) {
  HandleScope scope(isolate);

  Handle<Object> target = args.receiver();
  Handle<Object> obj = args.atOrUndefined(isolate, 1);
  Handle<Object> offset = args.atOrUndefined(isolate, 2);

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
    case TypedArraySetResultCodes::SAME_TYPE: {
      break;
    }
    case TypedArraySetResultCodes::OVERLAPPING: {
      RETURN_FAILURE_ON_EXCEPTION(
          isolate, TypedArraySetFromOverlapping(
                       isolate, Handle<JSTypedArray>::cast(target),
                       Handle<JSTypedArray>::cast(obj), int_offset));
      break;
    }
    case TypedArraySetResultCodes::NONOVERLAPPING: {
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
    case TypedArraySetResultCodes::NON_TYPED_ARRAY: {
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

      DCHECK_GE(int_offset, 0);
      if (int_offset + len->Number() >
          Handle<JSTypedArray>::cast(target)->length_value()) {
        THROW_NEW_ERROR_RETURN_FAILURE(
            isolate,
            NewRangeError(MessageTemplate::kTypedArraySetSourceTooLarge));
      }
      uint32_t int_l;
      CHECK(DoubleToUint32IfEqualToSelf(len->Number(), &int_l));
      RETURN_FAILURE_ON_EXCEPTION(
          isolate, TypedArraySetFromArrayLike(
                       isolate, Handle<JSTypedArray>::cast(target), obj, int_l,
                       int_offset));
    } break;
  }

  return *isolate->factory()->undefined_value();
}

BUILTIN(TypedArrayPrototypeSlice) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.slice";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  int64_t len = array->length_value();
  int64_t start = 0;
  int64_t end = len;
  {
    Handle<Object> num = args.atOrUndefined(isolate, 1);
    if (!num->IsUndefined(isolate)) {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, num,
                                         Object::ToInteger(isolate, num));
      start = CapRelativeIndex(num, 0, len);

      num = args.atOrUndefined(isolate, 2);
      if (!num->IsUndefined(isolate)) {
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, num,
                                           Object::ToInteger(isolate, num));
        end = CapRelativeIndex(num, 0, len);
      }
    }
  }

  int64_t count = std::max<int64_t>(end - start, 0);

  Handle<JSTypedArray> result_array;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result_array,
      TypedArraySpeciesCreateByLength(isolate, array, method, count));

  // TODO(cwhan.tunz): neutering check of the result_array should be done in
  // TypedArraySpeciesCreate, but currently ValidateTypedArray does not throw
  // for neutered buffer, so this is a temporary neutering check for the result
  // array
  if (V8_UNLIKELY(result_array->WasNeutered())) return *result_array;

  // TODO(cwhan.tunz): should throw.
  if (V8_UNLIKELY(array->WasNeutered())) return *result_array;

  if (count == 0) return *result_array;

  ElementsAccessor* accessor = array->GetElementsAccessor();
  return *accessor->Slice(array, static_cast<uint32_t>(start),
                          static_cast<uint32_t>(end), result_array);
}

}  // namespace internal
}  // namespace v8
