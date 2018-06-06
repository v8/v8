// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/elements.h"
#include "src/heap/factory.h"
#include "src/messages.h"
#include "src/objects-inl.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {

namespace {

bool NeedToFlipBytes(bool is_little_endian) {
#ifdef V8_TARGET_LITTLE_ENDIAN
  return !is_little_endian;
#else
  return is_little_endian;
#endif
}

template <size_t n>
void CopyBytes(uint8_t* target, uint8_t const* source) {
  for (size_t i = 0; i < n; i++) {
    *(target++) = *(source++);
  }
}

template <size_t n>
void FlipBytes(uint8_t* target, uint8_t const* source) {
  source = source + (n - 1);
  for (size_t i = 0; i < n; i++) {
    *(target++) = *(source--);
  }
}

template <typename T>
MaybeHandle<Object> AllocateResult(Isolate* isolate, T value) {
  return isolate->factory()->NewNumber(value);
}

template <>
MaybeHandle<Object> AllocateResult(Isolate* isolate, int64_t value) {
  return BigInt::FromInt64(isolate, value);
}

template <>
MaybeHandle<Object> AllocateResult(Isolate* isolate, uint64_t value) {
  return BigInt::FromUint64(isolate, value);
}

// ES6 section 24.2.1.1 GetViewValue (view, requestIndex, isLittleEndian, type)
template <typename T>
MaybeHandle<Object> GetViewValue(Isolate* isolate, Handle<JSDataView> data_view,
                                 Handle<Object> request_index,
                                 bool is_little_endian, const char* method) {
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, request_index,
      Object::ToIndex(isolate, request_index,
                      MessageTemplate::kInvalidDataViewAccessorOffset),
      Object);
  size_t get_index = 0;
  if (!TryNumberToSize(*request_index, &get_index)) {
    THROW_NEW_ERROR(
        isolate, NewRangeError(MessageTemplate::kInvalidDataViewAccessorOffset),
        Object);
  }
  Handle<JSArrayBuffer> buffer(JSArrayBuffer::cast(data_view->buffer()),
                               isolate);
  if (buffer->was_neutered()) {
    Handle<String> operation =
        isolate->factory()->NewStringFromAsciiChecked(method);
    THROW_NEW_ERROR(
        isolate, NewTypeError(MessageTemplate::kDetachedOperation, operation),
        Object);
  }
  size_t const data_view_byte_offset = NumberToSize(data_view->byte_offset());
  size_t const data_view_byte_length = NumberToSize(data_view->byte_length());
  if (get_index + sizeof(T) > data_view_byte_length ||
      get_index + sizeof(T) < get_index) {  // overflow
    THROW_NEW_ERROR(
        isolate, NewRangeError(MessageTemplate::kInvalidDataViewAccessorOffset),
        Object);
  }
  union {
    T data;
    uint8_t bytes[sizeof(T)];
  } v;
  size_t const buffer_offset = data_view_byte_offset + get_index;
  DCHECK_GE(NumberToSize(buffer->byte_length()), buffer_offset + sizeof(T));
  uint8_t const* const source =
      static_cast<uint8_t*>(buffer->backing_store()) + buffer_offset;
  if (NeedToFlipBytes(is_little_endian)) {
    FlipBytes<sizeof(T)>(v.bytes, source);
  } else {
    CopyBytes<sizeof(T)>(v.bytes, source);
  }
  return AllocateResult<T>(isolate, v.data);
}

template <typename T>
MaybeHandle<Object> DataViewConvertInput(Isolate* isolate,
                                         Handle<Object> input) {
  return Object::ToNumber(input);
}

template <>
MaybeHandle<Object> DataViewConvertInput<int64_t>(Isolate* isolate,
                                                  Handle<Object> input) {
  return BigInt::FromObject(isolate, input);
}

template <>
MaybeHandle<Object> DataViewConvertInput<uint64_t>(Isolate* isolate,
                                                   Handle<Object> input) {
  return BigInt::FromObject(isolate, input);
}

template <typename T>
T DataViewConvertValue(Handle<Object> value);

template <>
int8_t DataViewConvertValue<int8_t>(Handle<Object> value) {
  return static_cast<int8_t>(DoubleToInt32(value->Number()));
}

template <>
int16_t DataViewConvertValue<int16_t>(Handle<Object> value) {
  return static_cast<int16_t>(DoubleToInt32(value->Number()));
}

template <>
int32_t DataViewConvertValue<int32_t>(Handle<Object> value) {
  return DoubleToInt32(value->Number());
}

template <>
uint8_t DataViewConvertValue<uint8_t>(Handle<Object> value) {
  return static_cast<uint8_t>(DoubleToUint32(value->Number()));
}

template <>
uint16_t DataViewConvertValue<uint16_t>(Handle<Object> value) {
  return static_cast<uint16_t>(DoubleToUint32(value->Number()));
}

template <>
uint32_t DataViewConvertValue<uint32_t>(Handle<Object> value) {
  return DoubleToUint32(value->Number());
}

template <>
float DataViewConvertValue<float>(Handle<Object> value) {
  return static_cast<float>(value->Number());
}

template <>
double DataViewConvertValue<double>(Handle<Object> value) {
  return value->Number();
}

template <>
int64_t DataViewConvertValue<int64_t>(Handle<Object> value) {
  return BigInt::cast(*value)->AsInt64();
}

template <>
uint64_t DataViewConvertValue<uint64_t>(Handle<Object> value) {
  return BigInt::cast(*value)->AsUint64();
}

// ES6 section 24.2.1.2 SetViewValue (view, requestIndex, isLittleEndian, type,
//                                    value)
template <typename T>
MaybeHandle<Object> SetViewValue(Isolate* isolate, Handle<JSDataView> data_view,
                                 Handle<Object> request_index,
                                 bool is_little_endian, Handle<Object> value,
                                 const char* method) {
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, request_index,
      Object::ToIndex(isolate, request_index,
                      MessageTemplate::kInvalidDataViewAccessorOffset),
      Object);
  ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                             DataViewConvertInput<T>(isolate, value), Object);
  size_t get_index = 0;
  if (!TryNumberToSize(*request_index, &get_index)) {
    THROW_NEW_ERROR(
        isolate, NewRangeError(MessageTemplate::kInvalidDataViewAccessorOffset),
        Object);
  }
  Handle<JSArrayBuffer> buffer(JSArrayBuffer::cast(data_view->buffer()),
                               isolate);
  if (buffer->was_neutered()) {
    Handle<String> operation =
        isolate->factory()->NewStringFromAsciiChecked(method);
    THROW_NEW_ERROR(
        isolate, NewTypeError(MessageTemplate::kDetachedOperation, operation),
        Object);
  }
  size_t const data_view_byte_offset = NumberToSize(data_view->byte_offset());
  size_t const data_view_byte_length = NumberToSize(data_view->byte_length());
  if (get_index + sizeof(T) > data_view_byte_length ||
      get_index + sizeof(T) < get_index) {  // overflow
    THROW_NEW_ERROR(
        isolate, NewRangeError(MessageTemplate::kInvalidDataViewAccessorOffset),
        Object);
  }
  union {
    T data;
    uint8_t bytes[sizeof(T)];
  } v;
  v.data = DataViewConvertValue<T>(value);
  size_t const buffer_offset = data_view_byte_offset + get_index;
  DCHECK(NumberToSize(buffer->byte_length()) >= buffer_offset + sizeof(T));
  uint8_t* const target =
      static_cast<uint8_t*>(buffer->backing_store()) + buffer_offset;
  if (NeedToFlipBytes(is_little_endian)) {
    FlipBytes<sizeof(T)>(target, v.bytes);
  } else {
    CopyBytes<sizeof(T)>(target, v.bytes);
  }
  return isolate->factory()->undefined_value();
}

}  // namespace

#define CHECK_RECEIVER_OBJECT(method)                                       \
  Handle<Object> receiver = args.at<Object>(0);                             \
  if (!receiver->IsJSDataView()) {                                          \
    THROW_NEW_ERROR_RETURN_FAILURE(                                         \
        isolate,                                                            \
        NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,          \
                     isolate->factory()->NewStringFromAsciiChecked(method), \
                     receiver));                                            \
  }                                                                         \
  Handle<JSDataView> data_view = Handle<JSDataView>::cast(receiver);

#define DATA_VIEW_PROTOTYPE_GET(Type, type)                         \
  RUNTIME_FUNCTION(Runtime_DataViewGet##Type) {                     \
    HandleScope scope(isolate);                                     \
    CHECK_RECEIVER_OBJECT("DataView.prototype.get" #Type);          \
    Handle<Object> byte_offset = args.at<Object>(1);                \
    Handle<Object> is_little_endian = args.at<Object>(2);           \
    Handle<Object> result;                                          \
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(                             \
        isolate, result,                                            \
        GetViewValue<type>(isolate, data_view, byte_offset,         \
                           is_little_endian->BooleanValue(isolate), \
                           "DataView.prototype.get" #Type));        \
    return *result;                                                 \
  }

DATA_VIEW_PROTOTYPE_GET(Float32, float)
DATA_VIEW_PROTOTYPE_GET(Float64, double)
DATA_VIEW_PROTOTYPE_GET(BigInt64, int64_t)
DATA_VIEW_PROTOTYPE_GET(BigUint64, uint64_t)
#undef DATA_VIEW_PROTOTYPE_GET

#define DATA_VIEW_PROTOTYPE_SET(Type, type)                                \
  RUNTIME_FUNCTION(Runtime_DataViewSet##Type) {                            \
    HandleScope scope(isolate);                                            \
    CHECK_RECEIVER_OBJECT("DataView.prototype.set" #Type);                 \
    Handle<Object> byte_offset = args.at<Object>(1);                       \
    Handle<Object> value = args.at<Object>(2);                             \
    Handle<Object> is_little_endian = args.at<Object>(3);                  \
    Handle<Object> result;                                                 \
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(                                    \
        isolate, result,                                                   \
        SetViewValue<type>(isolate, data_view, byte_offset,                \
                           is_little_endian->BooleanValue(isolate), value, \
                           "DataView.prototype.set" #Type));               \
    return *result;                                                        \
  }

DATA_VIEW_PROTOTYPE_SET(Int8, int8_t)
DATA_VIEW_PROTOTYPE_SET(Uint8, uint8_t)
DATA_VIEW_PROTOTYPE_SET(Int16, int16_t)
DATA_VIEW_PROTOTYPE_SET(Uint16, uint16_t)
DATA_VIEW_PROTOTYPE_SET(Int32, int32_t)
DATA_VIEW_PROTOTYPE_SET(Uint32, uint32_t)
DATA_VIEW_PROTOTYPE_SET(Float32, float)
DATA_VIEW_PROTOTYPE_SET(Float64, double)
DATA_VIEW_PROTOTYPE_SET(BigInt64, int64_t)
DATA_VIEW_PROTOTYPE_SET(BigUint64, uint64_t)
#undef DATA_VIEW_PROTOTYPE_SET

#undef CHECK_RECEIVER_OBJECT
}  // namespace internal
}  // namespace v8
