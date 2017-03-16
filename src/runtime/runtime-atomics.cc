// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/conversions-inl.h"
#include "src/factory.h"

// Implement Atomic accesses to SharedArrayBuffers as defined in the
// SharedArrayBuffer draft spec, found here
// https://github.com/tc39/ecmascript_sharedmem

namespace v8 {
namespace internal {

namespace {

#if V8_CC_GNU

template <typename T>
inline T ExchangeSeqCst(T* p, T value) {
  return __atomic_exchange_n(p, value, __ATOMIC_SEQ_CST);
}

template <typename T>
inline T CompareExchangeSeqCst(T* p, T oldval, T newval) {
  (void)__atomic_compare_exchange_n(p, &oldval, newval, 0, __ATOMIC_SEQ_CST,
                                    __ATOMIC_SEQ_CST);
  return oldval;
}

#elif V8_CC_MSVC

#define InterlockedExchange32 _InterlockedExchange
#define InterlockedCompareExchange32 _InterlockedCompareExchange
#define InterlockedCompareExchange8 _InterlockedCompareExchange8

#define ATOMIC_OPS(type, suffix, vctype)                                    \
  inline type ExchangeSeqCst(type* p, type value) {                         \
    return InterlockedExchange##suffix(reinterpret_cast<vctype*>(p),        \
                                       bit_cast<vctype>(value));            \
  }                                                                         \
  inline type CompareExchangeSeqCst(type* p, type oldval, type newval) {    \
    return InterlockedCompareExchange##suffix(reinterpret_cast<vctype*>(p), \
                                              bit_cast<vctype>(newval),     \
                                              bit_cast<vctype>(oldval));    \
  }

ATOMIC_OPS(int8_t, 8, char)
ATOMIC_OPS(uint8_t, 8, char)
ATOMIC_OPS(int16_t, 16, short)  /* NOLINT(runtime/int) */
ATOMIC_OPS(uint16_t, 16, short) /* NOLINT(runtime/int) */
ATOMIC_OPS(int32_t, 32, long)   /* NOLINT(runtime/int) */
ATOMIC_OPS(uint32_t, 32, long)  /* NOLINT(runtime/int) */

#undef ATOMIC_OPS_INTEGER
#undef ATOMIC_OPS

#undef InterlockedExchange32
#undef InterlockedCompareExchange32
#undef InterlockedCompareExchange8

#else

#error Unsupported platform!

#endif

template <typename T>
T FromObject(Handle<Object> number);

template <>
inline uint8_t FromObject<uint8_t>(Handle<Object> number) {
  return NumberToUint32(*number);
}

template <>
inline int8_t FromObject<int8_t>(Handle<Object> number) {
  return NumberToInt32(*number);
}

template <>
inline uint16_t FromObject<uint16_t>(Handle<Object> number) {
  return NumberToUint32(*number);
}

template <>
inline int16_t FromObject<int16_t>(Handle<Object> number) {
  return NumberToInt32(*number);
}

template <>
inline uint32_t FromObject<uint32_t>(Handle<Object> number) {
  return NumberToUint32(*number);
}

template <>
inline int32_t FromObject<int32_t>(Handle<Object> number) {
  return NumberToInt32(*number);
}


inline Object* ToObject(Isolate* isolate, int8_t t) { return Smi::FromInt(t); }

inline Object* ToObject(Isolate* isolate, uint8_t t) { return Smi::FromInt(t); }

inline Object* ToObject(Isolate* isolate, int16_t t) { return Smi::FromInt(t); }

inline Object* ToObject(Isolate* isolate, uint16_t t) {
  return Smi::FromInt(t);
}

inline Object* ToObject(Isolate* isolate, int32_t t) {
  return *isolate->factory()->NewNumber(t);
}

inline Object* ToObject(Isolate* isolate, uint32_t t) {
  return *isolate->factory()->NewNumber(t);
}

template <typename T>
inline Object* DoExchange(Isolate* isolate, void* buffer, size_t index,
                          Handle<Object> obj) {
  T value = FromObject<T>(obj);
  T result = ExchangeSeqCst(static_cast<T*>(buffer) + index, value);
  return ToObject(isolate, result);
}

template <typename T>
inline Object* DoCompareExchange(Isolate* isolate, void* buffer, size_t index,
                                 Handle<Object> oldobj, Handle<Object> newobj) {
  T oldval = FromObject<T>(oldobj);
  T newval = FromObject<T>(newobj);
  T result =
      CompareExchangeSeqCst(static_cast<T*>(buffer) + index, oldval, newval);
  return ToObject(isolate, result);
}

}  // anonymous namespace

// Duplicated from objects.h
// V has parameters (Type, type, TYPE, C type, element_size)
#define INTEGER_TYPED_ARRAYS(V)          \
  V(Uint8, uint8, UINT8, uint8_t, 1)     \
  V(Int8, int8, INT8, int8_t, 1)         \
  V(Uint16, uint16, UINT16, uint16_t, 2) \
  V(Int16, int16, INT16, int16_t, 2)     \
  V(Uint32, uint32, UINT32, uint32_t, 4) \
  V(Int32, int32, INT32, int32_t, 4)

RUNTIME_FUNCTION(Runtime_ThrowNotIntegerSharedTypedArrayError) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, value, 0);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate,
      NewTypeError(MessageTemplate::kNotIntegerSharedTypedArray, value));
}

RUNTIME_FUNCTION(Runtime_ThrowNotInt32SharedTypedArrayError) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, value, 0);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kNotInt32SharedTypedArray, value));
}

RUNTIME_FUNCTION(Runtime_ThrowInvalidAtomicAccessIndexError) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewRangeError(MessageTemplate::kInvalidAtomicAccessIndex));
}

RUNTIME_FUNCTION(Runtime_AtomicsExchange) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSTypedArray, sta, 0);
  CONVERT_SIZE_ARG_CHECKED(index, 1);
  CONVERT_NUMBER_ARG_HANDLE_CHECKED(value, 2);
  CHECK(sta->GetBuffer()->is_shared());
  CHECK_LT(index, NumberToSize(sta->length()));

  uint8_t* source = static_cast<uint8_t*>(sta->GetBuffer()->backing_store()) +
                    NumberToSize(sta->byte_offset());

  switch (sta->type()) {
#define TYPED_ARRAY_CASE(Type, typeName, TYPE, ctype, size) \
  case kExternal##Type##Array:                              \
    return DoExchange<ctype>(isolate, source, index, value);

    INTEGER_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

    default:
      break;
  }

  UNREACHABLE();
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_AtomicsCompareExchange) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSTypedArray, sta, 0);
  CONVERT_SIZE_ARG_CHECKED(index, 1);
  CONVERT_NUMBER_ARG_HANDLE_CHECKED(oldobj, 2);
  CONVERT_NUMBER_ARG_HANDLE_CHECKED(newobj, 3);
  CHECK(sta->GetBuffer()->is_shared());
  CHECK_LT(index, NumberToSize(sta->length()));

  uint8_t* source = static_cast<uint8_t*>(sta->GetBuffer()->backing_store()) +
                    NumberToSize(sta->byte_offset());

  switch (sta->type()) {
#define TYPED_ARRAY_CASE(Type, typeName, TYPE, ctype, size) \
  case kExternal##Type##Array:                              \
    return DoCompareExchange<ctype>(isolate, source, index, oldobj, newobj);

    INTEGER_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

    default:
      break;
  }

  UNREACHABLE();
  return isolate->heap()->undefined_value();
}

}  // namespace internal
}  // namespace v8
