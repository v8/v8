// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/code-factory.h"
#include "src/code-stub-assembler.h"
#include "src/conversions-inl.h"
#include "src/counters.h"
#include "src/factory.h"
#include "src/futex-emulation.h"
#include "src/globals.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

using compiler::Node;

class SharedArrayBufferBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit SharedArrayBufferBuiltinsAssembler(
      compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  void ValidateSharedTypedArray(Node* tagged, Node* context,
                                Node** out_instance_type,
                                Node** out_backing_store);
  Node* ConvertTaggedAtomicIndexToWord32(Node* tagged, Node* context,
                                         Node** number_index);
  void ValidateAtomicIndex(Node* index_word, Node* array_length_word,
                           Node* context);
};

// ES7 sharedmem 6.3.4.1 get SharedArrayBuffer.prototype.byteLength
BUILTIN(SharedArrayBufferPrototypeGetByteLength) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSArrayBuffer, array_buffer,
                 "get SharedArrayBuffer.prototype.byteLength");
  if (!array_buffer->is_shared()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "get SharedArrayBuffer.prototype.byteLength"),
                              args.receiver()));
  }
  return array_buffer->byte_length();
}

void SharedArrayBufferBuiltinsAssembler::ValidateSharedTypedArray(
    Node* tagged, Node* context, Node** out_instance_type,
    Node** out_backing_store) {
  Label not_float_or_clamped(this), invalid(this);

  // Fail if it is not a heap object.
  GotoIf(TaggedIsSmi(tagged), &invalid);

  // Fail if the array's instance type is not JSTypedArray.
  GotoIf(Word32NotEqual(LoadInstanceType(tagged),
                        Int32Constant(JS_TYPED_ARRAY_TYPE)),
         &invalid);

  // Fail if the array's JSArrayBuffer is not shared.
  Node* array_buffer = LoadObjectField(tagged, JSTypedArray::kBufferOffset);
  Node* bitfield = LoadObjectField(array_buffer, JSArrayBuffer::kBitFieldOffset,
                                   MachineType::Uint32());
  GotoIfNot(IsSetWord32<JSArrayBuffer::IsShared>(bitfield), &invalid);

  // Fail if the array's element type is float32, float64 or clamped.
  Node* elements_instance_type =
      LoadInstanceType(LoadObjectField(tagged, JSObject::kElementsOffset));
  STATIC_ASSERT(FIXED_INT8_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_INT16_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_INT32_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_UINT8_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_UINT16_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_UINT32_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  Branch(Int32LessThan(elements_instance_type,
                       Int32Constant(FIXED_FLOAT32_ARRAY_TYPE)),
         &not_float_or_clamped, &invalid);

  Bind(&invalid);
  {
    CallRuntime(Runtime::kThrowNotIntegerSharedTypedArrayError, context,
                tagged);
    Unreachable();
  }

  Bind(&not_float_or_clamped);
  *out_instance_type = elements_instance_type;

  Node* backing_store =
      LoadObjectField(array_buffer, JSArrayBuffer::kBackingStoreOffset);
  Node* byte_offset = ChangeUint32ToWord(TruncateTaggedToWord32(
      context, LoadObjectField(tagged, JSArrayBufferView::kByteOffsetOffset)));
  *out_backing_store =
      IntPtrAdd(BitcastTaggedToWord(backing_store), byte_offset);
}

// https://tc39.github.io/ecmascript_sharedmem/shmem.html#Atomics.ValidateAtomicAccess
Node* SharedArrayBufferBuiltinsAssembler::ConvertTaggedAtomicIndexToWord32(
    Node* tagged, Node* context, Node** number_index) {
  Variable var_result(this, MachineRepresentation::kWord32);

  // TODO(jkummerow): Skip ToNumber call when |tagged| is a number already.
  // Maybe this can be unified with other tagged-to-index conversions?
  // Why does this return an int32, and not an intptr?
  // Why is there the additional |number_index| output parameter?
  Callable to_number = CodeFactory::ToNumber(isolate());
  *number_index = CallStub(to_number, context, tagged);
  Label done(this, &var_result);

  Label if_numberissmi(this), if_numberisnotsmi(this);
  Branch(TaggedIsSmi(*number_index), &if_numberissmi, &if_numberisnotsmi);

  Bind(&if_numberissmi);
  {
    var_result.Bind(SmiToWord32(*number_index));
    Goto(&done);
  }

  Bind(&if_numberisnotsmi);
  {
    Node* number_index_value = LoadHeapNumberValue(*number_index);
    Node* access_index = TruncateFloat64ToWord32(number_index_value);
    Node* test_index = ChangeInt32ToFloat64(access_index);

    Label if_indexesareequal(this), if_indexesarenotequal(this);
    Branch(Float64Equal(number_index_value, test_index), &if_indexesareequal,
           &if_indexesarenotequal);

    Bind(&if_indexesareequal);
    {
      var_result.Bind(access_index);
      Goto(&done);
    }

    Bind(&if_indexesarenotequal);
    {
      CallRuntime(Runtime::kThrowInvalidAtomicAccessIndexError, context);
      Unreachable();
    }
  }

  Bind(&done);
  return var_result.value();
}

void SharedArrayBufferBuiltinsAssembler::ValidateAtomicIndex(
    Node* index_word, Node* array_length_word, Node* context) {
  // Check if the index is in bounds. If not, throw RangeError.
  Label check_passed(this);
  GotoIf(Uint32LessThan(index_word, array_length_word), &check_passed);

  CallRuntime(Runtime::kThrowInvalidAtomicAccessIndexError, context);
  Unreachable();

  Bind(&check_passed);
}

TF_BUILTIN(AtomicsLoad, SharedArrayBufferBuiltinsAssembler) {
  Node* array = Parameter(1);
  Node* index = Parameter(2);
  Node* context = Parameter(3 + 2);

  Node* instance_type;
  Node* backing_store;
  ValidateSharedTypedArray(array, context, &instance_type, &backing_store);

  Node* index_integer;
  Node* index_word32 =
      ConvertTaggedAtomicIndexToWord32(index, context, &index_integer);
  Node* array_length_word32 = TruncateTaggedToWord32(
      context, LoadObjectField(array, JSTypedArray::kLengthOffset));
  ValidateAtomicIndex(index_word32, array_length_word32, context);
  Node* index_word = ChangeUint32ToWord(index_word32);

  Label i8(this), u8(this), i16(this), u16(this), i32(this), u32(this),
      other(this);
  int32_t case_values[] = {
      FIXED_INT8_ARRAY_TYPE,   FIXED_UINT8_ARRAY_TYPE, FIXED_INT16_ARRAY_TYPE,
      FIXED_UINT16_ARRAY_TYPE, FIXED_INT32_ARRAY_TYPE, FIXED_UINT32_ARRAY_TYPE,
  };
  Label* case_labels[] = {
      &i8, &u8, &i16, &u16, &i32, &u32,
  };
  Switch(instance_type, &other, case_values, case_labels,
         arraysize(case_labels));

  Bind(&i8);
  Return(SmiFromWord32(
      AtomicLoad(MachineType::Int8(), backing_store, index_word)));

  Bind(&u8);
  Return(SmiFromWord32(
      AtomicLoad(MachineType::Uint8(), backing_store, index_word)));

  Bind(&i16);
  Return(SmiFromWord32(
      AtomicLoad(MachineType::Int16(), backing_store, WordShl(index_word, 1))));

  Bind(&u16);
  Return(SmiFromWord32(AtomicLoad(MachineType::Uint16(), backing_store,
                                  WordShl(index_word, 1))));

  Bind(&i32);
  Return(ChangeInt32ToTagged(
      AtomicLoad(MachineType::Int32(), backing_store, WordShl(index_word, 2))));

  Bind(&u32);
  Return(ChangeUint32ToTagged(AtomicLoad(MachineType::Uint32(), backing_store,
                                         WordShl(index_word, 2))));

  // This shouldn't happen, we've already validated the type.
  Bind(&other);
  Unreachable();
}

TF_BUILTIN(AtomicsStore, SharedArrayBufferBuiltinsAssembler) {
  Node* array = Parameter(1);
  Node* index = Parameter(2);
  Node* value = Parameter(3);
  Node* context = Parameter(4 + 2);

  Node* instance_type;
  Node* backing_store;
  ValidateSharedTypedArray(array, context, &instance_type, &backing_store);

  Node* index_integer;
  Node* index_word32 =
      ConvertTaggedAtomicIndexToWord32(index, context, &index_integer);
  Node* array_length_word32 = TruncateTaggedToWord32(
      context, LoadObjectField(array, JSTypedArray::kLengthOffset));
  ValidateAtomicIndex(index_word32, array_length_word32, context);
  Node* index_word = ChangeUint32ToWord(index_word32);

  Node* value_integer = ToInteger(context, value);
  Node* value_word32 = TruncateTaggedToWord32(context, value_integer);

  Label u8(this), u16(this), u32(this), other(this);
  int32_t case_values[] = {
      FIXED_INT8_ARRAY_TYPE,   FIXED_UINT8_ARRAY_TYPE, FIXED_INT16_ARRAY_TYPE,
      FIXED_UINT16_ARRAY_TYPE, FIXED_INT32_ARRAY_TYPE, FIXED_UINT32_ARRAY_TYPE,
  };
  Label* case_labels[] = {
      &u8, &u8, &u16, &u16, &u32, &u32,
  };
  Switch(instance_type, &other, case_values, case_labels,
         arraysize(case_labels));

  Bind(&u8);
  AtomicStore(MachineRepresentation::kWord8, backing_store, index_word,
              value_word32);
  Return(value_integer);

  Bind(&u16);
  AtomicStore(MachineRepresentation::kWord16, backing_store,
              WordShl(index_word, 1), value_word32);
  Return(value_integer);

  Bind(&u32);
  AtomicStore(MachineRepresentation::kWord32, backing_store,
              WordShl(index_word, 2), value_word32);
  Return(value_integer);

  // This shouldn't happen, we've already validated the type.
  Bind(&other);
  Unreachable();
}

TF_BUILTIN(AtomicsExchange, SharedArrayBufferBuiltinsAssembler) {
  Node* array = Parameter(1);
  Node* index = Parameter(2);
  Node* value = Parameter(3);
  Node* context = Parameter(4 + 2);

  Node* instance_type;
  Node* backing_store;
  ValidateSharedTypedArray(array, context, &instance_type, &backing_store);

  Node* index_integer;
  Node* index_word32 =
      ConvertTaggedAtomicIndexToWord32(index, context, &index_integer);
  Node* array_length_word32 = TruncateTaggedToWord32(
      context, LoadObjectField(array, JSTypedArray::kLengthOffset));
  ValidateAtomicIndex(index_word32, array_length_word32, context);

  Node* value_integer = ToInteger(context, value);

#if V8_TARGET_ARCH_MIPS || V8_TARGET_ARCH_MIPS64 || V8_TARGET_ARCH_PPC64 || \
    V8_TARGET_ARCH_PPC
  Return(CallRuntime(Runtime::kAtomicsExchange, context, array, index_integer,
                     value_integer));
#else
  Node* index_word = ChangeUint32ToWord(index_word32);

  Node* value_word32 = TruncateTaggedToWord32(context, value_integer);

  Label i8(this), u8(this), i16(this), u16(this), i32(this), u32(this),
      other(this);
  int32_t case_values[] = {
      FIXED_INT8_ARRAY_TYPE,   FIXED_UINT8_ARRAY_TYPE, FIXED_INT16_ARRAY_TYPE,
      FIXED_UINT16_ARRAY_TYPE, FIXED_INT32_ARRAY_TYPE, FIXED_UINT32_ARRAY_TYPE,
  };
  Label* case_labels[] = {
      &i8, &u8, &i16, &u16, &i32, &u32,
  };
  Switch(instance_type, &other, case_values, case_labels,
         arraysize(case_labels));

  Bind(&i8);
  Return(SmiFromWord32(AtomicExchange(MachineType::Int8(), backing_store,
                                      index_word, value_word32)));

  Bind(&u8);
  Return(SmiFromWord32(AtomicExchange(MachineType::Uint8(), backing_store,
                                      index_word, value_word32)));

  Bind(&i16);
  Return(SmiFromWord32(AtomicExchange(MachineType::Int16(), backing_store,
                                      WordShl(index_word, 1), value_word32)));

  Bind(&u16);
  Return(SmiFromWord32(AtomicExchange(MachineType::Uint16(), backing_store,
                                      WordShl(index_word, 1), value_word32)));

  Bind(&i32);
  Return(ChangeInt32ToTagged(AtomicExchange(MachineType::Int32(), backing_store,
                                            WordShl(index_word, 2),
                                            value_word32)));

  Bind(&u32);
  Return(ChangeUint32ToTagged(
      AtomicExchange(MachineType::Uint32(), backing_store,
                     WordShl(index_word, 2), value_word32)));

  // This shouldn't happen, we've already validated the type.
  Bind(&other);
  Unreachable();
#endif  // V8_TARGET_ARCH_MIPS || V8_TARGET_ARCH_MIPS64 || V8_TARGET_ARCH_PPC64
        // || V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_S390 || V8_TARGET_ARCH_S390X
}

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
inline T CompareExchangeSeqCst(T* p, T oldval, T newval) {
  (void)__atomic_compare_exchange_n(p, &oldval, newval, 0, __ATOMIC_SEQ_CST,
                                    __ATOMIC_SEQ_CST);
  return oldval;
}

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

#define InterlockedCompareExchange32 _InterlockedCompareExchange
#define InterlockedExchange32 _InterlockedExchange
#define InterlockedExchangeAdd32 _InterlockedExchangeAdd
#define InterlockedAnd32 _InterlockedAnd
#define InterlockedOr32 _InterlockedOr
#define InterlockedXor32 _InterlockedXor
#define InterlockedExchangeAdd16 _InterlockedExchangeAdd16
#define InterlockedCompareExchange8 _InterlockedCompareExchange8
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

#undef InterlockedCompareExchange32
#undef InterlockedExchange32
#undef InterlockedExchangeAdd32
#undef InterlockedAnd32
#undef InterlockedOr32
#undef InterlockedXor32
#undef InterlockedExchangeAdd16
#undef InterlockedCompareExchange8
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
inline Object* DoCompareExchange(Isolate* isolate, void* buffer, size_t index,
                                 Handle<Object> oldobj, Handle<Object> newobj) {
  T oldval = FromObject<T>(oldobj);
  T newval = FromObject<T>(newobj);
  T result =
      CompareExchangeSeqCst(static_cast<T*>(buffer) + index, oldval, newval);
  return ToObject(isolate, result);
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

// ES #sec-atomics.wait
// Atomics.compareExchange( typedArray, index, expectedValue, replacementValue )
BUILTIN(AtomicsCompareExchange) {
  HandleScope scope(isolate);
  Handle<Object> array = args.atOrUndefined(isolate, 1);
  Handle<Object> index = args.atOrUndefined(isolate, 2);
  Handle<Object> expected_value = args.atOrUndefined(isolate, 3);
  Handle<Object> replacement_value = args.atOrUndefined(isolate, 4);

  Handle<JSTypedArray> sta;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, sta, ValidateSharedIntegerTypedArray(isolate, array));

  Maybe<size_t> maybe_index = ValidateAtomicAccess(isolate, sta, index);
  if (maybe_index.IsNothing()) return isolate->heap()->exception();
  size_t i = maybe_index.FromJust();

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, expected_value, Object::ToInteger(isolate, expected_value));

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, replacement_value,
      Object::ToInteger(isolate, replacement_value));

  uint8_t* source = static_cast<uint8_t*>(sta->GetBuffer()->backing_store()) +
                    NumberToSize(sta->byte_offset());

  switch (sta->type()) {
#define TYPED_ARRAY_CASE(Type, typeName, TYPE, ctype, size)             \
  case kExternal##Type##Array:                                          \
    return DoCompareExchange<ctype>(isolate, source, i, expected_value, \
                                    replacement_value);

    INTEGER_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

    default:
      break;
  }

  UNREACHABLE();
  return isolate->heap()->undefined_value();
}

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
