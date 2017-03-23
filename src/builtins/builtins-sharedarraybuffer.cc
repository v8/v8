// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/code-factory.h"
#include "src/conversions-inl.h"
#include "src/counters.h"
#include "src/factory.h"
#include "src/futex-emulation.h"
#include "src/globals.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

// See builtins-arraybuffer.cc for implementations of
// SharedArrayBuffer.prototye.byteLength and SharedArrayBuffer.prototype.slice

inline bool AtomicIsLockFree(uint32_t size) {
  return size == 1 || size == 2 || size == 4;
}

// ES #sec-atomics.islockfree
BUILTIN(AtomicsIsLockFree) {
  HandleScope scope(isolate);
  Handle<Object> size = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, size, Object::ToNumber(size));
  return *isolate->factory()->ToBoolean(AtomicIsLockFree(size->Number()));
}

// ES #sec-validatesharedintegertypedarray
MUST_USE_RESULT MaybeHandle<JSTypedArray> ValidateSharedIntegerTypedArray(
    Isolate* isolate, Handle<Object> object, bool only_int32 = false) {
  if (object->IsJSTypedArray()) {
    Handle<JSTypedArray> typed_array = Handle<JSTypedArray>::cast(object);
    if (typed_array->GetBuffer()->is_shared()) {
      if (only_int32) {
        if (typed_array->type() == kExternalInt32Array) return typed_array;
      } else {
        if (typed_array->type() != kExternalFloat32Array &&
            typed_array->type() != kExternalFloat64Array &&
            typed_array->type() != kExternalUint8ClampedArray)
          return typed_array;
      }
    }
  }

  THROW_NEW_ERROR(
      isolate,
      NewTypeError(only_int32 ? MessageTemplate::kNotInt32SharedTypedArray
                              : MessageTemplate::kNotIntegerSharedTypedArray,
                   object),
      JSTypedArray);
}

// ES #sec-validateatomicaccess
// ValidateAtomicAccess( typedArray, requestIndex )
MUST_USE_RESULT Maybe<size_t> ValidateAtomicAccess(
    Isolate* isolate, Handle<JSTypedArray> typed_array,
    Handle<Object> request_index) {
  // TOOD(v8:5961): Use ToIndex for indexes
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, request_index,
                                   Object::ToNumber(request_index),
                                   Nothing<size_t>());
  Handle<Object> offset;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, offset,
                                   Object::ToInteger(isolate, request_index),
                                   Nothing<size_t>());
  if (!request_index->SameValue(*offset)) {
    isolate->Throw(*isolate->factory()->NewRangeError(
        MessageTemplate::kInvalidAtomicAccessIndex));
    return Nothing<size_t>();
  }
  size_t access_index;
  uint32_t length = typed_array->length_value();
  if (!TryNumberToSize(*request_index, &access_index) ||
      access_index >= length) {
    isolate->Throw(*isolate->factory()->NewRangeError(
        MessageTemplate::kInvalidAtomicAccessIndex));
    return Nothing<size_t>();
  }
  return Just<size_t>(access_index);
}

// ES #sec-atomics.wake
// Atomics.wake( typedArray, index, count )
BUILTIN(AtomicsWake) {
  HandleScope scope(isolate);
  Handle<Object> array = args.atOrUndefined(isolate, 1);
  Handle<Object> index = args.atOrUndefined(isolate, 2);
  Handle<Object> count = args.atOrUndefined(isolate, 3);

  Handle<JSTypedArray> sta;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, sta, ValidateSharedIntegerTypedArray(isolate, array, true));

  Maybe<size_t> maybe_index = ValidateAtomicAccess(isolate, sta, index);
  if (maybe_index.IsNothing()) return isolate->heap()->exception();
  size_t i = maybe_index.FromJust();

  uint32_t c;
  if (count->IsUndefined(isolate)) {
    c = kMaxUInt32;
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, count,
                                       Object::ToInteger(isolate, count));
    double count_double = count->Number();
    if (count_double < 0)
      count_double = 0;
    else if (count_double > kMaxUInt32)
      count_double = kMaxUInt32;
    c = static_cast<uint32_t>(count_double);
  }

  Handle<JSArrayBuffer> array_buffer = sta->GetBuffer();
  size_t addr = (i << 2) + NumberToSize(sta->byte_offset());

  return FutexEmulation::Wake(isolate, array_buffer, addr, c);
}

// ES #sec-atomics.wait
// Atomics.wait( typedArray, index, value, timeout )
BUILTIN(AtomicsWait) {
  HandleScope scope(isolate);
  Handle<Object> array = args.atOrUndefined(isolate, 1);
  Handle<Object> index = args.atOrUndefined(isolate, 2);
  Handle<Object> value = args.atOrUndefined(isolate, 3);
  Handle<Object> timeout = args.atOrUndefined(isolate, 4);

  Handle<JSTypedArray> sta;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, sta, ValidateSharedIntegerTypedArray(isolate, array, true));

  Maybe<size_t> maybe_index = ValidateAtomicAccess(isolate, sta, index);
  if (maybe_index.IsNothing()) return isolate->heap()->exception();
  size_t i = maybe_index.FromJust();

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value,
                                     Object::ToInt32(isolate, value));
  int32_t value_int32 = NumberToInt32(*value);

  double timeout_number;
  if (timeout->IsUndefined(isolate)) {
    timeout_number = isolate->heap()->infinity_value()->Number();
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, timeout,
                                       Object::ToNumber(timeout));
    timeout_number = timeout->Number();
    if (std::isnan(timeout_number))
      timeout_number = isolate->heap()->infinity_value()->Number();
    else if (timeout_number < 0)
      timeout_number = 0;
  }

  if (!isolate->allow_atomics_wait()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kAtomicsWaitNotAllowed));
  }

  Handle<JSArrayBuffer> array_buffer = sta->GetBuffer();
  size_t addr = (i << 2) + NumberToSize(sta->byte_offset());

  return FutexEmulation::Wait(isolate, array_buffer, addr, value_int32,
                              timeout_number);
}

namespace {

#if V8_CC_GNU

template <typename T>
inline T AddSeqCst(T* p, T value) {
  return __atomic_fetch_add(p, value, __ATOMIC_SEQ_CST);
}

template <typename T>
inline T SubSeqCst(T* p, T value) {
  return __atomic_fetch_sub(p, value, __ATOMIC_SEQ_CST);
}

template <typename T>
inline T AndSeqCst(T* p, T value) {
  return __atomic_fetch_and(p, value, __ATOMIC_SEQ_CST);
}

template <typename T>
inline T OrSeqCst(T* p, T value) {
  return __atomic_fetch_or(p, value, __ATOMIC_SEQ_CST);
}

template <typename T>
inline T XorSeqCst(T* p, T value) {
  return __atomic_fetch_xor(p, value, __ATOMIC_SEQ_CST);
}

#elif V8_CC_MSVC

#define InterlockedExchangeAdd32 _InterlockedExchangeAdd
#define InterlockedAnd32 _InterlockedAnd
#define InterlockedOr32 _InterlockedOr
#define InterlockedXor32 _InterlockedXor
#define InterlockedExchangeAdd16 _InterlockedExchangeAdd16
#define InterlockedExchangeAdd8 _InterlockedExchangeAdd8

#define ATOMIC_OPS(type, suffix, vctype)                                    \
  inline type AddSeqCst(type* p, type value) {                              \
    return InterlockedExchangeAdd##suffix(reinterpret_cast<vctype*>(p),     \
                                          bit_cast<vctype>(value));         \
  }                                                                         \
  inline type SubSeqCst(type* p, type value) {                              \
    return InterlockedExchangeAdd##suffix(reinterpret_cast<vctype*>(p),     \
                                          -bit_cast<vctype>(value));        \
  }                                                                         \
  inline type AndSeqCst(type* p, type value) {                              \
    return InterlockedAnd##suffix(reinterpret_cast<vctype*>(p),             \
                                  bit_cast<vctype>(value));                 \
  }                                                                         \
  inline type OrSeqCst(type* p, type value) {                               \
    return InterlockedOr##suffix(reinterpret_cast<vctype*>(p),              \
                                 bit_cast<vctype>(value));                  \
  }                                                                         \
  inline type XorSeqCst(type* p, type value) {                              \
    return InterlockedXor##suffix(reinterpret_cast<vctype*>(p),             \
                                  bit_cast<vctype>(value));                 \
  }

ATOMIC_OPS(int8_t, 8, char)
ATOMIC_OPS(uint8_t, 8, char)
ATOMIC_OPS(int16_t, 16, short)  /* NOLINT(runtime/int) */
ATOMIC_OPS(uint16_t, 16, short) /* NOLINT(runtime/int) */
ATOMIC_OPS(int32_t, 32, long)   /* NOLINT(runtime/int) */
ATOMIC_OPS(uint32_t, 32, long)  /* NOLINT(runtime/int) */

#undef ATOMIC_OPS_INTEGER
#undef ATOMIC_OPS

#undef InterlockedExchangeAdd32
#undef InterlockedAnd32
#undef InterlockedOr32
#undef InterlockedXor32
#undef InterlockedExchangeAdd16
#undef InterlockedExchangeAdd8

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
inline Object* DoAdd(Isolate* isolate, void* buffer, size_t index,
                     Handle<Object> obj) {
  T value = FromObject<T>(obj);
  T result = AddSeqCst(static_cast<T*>(buffer) + index, value);
  return ToObject(isolate, result);
}

template <typename T>
inline Object* DoSub(Isolate* isolate, void* buffer, size_t index,
                     Handle<Object> obj) {
  T value = FromObject<T>(obj);
  T result = SubSeqCst(static_cast<T*>(buffer) + index, value);
  return ToObject(isolate, result);
}

template <typename T>
inline Object* DoAnd(Isolate* isolate, void* buffer, size_t index,
                     Handle<Object> obj) {
  T value = FromObject<T>(obj);
  T result = AndSeqCst(static_cast<T*>(buffer) + index, value);
  return ToObject(isolate, result);
}

template <typename T>
inline Object* DoOr(Isolate* isolate, void* buffer, size_t index,
                    Handle<Object> obj) {
  T value = FromObject<T>(obj);
  T result = OrSeqCst(static_cast<T*>(buffer) + index, value);
  return ToObject(isolate, result);
}

template <typename T>
inline Object* DoXor(Isolate* isolate, void* buffer, size_t index,
                     Handle<Object> obj) {
  T value = FromObject<T>(obj);
  T result = XorSeqCst(static_cast<T*>(buffer) + index, value);
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

// ES #sec-atomics.add
// Atomics.add( typedArray, index, value )
BUILTIN(AtomicsAdd) {
  HandleScope scope(isolate);
  Handle<Object> array = args.atOrUndefined(isolate, 1);
  Handle<Object> index = args.atOrUndefined(isolate, 2);
  Handle<Object> value = args.atOrUndefined(isolate, 3);

  Handle<JSTypedArray> sta;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, sta, ValidateSharedIntegerTypedArray(isolate, array));

  Maybe<size_t> maybe_index = ValidateAtomicAccess(isolate, sta, index);
  if (maybe_index.IsNothing()) return isolate->heap()->exception();
  size_t i = maybe_index.FromJust();

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value,
                                     Object::ToInteger(isolate, value));

  uint8_t* source = static_cast<uint8_t*>(sta->GetBuffer()->backing_store()) +
                    NumberToSize(sta->byte_offset());

  switch (sta->type()) {
#define TYPED_ARRAY_CASE(Type, typeName, TYPE, ctype, size) \
  case kExternal##Type##Array:                              \
    return DoAdd<ctype>(isolate, source, i, value);

    INTEGER_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

    default:
      break;
  }

  UNREACHABLE();
  return isolate->heap()->undefined_value();
}

// ES #sec-atomics.sub
// Atomics.sub( typedArray, index, value )
BUILTIN(AtomicsSub) {
  HandleScope scope(isolate);
  Handle<Object> array = args.atOrUndefined(isolate, 1);
  Handle<Object> index = args.atOrUndefined(isolate, 2);
  Handle<Object> value = args.atOrUndefined(isolate, 3);

  Handle<JSTypedArray> sta;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, sta, ValidateSharedIntegerTypedArray(isolate, array));

  Maybe<size_t> maybe_index = ValidateAtomicAccess(isolate, sta, index);
  if (maybe_index.IsNothing()) return isolate->heap()->exception();
  size_t i = maybe_index.FromJust();

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value,
                                     Object::ToInteger(isolate, value));

  uint8_t* source = static_cast<uint8_t*>(sta->GetBuffer()->backing_store()) +
                    NumberToSize(sta->byte_offset());

  switch (sta->type()) {
#define TYPED_ARRAY_CASE(Type, typeName, TYPE, ctype, size) \
  case kExternal##Type##Array:                              \
    return DoSub<ctype>(isolate, source, i, value);

    INTEGER_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

    default:
      break;
  }

  UNREACHABLE();
  return isolate->heap()->undefined_value();
}

// ES #sec-atomics.and
// Atomics.and( typedArray, index, value )
BUILTIN(AtomicsAnd) {
  HandleScope scope(isolate);
  Handle<Object> array = args.atOrUndefined(isolate, 1);
  Handle<Object> index = args.atOrUndefined(isolate, 2);
  Handle<Object> value = args.atOrUndefined(isolate, 3);

  Handle<JSTypedArray> sta;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, sta, ValidateSharedIntegerTypedArray(isolate, array));

  Maybe<size_t> maybe_index = ValidateAtomicAccess(isolate, sta, index);
  if (maybe_index.IsNothing()) return isolate->heap()->exception();
  size_t i = maybe_index.FromJust();

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value,
                                     Object::ToInteger(isolate, value));

  uint8_t* source = static_cast<uint8_t*>(sta->GetBuffer()->backing_store()) +
                    NumberToSize(sta->byte_offset());

  switch (sta->type()) {
#define TYPED_ARRAY_CASE(Type, typeName, TYPE, ctype, size) \
  case kExternal##Type##Array:                              \
    return DoAnd<ctype>(isolate, source, i, value);

    INTEGER_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

    default:
      break;
  }

  UNREACHABLE();
  return isolate->heap()->undefined_value();
}

// ES #sec-atomics.or
// Atomics.or( typedArray, index, value )
BUILTIN(AtomicsOr) {
  HandleScope scope(isolate);
  Handle<Object> array = args.atOrUndefined(isolate, 1);
  Handle<Object> index = args.atOrUndefined(isolate, 2);
  Handle<Object> value = args.atOrUndefined(isolate, 3);

  Handle<JSTypedArray> sta;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, sta, ValidateSharedIntegerTypedArray(isolate, array));

  Maybe<size_t> maybe_index = ValidateAtomicAccess(isolate, sta, index);
  if (maybe_index.IsNothing()) return isolate->heap()->exception();
  size_t i = maybe_index.FromJust();

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value,
                                     Object::ToInteger(isolate, value));

  uint8_t* source = static_cast<uint8_t*>(sta->GetBuffer()->backing_store()) +
                    NumberToSize(sta->byte_offset());

  switch (sta->type()) {
#define TYPED_ARRAY_CASE(Type, typeName, TYPE, ctype, size) \
  case kExternal##Type##Array:                              \
    return DoOr<ctype>(isolate, source, i, value);

    INTEGER_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

    default:
      break;
  }

  UNREACHABLE();
  return isolate->heap()->undefined_value();
}

// ES #sec-atomics.xor
// Atomics.xor( typedArray, index, value )
BUILTIN(AtomicsXor) {
  HandleScope scope(isolate);
  Handle<Object> array = args.atOrUndefined(isolate, 1);
  Handle<Object> index = args.atOrUndefined(isolate, 2);
  Handle<Object> value = args.atOrUndefined(isolate, 3);

  Handle<JSTypedArray> sta;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, sta, ValidateSharedIntegerTypedArray(isolate, array));

  Maybe<size_t> maybe_index = ValidateAtomicAccess(isolate, sta, index);
  if (maybe_index.IsNothing()) return isolate->heap()->exception();
  size_t i = maybe_index.FromJust();

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value,
                                     Object::ToInteger(isolate, value));

  uint8_t* source = static_cast<uint8_t*>(sta->GetBuffer()->backing_store()) +
                    NumberToSize(sta->byte_offset());

  switch (sta->type()) {
#define TYPED_ARRAY_CASE(Type, typeName, TYPE, ctype, size) \
  case kExternal##Type##Array:                              \
    return DoXor<ctype>(isolate, source, i, value);

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
