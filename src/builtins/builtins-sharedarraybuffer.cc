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
#include "src/factory.h"
#include "src/futex-emulation.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

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

namespace {

void ValidateSharedTypedArray(CodeStubAssembler* a, compiler::Node* tagged,
                              compiler::Node* context,
                              compiler::Node** out_instance_type,
                              compiler::Node** out_backing_store) {
  using compiler::Node;
  CodeStubAssembler::Label is_smi(a), not_smi(a), is_typed_array(a),
      not_typed_array(a), is_shared(a), not_shared(a), is_float_or_clamped(a),
      not_float_or_clamped(a), invalid(a);

  // Fail if it is not a heap object.
  a->Branch(a->TaggedIsSmi(tagged), &is_smi, &not_smi);
  a->Bind(&is_smi);
  a->Goto(&invalid);

  // Fail if the array's instance type is not JSTypedArray.
  a->Bind(&not_smi);
  a->Branch(a->Word32Equal(a->LoadInstanceType(tagged),
                           a->Int32Constant(JS_TYPED_ARRAY_TYPE)),
            &is_typed_array, &not_typed_array);
  a->Bind(&not_typed_array);
  a->Goto(&invalid);

  // Fail if the array's JSArrayBuffer is not shared.
  a->Bind(&is_typed_array);
  Node* array_buffer = a->LoadObjectField(tagged, JSTypedArray::kBufferOffset);
  Node* is_buffer_shared =
      a->IsSetWord32<JSArrayBuffer::IsShared>(a->LoadObjectField(
          array_buffer, JSArrayBuffer::kBitFieldOffset, MachineType::Uint32()));
  a->Branch(is_buffer_shared, &is_shared, &not_shared);
  a->Bind(&not_shared);
  a->Goto(&invalid);

  // Fail if the array's element type is float32, float64 or clamped.
  a->Bind(&is_shared);
  Node* elements_instance_type = a->LoadInstanceType(
      a->LoadObjectField(tagged, JSObject::kElementsOffset));
  STATIC_ASSERT(FIXED_INT8_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_INT16_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_INT32_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_UINT8_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_UINT16_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_UINT32_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  a->Branch(a->Int32LessThan(elements_instance_type,
                             a->Int32Constant(FIXED_FLOAT32_ARRAY_TYPE)),
            &not_float_or_clamped, &is_float_or_clamped);
  a->Bind(&is_float_or_clamped);
  a->Goto(&invalid);

  a->Bind(&invalid);
  a->CallRuntime(Runtime::kThrowNotIntegerSharedTypedArrayError, context,
                 tagged);
  a->Return(a->UndefinedConstant());

  a->Bind(&not_float_or_clamped);
  *out_instance_type = elements_instance_type;

  Node* backing_store =
      a->LoadObjectField(array_buffer, JSArrayBuffer::kBackingStoreOffset);
  Node* byte_offset = a->ChangeUint32ToWord(a->TruncateTaggedToWord32(
      context,
      a->LoadObjectField(tagged, JSArrayBufferView::kByteOffsetOffset)));
  *out_backing_store =
      a->IntPtrAdd(a->BitcastTaggedToWord(backing_store), byte_offset);
}

// https://tc39.github.io/ecmascript_sharedmem/shmem.html#Atomics.ValidateAtomicAccess
compiler::Node* ConvertTaggedAtomicIndexToWord32(CodeStubAssembler* a,
                                                 compiler::Node* tagged,
                                                 compiler::Node* context) {
  using compiler::Node;
  CodeStubAssembler::Variable var_result(a, MachineRepresentation::kWord32);

  Callable to_number = CodeFactory::ToNumber(a->isolate());
  Node* number_index = a->CallStub(to_number, context, tagged);
  CodeStubAssembler::Label done(a, &var_result);

  CodeStubAssembler::Label if_numberissmi(a), if_numberisnotsmi(a);
  a->Branch(a->TaggedIsSmi(number_index), &if_numberissmi, &if_numberisnotsmi);

  a->Bind(&if_numberissmi);
  {
    var_result.Bind(a->SmiToWord32(number_index));
    a->Goto(&done);
  }

  a->Bind(&if_numberisnotsmi);
  {
    Node* number_index_value = a->LoadHeapNumberValue(number_index);
    Node* access_index = a->TruncateFloat64ToWord32(number_index_value);
    Node* test_index = a->ChangeInt32ToFloat64(access_index);

    CodeStubAssembler::Label if_indexesareequal(a), if_indexesarenotequal(a);
    a->Branch(a->Float64Equal(number_index_value, test_index),
              &if_indexesareequal, &if_indexesarenotequal);

    a->Bind(&if_indexesareequal);
    {
      var_result.Bind(access_index);
      a->Goto(&done);
    }

    a->Bind(&if_indexesarenotequal);
    a->Return(
        a->CallRuntime(Runtime::kThrowInvalidAtomicAccessIndexError, context));
  }

  a->Bind(&done);
  return var_result.value();
}

void ValidateAtomicIndex(CodeStubAssembler* a, compiler::Node* index_word,
                         compiler::Node* array_length_word,
                         compiler::Node* context) {
  using compiler::Node;
  // Check if the index is in bounds. If not, throw RangeError.
  CodeStubAssembler::Label if_inbounds(a), if_notinbounds(a);
  // TODO(jkummerow): Use unsigned comparison instead of "i<0 || i>length".
  a->Branch(
      a->Word32Or(a->Int32LessThan(index_word, a->Int32Constant(0)),
                  a->Int32GreaterThanOrEqual(index_word, array_length_word)),
      &if_notinbounds, &if_inbounds);
  a->Bind(&if_notinbounds);
  a->Return(
      a->CallRuntime(Runtime::kThrowInvalidAtomicAccessIndexError, context));
  a->Bind(&if_inbounds);
}

}  // anonymous namespace

void Builtins::Generate_AtomicsLoad(compiler::CodeAssemblerState* state) {
  using compiler::Node;
  CodeStubAssembler a(state);
  Node* array = a.Parameter(1);
  Node* index = a.Parameter(2);
  Node* context = a.Parameter(3 + 2);

  Node* instance_type;
  Node* backing_store;
  ValidateSharedTypedArray(&a, array, context, &instance_type, &backing_store);

  Node* index_word32 = ConvertTaggedAtomicIndexToWord32(&a, index, context);
  Node* array_length_word32 = a.TruncateTaggedToWord32(
      context, a.LoadObjectField(array, JSTypedArray::kLengthOffset));
  ValidateAtomicIndex(&a, index_word32, array_length_word32, context);
  Node* index_word = a.ChangeUint32ToWord(index_word32);

  CodeStubAssembler::Label i8(&a), u8(&a), i16(&a), u16(&a), i32(&a), u32(&a),
      other(&a);
  int32_t case_values[] = {
      FIXED_INT8_ARRAY_TYPE,   FIXED_UINT8_ARRAY_TYPE, FIXED_INT16_ARRAY_TYPE,
      FIXED_UINT16_ARRAY_TYPE, FIXED_INT32_ARRAY_TYPE, FIXED_UINT32_ARRAY_TYPE,
  };
  CodeStubAssembler::Label* case_labels[] = {
      &i8, &u8, &i16, &u16, &i32, &u32,
  };
  a.Switch(instance_type, &other, case_values, case_labels,
           arraysize(case_labels));

  a.Bind(&i8);
  a.Return(a.SmiFromWord32(
      a.AtomicLoad(MachineType::Int8(), backing_store, index_word)));

  a.Bind(&u8);
  a.Return(a.SmiFromWord32(
      a.AtomicLoad(MachineType::Uint8(), backing_store, index_word)));

  a.Bind(&i16);
  a.Return(a.SmiFromWord32(a.AtomicLoad(MachineType::Int16(), backing_store,
                                        a.WordShl(index_word, 1))));

  a.Bind(&u16);
  a.Return(a.SmiFromWord32(a.AtomicLoad(MachineType::Uint16(), backing_store,
                                        a.WordShl(index_word, 1))));

  a.Bind(&i32);
  a.Return(a.ChangeInt32ToTagged(a.AtomicLoad(
      MachineType::Int32(), backing_store, a.WordShl(index_word, 2))));

  a.Bind(&u32);
  a.Return(a.ChangeUint32ToTagged(a.AtomicLoad(
      MachineType::Uint32(), backing_store, a.WordShl(index_word, 2))));

  // This shouldn't happen, we've already validated the type.
  a.Bind(&other);
  a.Return(a.SmiConstant(0));
}

void Builtins::Generate_AtomicsStore(compiler::CodeAssemblerState* state) {
  using compiler::Node;
  CodeStubAssembler a(state);
  Node* array = a.Parameter(1);
  Node* index = a.Parameter(2);
  Node* value = a.Parameter(3);
  Node* context = a.Parameter(4 + 2);

  Node* instance_type;
  Node* backing_store;
  ValidateSharedTypedArray(&a, array, context, &instance_type, &backing_store);

  Node* index_word32 = ConvertTaggedAtomicIndexToWord32(&a, index, context);
  Node* array_length_word32 = a.TruncateTaggedToWord32(
      context, a.LoadObjectField(array, JSTypedArray::kLengthOffset));
  ValidateAtomicIndex(&a, index_word32, array_length_word32, context);
  Node* index_word = a.ChangeUint32ToWord(index_word32);

  Node* value_integer = a.ToInteger(context, value);
  Node* value_word32 = a.TruncateTaggedToWord32(context, value_integer);

  CodeStubAssembler::Label u8(&a), u16(&a), u32(&a), other(&a);
  int32_t case_values[] = {
      FIXED_INT8_ARRAY_TYPE,   FIXED_UINT8_ARRAY_TYPE, FIXED_INT16_ARRAY_TYPE,
      FIXED_UINT16_ARRAY_TYPE, FIXED_INT32_ARRAY_TYPE, FIXED_UINT32_ARRAY_TYPE,
  };
  CodeStubAssembler::Label* case_labels[] = {
      &u8, &u8, &u16, &u16, &u32, &u32,
  };
  a.Switch(instance_type, &other, case_values, case_labels,
           arraysize(case_labels));

  a.Bind(&u8);
  a.AtomicStore(MachineRepresentation::kWord8, backing_store, index_word,
                value_word32);
  a.Return(value_integer);

  a.Bind(&u16);
  a.AtomicStore(MachineRepresentation::kWord16, backing_store,
                a.WordShl(index_word, 1), value_word32);
  a.Return(value_integer);

  a.Bind(&u32);
  a.AtomicStore(MachineRepresentation::kWord32, backing_store,
                a.WordShl(index_word, 2), value_word32);
  a.Return(value_integer);

  // This shouldn't happen, we've already validated the type.
  a.Bind(&other);
  a.Return(a.SmiConstant(0));
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
    Handle<JSTypedArray> typedArray = Handle<JSTypedArray>::cast(object);
    if (typedArray->GetBuffer()->is_shared()) {
      if (only_int32) {
        if (typedArray->type() == kExternalInt32Array) return typedArray;
      } else {
        if (typedArray->type() != kExternalFloat32Array &&
            typedArray->type() != kExternalFloat64Array &&
            typedArray->type() != kExternalUint8ClampedArray)
          return typedArray;
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
MUST_USE_RESULT Maybe<size_t> ValidateAtomicAccess(
    Isolate* isolate, Handle<JSTypedArray> typedArray,
    Handle<Object> requestIndex) {
  // TOOD(v8:5961): Use ToIndex for indexes
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, requestIndex, Object::ToNumber(requestIndex), Nothing<size_t>());
  Handle<Object> offset;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, offset,
                                   Object::ToInteger(isolate, requestIndex),
                                   Nothing<size_t>());
  if (!requestIndex->SameValue(*offset)) {
    isolate->Throw(*isolate->factory()->NewRangeError(
        MessageTemplate::kInvalidAtomicAccessIndex));
    return Nothing<size_t>();
  }
  size_t accessIndex;
  uint32_t length = typedArray->length_value();
  if (!TryNumberToSize(*requestIndex, &accessIndex) || accessIndex >= length) {
    isolate->Throw(*isolate->factory()->NewRangeError(
        MessageTemplate::kInvalidAtomicAccessIndex));
    return Nothing<size_t>();
  }
  return Just<size_t>(accessIndex);
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

  Maybe<size_t> maybeIndex = ValidateAtomicAccess(isolate, sta, index);
  if (maybeIndex.IsNothing()) return isolate->heap()->exception();
  size_t i = maybeIndex.FromJust();

  uint32_t c;
  if (count->IsUndefined(isolate)) {
    c = kMaxUInt32;
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, count,
                                       Object::ToInteger(isolate, count));
    double countDouble = count->Number();
    if (countDouble < 0)
      countDouble = 0;
    else if (countDouble > kMaxUInt32)
      countDouble = kMaxUInt32;
    c = static_cast<uint32_t>(countDouble);
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

  Maybe<size_t> maybeIndex = ValidateAtomicAccess(isolate, sta, index);
  if (maybeIndex.IsNothing()) return isolate->heap()->exception();
  size_t i = maybeIndex.FromJust();

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value,
                                     Object::ToInt32(isolate, value));
  int32_t valueInt32 = NumberToInt32(*value);

  double timeoutNumber;
  if (timeout->IsUndefined(isolate)) {
    timeoutNumber = isolate->heap()->infinity_value()->Number();
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, timeout,
                                       Object::ToNumber(timeout));
    timeoutNumber = timeout->Number();
    if (std::isnan(timeoutNumber))
      timeoutNumber = isolate->heap()->infinity_value()->Number();
    else if (timeoutNumber < 0)
      timeoutNumber = 0;
  }

  if (!isolate->allow_atomics_wait()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kAtomicsWaitNotAllowed));
  }

  Handle<JSArrayBuffer> array_buffer = sta->GetBuffer();
  size_t addr = (i << 2) + NumberToSize(sta->byte_offset());

  return FutexEmulation::Wait(isolate, array_buffer, addr, valueInt32,
                              timeoutNumber);
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

template <typename T>
inline T ExchangeSeqCst(T* p, T value) {
  return __atomic_exchange_n(p, value, __ATOMIC_SEQ_CST);
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
  inline type ExchangeSeqCst(type* p, type value) {                         \
    return InterlockedExchange##suffix(reinterpret_cast<vctype*>(p),        \
                                       bit_cast<vctype>(value));            \
  }                                                                         \
                                                                            \
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

template <typename T>
inline Object* DoExchange(Isolate* isolate, void* buffer, size_t index,
                          Handle<Object> obj) {
  T value = FromObject<T>(obj);
  T result = ExchangeSeqCst(static_cast<T*>(buffer) + index, value);
  return ToObject(isolate, result);
}

// Uint8Clamped functions

uint8_t ClampToUint8(int32_t value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return value;
}

inline Object* DoCompareExchangeUint8Clamped(Isolate* isolate, void* buffer,
                                             size_t index,
                                             Handle<Object> oldobj,
                                             Handle<Object> newobj) {
  typedef int32_t convert_type;
  uint8_t oldval = ClampToUint8(FromObject<convert_type>(oldobj));
  uint8_t newval = ClampToUint8(FromObject<convert_type>(newobj));
  uint8_t result = CompareExchangeSeqCst(static_cast<uint8_t*>(buffer) + index,
                                         oldval, newval);
  return ToObject(isolate, result);
}

#define DO_UINT8_CLAMPED_OP(name, op)                                        \
  inline Object* Do##name##Uint8Clamped(Isolate* isolate, void* buffer,      \
                                        size_t index, Handle<Object> obj) {  \
    typedef int32_t convert_type;                                            \
    uint8_t* p = static_cast<uint8_t*>(buffer) + index;                      \
    convert_type operand = FromObject<convert_type>(obj);                    \
    uint8_t expected;                                                        \
    uint8_t result;                                                          \
    do {                                                                     \
      expected = *p;                                                         \
      result = ClampToUint8(static_cast<convert_type>(expected) op operand); \
    } while (CompareExchangeSeqCst(p, expected, result) != expected);        \
    return ToObject(isolate, expected);                                      \
  }

DO_UINT8_CLAMPED_OP(Add, +)
DO_UINT8_CLAMPED_OP(Sub, -)
DO_UINT8_CLAMPED_OP(And, &)
DO_UINT8_CLAMPED_OP(Or, |)
DO_UINT8_CLAMPED_OP(Xor, ^)

#undef DO_UINT8_CLAMPED_OP

inline Object* DoExchangeUint8Clamped(Isolate* isolate, void* buffer,
                                      size_t index, Handle<Object> obj) {
  typedef int32_t convert_type;
  uint8_t* p = static_cast<uint8_t*>(buffer) + index;
  uint8_t result = ClampToUint8(FromObject<convert_type>(obj));
  uint8_t expected;
  do {
    expected = *p;
  } while (CompareExchangeSeqCst(p, expected, result) != expected);
  return ToObject(isolate, expected);
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
  Handle<Object> expectedValue = args.atOrUndefined(isolate, 3);
  Handle<Object> replacementValue = args.atOrUndefined(isolate, 4);

  Handle<JSTypedArray> sta;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, sta, ValidateSharedIntegerTypedArray(isolate, array));

  Maybe<size_t> maybeIndex = ValidateAtomicAccess(isolate, sta, index);
  if (maybeIndex.IsNothing()) return isolate->heap()->exception();
  size_t i = maybeIndex.FromJust();

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, expectedValue,
                                     Object::ToInteger(isolate, expectedValue));

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, replacementValue, Object::ToInteger(isolate, replacementValue));

  uint8_t* source = static_cast<uint8_t*>(sta->GetBuffer()->backing_store()) +
                    NumberToSize(sta->byte_offset());

  switch (sta->type()) {
#define TYPED_ARRAY_CASE(Type, typeName, TYPE, ctype, size)            \
  case kExternal##Type##Array:                                         \
    return DoCompareExchange<ctype>(isolate, source, i, expectedValue, \
                                    replacementValue);

    INTEGER_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

    case kExternalUint8ClampedArray:
      return DoCompareExchangeUint8Clamped(isolate, source, i, expectedValue,
                                           replacementValue);

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

  Maybe<size_t> maybeIndex = ValidateAtomicAccess(isolate, sta, index);
  if (maybeIndex.IsNothing()) return isolate->heap()->exception();
  size_t i = maybeIndex.FromJust();

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

    case kExternalUint8ClampedArray:
      return DoAddUint8Clamped(isolate, source, i, value);

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

  Maybe<size_t> maybeIndex = ValidateAtomicAccess(isolate, sta, index);
  if (maybeIndex.IsNothing()) return isolate->heap()->exception();
  size_t i = maybeIndex.FromJust();

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

    case kExternalUint8ClampedArray:
      return DoSubUint8Clamped(isolate, source, i, value);

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

  Maybe<size_t> maybeIndex = ValidateAtomicAccess(isolate, sta, index);
  if (maybeIndex.IsNothing()) return isolate->heap()->exception();
  size_t i = maybeIndex.FromJust();

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

    case kExternalUint8ClampedArray:
      return DoAndUint8Clamped(isolate, source, i, value);

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

  Maybe<size_t> maybeIndex = ValidateAtomicAccess(isolate, sta, index);
  if (maybeIndex.IsNothing()) return isolate->heap()->exception();
  size_t i = maybeIndex.FromJust();

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

    case kExternalUint8ClampedArray:
      return DoOrUint8Clamped(isolate, source, i, value);

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

  Maybe<size_t> maybeIndex = ValidateAtomicAccess(isolate, sta, index);
  if (maybeIndex.IsNothing()) return isolate->heap()->exception();
  size_t i = maybeIndex.FromJust();

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

    case kExternalUint8ClampedArray:
      return DoXorUint8Clamped(isolate, source, i, value);

    default:
      break;
  }

  UNREACHABLE();
  return isolate->heap()->undefined_value();
}

// ES #sec-atomics.exchange
// Atomics.exchange( typedArray, index, value )
BUILTIN(AtomicsExchange) {
  HandleScope scope(isolate);
  Handle<Object> array = args.atOrUndefined(isolate, 1);
  Handle<Object> index = args.atOrUndefined(isolate, 2);
  Handle<Object> value = args.atOrUndefined(isolate, 3);

  Handle<JSTypedArray> sta;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, sta, ValidateSharedIntegerTypedArray(isolate, array));

  Maybe<size_t> maybeIndex = ValidateAtomicAccess(isolate, sta, index);
  if (maybeIndex.IsNothing()) return isolate->heap()->exception();
  size_t i = maybeIndex.FromJust();

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value,
                                     Object::ToInteger(isolate, value));

  uint8_t* source = static_cast<uint8_t*>(sta->GetBuffer()->backing_store()) +
                    NumberToSize(sta->byte_offset());

  switch (sta->type()) {
#define TYPED_ARRAY_CASE(Type, typeName, TYPE, ctype, size) \
  case kExternal##Type##Array:                              \
    return DoExchange<ctype>(isolate, source, i, value);

    INTEGER_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

    case kExternalUint8ClampedArray:
      return DoExchangeUint8Clamped(isolate, source, i, value);

    default:
      break;
  }

  UNREACHABLE();
  return isolate->heap()->undefined_value();
}

}  // namespace internal
}  // namespace v8
