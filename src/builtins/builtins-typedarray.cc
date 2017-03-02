// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"
#include "src/counters.h"
#include "src/elements.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

class TypedArrayBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit TypedArrayBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  void GenerateTypedArrayPrototypeGetter(const char* method_name,
                                         int object_offset);
  template <IterationKind kIterationKind>
  void GenerateTypedArrayPrototypeIterationMethod(const char* method_name);

  void LoadMapAndElementsSize(Node* array, Variable* typed_map, Variable* size);

  void DoInitialize(Node* holder, Node* length, Node* maybe_buffer,
                    Node* byte_offset, Node* byte_length, Node* initialize,
                    Node* context);
};

void TypedArrayBuiltinsAssembler::LoadMapAndElementsSize(Node* array,
                                                         Variable* typed_map,
                                                         Variable* size) {
  Label unreachable(this), done(this);
  Label uint8_elements(this), uint8_clamped_elements(this), int8_elements(this),
      uint16_elements(this), int16_elements(this), uint32_elements(this),
      int32_elements(this), float32_elements(this), float64_elements(this);
  Label* elements_kind_labels[] = {
      &uint8_elements,  &uint8_clamped_elements, &int8_elements,
      &uint16_elements, &int16_elements,         &uint32_elements,
      &int32_elements,  &float32_elements,       &float64_elements};
  int32_t elements_kinds[] = {
      UINT8_ELEMENTS,  UINT8_CLAMPED_ELEMENTS, INT8_ELEMENTS,
      UINT16_ELEMENTS, INT16_ELEMENTS,         UINT32_ELEMENTS,
      INT32_ELEMENTS,  FLOAT32_ELEMENTS,       FLOAT64_ELEMENTS};
  const size_t kTypedElementsKindCount = LAST_FIXED_TYPED_ARRAY_ELEMENTS_KIND -
                                         FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND +
                                         1;
  DCHECK_EQ(kTypedElementsKindCount, arraysize(elements_kinds));
  DCHECK_EQ(kTypedElementsKindCount, arraysize(elements_kind_labels));

  Node* array_map = LoadMap(array);
  Node* elements_kind = LoadMapElementsKind(array_map);
  Switch(elements_kind, &unreachable, elements_kinds, elements_kind_labels,
         kTypedElementsKindCount);

  for (int i = 0; i < static_cast<int>(kTypedElementsKindCount); i++) {
    Bind(elements_kind_labels[i]);
    {
      ElementsKind kind = static_cast<ElementsKind>(elements_kinds[i]);
      ExternalArrayType type =
          isolate()->factory()->GetArrayTypeFromElementsKind(kind);
      Handle<Map> map(isolate()->heap()->MapForFixedTypedArray(type));
      typed_map->Bind(HeapConstant(map));
      size->Bind(SmiConstant(static_cast<int>(
          isolate()->factory()->GetExternalArrayElementSize(type))));
      Goto(&done);
    }
  }

  Bind(&unreachable);
  { Unreachable(); }
  Bind(&done);
}

void TypedArrayBuiltinsAssembler::DoInitialize(
    Node* holder, Node* length, Node* maybe_buffer, Node* byte_offset,
    Node* byte_length, Node* initialize, Node* context) {
  static const int32_t fta_base_data_offset =
      FixedTypedArrayBase::kDataOffset - kHeapObjectTag;

  Label setup_holder(this), alloc_array_buffer(this), aligned(this),
      allocate_elements(this), attach_buffer(this), done(this);
  Variable fixed_typed_map(this, MachineRepresentation::kTagged);
  Variable element_size(this, MachineRepresentation::kTagged);
  Variable total_size(this, MachineType::PointerRepresentation());

  // Make sure length is a Smi. The caller guarantees this is the case.
  length = ToInteger(context, length, CodeStubAssembler::kTruncateMinusZero);
  CSA_ASSERT(this, TaggedIsSmi(length));

  byte_offset =
      ToInteger(context, byte_offset, CodeStubAssembler::kTruncateMinusZero);
  CSA_ASSERT(this, TaggedIsSmi(byte_offset));

  // byte_length can be -0, get rid of it.
  byte_length =
      ToInteger(context, byte_length, CodeStubAssembler::kTruncateMinusZero);

  GotoIfNot(IsNull(maybe_buffer), &setup_holder);
  // If the buffer is null, then we need a Smi byte_length. The caller
  // guarantees this is the case, because when byte_length >
  // TypedArrayMaxSizeInHeap, a buffer is allocated and passed in here.
  CSA_ASSERT(this, TaggedIsSmi(byte_length));
  Goto(&setup_holder);

  Bind(&setup_holder);
  {
    LoadMapAndElementsSize(holder, &fixed_typed_map, &element_size);
    // Setup the holder (JSArrayBufferView).
    //  - Set the length.
    //  - Set the byte_offset.
    //  - Set the byte_length.
    //  - Set InternalFields to 0.
    StoreObjectField(holder, JSTypedArray::kLengthOffset, length);
    StoreObjectField(holder, JSArrayBufferView::kByteOffsetOffset, byte_offset);
    StoreObjectField(holder, JSArrayBufferView::kByteLengthOffset, byte_length);
    for (int offset = JSTypedArray::kSize;
         offset < JSTypedArray::kSizeWithInternalFields;
         offset += kPointerSize) {
      StoreObjectField(holder, offset, SmiConstant(Smi::kZero));
    }

    Branch(IsNull(maybe_buffer), &alloc_array_buffer, &attach_buffer);
  }

  Bind(&alloc_array_buffer);
  {
    // Allocate a new ArrayBuffer and initialize it with empty properties and
    // elements.
    Node* const native_context = LoadNativeContext(context);
    Node* const map =
        LoadContextElement(native_context, Context::ARRAY_BUFFER_MAP_INDEX);
    Node* empty_fixed_array = LoadRoot(Heap::kEmptyFixedArrayRootIndex);

    Node* buffer = Allocate(JSArrayBuffer::kSizeWithInternalFields);
    StoreMapNoWriteBarrier(buffer, map);
    StoreObjectFieldNoWriteBarrier(buffer, JSArray::kPropertiesOffset,
                                   empty_fixed_array);
    StoreObjectFieldNoWriteBarrier(buffer, JSArray::kElementsOffset,
                                   empty_fixed_array);
    // Setup the ArrayBuffer.
    //  - Set BitField to 0.
    //  - Set IsExternal and IsNeuterable bits of BitFieldSlot.
    //  - Set the byte_length field to byte_length.
    //  - Set backing_store to null/Smi(0).
    //  - Set all internal fields to Smi(0).
    StoreObjectFieldNoWriteBarrier(buffer, JSArrayBuffer::kBitFieldSlot,
                                   SmiConstant(Smi::kZero));
    int32_t bitfield_value = (1 << JSArrayBuffer::IsExternal::kShift) |
                             (1 << JSArrayBuffer::IsNeuterable::kShift);
    StoreObjectFieldNoWriteBarrier(buffer, JSArrayBuffer::kBitFieldOffset,
                                   Int32Constant(bitfield_value),
                                   MachineRepresentation::kWord32);

    StoreObjectFieldNoWriteBarrier(buffer, JSArrayBuffer::kByteLengthOffset,
                                   byte_length);
    StoreObjectFieldNoWriteBarrier(buffer, JSArrayBuffer::kBackingStoreOffset,
                                   SmiConstant(Smi::kZero));
    for (int i = 0; i < v8::ArrayBuffer::kInternalFieldCount; i++) {
      int offset = JSArrayBuffer::kSize + i * kPointerSize;
      StoreObjectFieldNoWriteBarrier(buffer, offset, SmiConstant(Smi::kZero));
    }

    StoreObjectField(holder, JSArrayBufferView::kBufferOffset, buffer);

    // Check the alignment.
    GotoIf(SmiEqual(SmiMod(element_size.value(), SmiConstant(kObjectAlignment)),
                    SmiConstant(0)),
           &aligned);

    // Fix alignment if needed.
    DCHECK_EQ(0, FixedTypedArrayBase::kHeaderSize & kObjectAlignmentMask);
    Node* aligned_header_size =
        IntPtrConstant(FixedTypedArrayBase::kHeaderSize + kObjectAlignmentMask);
    Node* size = IntPtrAdd(SmiToWord(byte_length), aligned_header_size);
    total_size.Bind(WordAnd(size, IntPtrConstant(~kObjectAlignmentMask)));
    Goto(&allocate_elements);
  }

  Bind(&aligned);
  {
    Node* header_size = IntPtrConstant(FixedTypedArrayBase::kHeaderSize);
    total_size.Bind(IntPtrAdd(SmiToWord(byte_length), header_size));
    Goto(&allocate_elements);
  }

  Bind(&allocate_elements);
  {
    // Allocate a FixedTypedArray and set the length, base pointer and external
    // pointer.
    CSA_ASSERT(this, IsRegularHeapObjectSize(total_size.value()));
    Node* elements = Allocate(total_size.value());

    StoreMapNoWriteBarrier(elements, fixed_typed_map.value());
    StoreObjectFieldNoWriteBarrier(elements, FixedArray::kLengthOffset, length);
    StoreObjectFieldNoWriteBarrier(
        elements, FixedTypedArrayBase::kBasePointerOffset, elements);
    StoreObjectFieldNoWriteBarrier(elements,
                                   FixedTypedArrayBase::kExternalPointerOffset,
                                   IntPtrConstant(fta_base_data_offset),
                                   MachineType::PointerRepresentation());

    StoreObjectField(holder, JSObject::kElementsOffset, elements);

    GotoIf(IsFalse(initialize), &done);
    // Initialize the backing store by filling it with 0s.
    Node* backing_store = IntPtrAdd(BitcastTaggedToWord(elements),
                                    IntPtrConstant(fta_base_data_offset));
    // Call out to memset to perform initialization.
    Node* memset =
        ExternalConstant(ExternalReference::libc_memset_function(isolate()));
    CallCFunction3(MachineType::AnyTagged(), MachineType::Pointer(),
                   MachineType::IntPtr(), MachineType::UintPtr(), memset,
                   backing_store, IntPtrConstant(0), SmiToWord(byte_length));
    Goto(&done);
  }

  Bind(&attach_buffer);
  {
    StoreObjectField(holder, JSArrayBufferView::kBufferOffset, maybe_buffer);

    Node* elements = Allocate(FixedTypedArrayBase::kHeaderSize);
    StoreMapNoWriteBarrier(elements, fixed_typed_map.value());
    StoreObjectFieldNoWriteBarrier(elements, FixedArray::kLengthOffset, length);
    StoreObjectFieldNoWriteBarrier(
        elements, FixedTypedArrayBase::kBasePointerOffset, SmiConstant(0));

    Node* backing_store =
        LoadObjectField(maybe_buffer, JSArrayBuffer::kBackingStoreOffset,
                        MachineType::Pointer());
    Node* external_pointer = IntPtrAdd(backing_store, SmiToWord(byte_offset));
    StoreObjectFieldNoWriteBarrier(
        elements, FixedTypedArrayBase::kExternalPointerOffset, external_pointer,
        MachineType::PointerRepresentation());

    StoreObjectField(holder, JSObject::kElementsOffset, elements);

    Goto(&done);
  }

  Bind(&done);
  Return(UndefinedConstant());
}

TF_BUILTIN(TypedArrayInitialize, TypedArrayBuiltinsAssembler) {
  Node* holder = Parameter(1);
  Node* length = Parameter(2);
  Node* maybe_buffer = Parameter(3);
  Node* byte_offset = Parameter(4);
  Node* byte_length = Parameter(5);
  Node* initialize = Parameter(6);
  Node* context = Parameter(9);

  DoInitialize(holder, length, maybe_buffer, byte_offset, byte_length,
               initialize, context);
}

// -----------------------------------------------------------------------------
// ES6 section 22.2 TypedArray Objects

// ES6 section 22.2.4.2 TypedArray ( length )
TF_BUILTIN(TypedArrayConstructByLength, TypedArrayBuiltinsAssembler) {
  // We know that holder cannot be an object if this builtin was called.
  Node* holder = Parameter(1);
  Node* length = Parameter(2);
  Node* element_size = Parameter(3);
  Node* context = Parameter(6);

  Variable maybe_buffer(this, MachineRepresentation::kTagged);
  maybe_buffer.Bind(NullConstant());
  Node* byte_offset = SmiConstant(0);
  Node* initialize = BooleanConstant(true);

  Label external_buffer(this), call_init(this), invalid_length(this);

  length = ToInteger(context, length, CodeStubAssembler::kTruncateMinusZero);
  // The maximum length of a TypedArray is MaxSmi().
  // Note: this is not per spec, but rather a constraint of our current
  // representation (which uses smi's).
  GotoIf(TaggedIsNotSmi(length), &invalid_length);
  GotoIf(SmiLessThan(length, SmiConstant(0)), &invalid_length);

  // For byte_length < typed_array_max_size_in_heap, we allocate the buffer on
  // the heap. Otherwise we allocate it externally and attach it.
  Node* byte_length = SmiMul(length, element_size);
  GotoIf(TaggedIsNotSmi(byte_length), &external_buffer);
  Branch(SmiLessThanOrEqual(byte_length,
                            SmiConstant(FLAG_typed_array_max_size_in_heap)),
         &call_init, &external_buffer);

  Bind(&external_buffer);
  {
    Node* const buffer_constructor = LoadContextElement(
        LoadNativeContext(context), Context::ARRAY_BUFFER_FUN_INDEX);
    maybe_buffer.Bind(ConstructJS(CodeFactory::Construct(isolate()), context,
                                  buffer_constructor, byte_length));
    Goto(&call_init);
  }

  Bind(&call_init);
  {
    DoInitialize(holder, length, maybe_buffer.value(), byte_offset, byte_length,
                 initialize, context);
  }

  Bind(&invalid_length);
  {
    CallRuntime(Runtime::kThrowRangeError, context,
                SmiConstant(MessageTemplate::kInvalidTypedArrayLength));
    Unreachable();
  }
}

// ES6 section 22.2.3.1 get %TypedArray%.prototype.buffer
BUILTIN(TypedArrayPrototypeBuffer) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSTypedArray, typed_array, "get TypedArray.prototype.buffer");
  return *typed_array->GetBuffer();
}

void TypedArrayBuiltinsAssembler::GenerateTypedArrayPrototypeGetter(
    const char* method_name, int object_offset) {
  Node* receiver = Parameter(0);
  Node* context = Parameter(3);

  // Check if the {receiver} is actually a JSTypedArray.
  Label receiver_is_incompatible(this, Label::kDeferred);
  GotoIf(TaggedIsSmi(receiver), &receiver_is_incompatible);
  GotoIfNot(HasInstanceType(receiver, JS_TYPED_ARRAY_TYPE),
            &receiver_is_incompatible);

  // Check if the {receiver}'s JSArrayBuffer was neutered.
  Node* receiver_buffer =
      LoadObjectField(receiver, JSTypedArray::kBufferOffset);
  Label if_receiverisneutered(this, Label::kDeferred);
  GotoIf(IsDetachedBuffer(receiver_buffer), &if_receiverisneutered);
  Return(LoadObjectField(receiver, object_offset));

  Bind(&if_receiverisneutered);
  {
    // The {receiver}s buffer was neutered, default to zero.
    Return(SmiConstant(0));
  }

  Bind(&receiver_is_incompatible);
  {
    // The {receiver} is not a valid JSTypedArray.
    CallRuntime(Runtime::kThrowIncompatibleMethodReceiver, context,
                HeapConstant(
                    factory()->NewStringFromAsciiChecked(method_name, TENURED)),
                receiver);
    Unreachable();
  }
}

// ES6 section 22.2.3.2 get %TypedArray%.prototype.byteLength
TF_BUILTIN(TypedArrayPrototypeByteLength, TypedArrayBuiltinsAssembler) {
  GenerateTypedArrayPrototypeGetter("get TypedArray.prototype.byteLength",
                                    JSTypedArray::kByteLengthOffset);
}

// ES6 section 22.2.3.3 get %TypedArray%.prototype.byteOffset
TF_BUILTIN(TypedArrayPrototypeByteOffset, TypedArrayBuiltinsAssembler) {
  GenerateTypedArrayPrototypeGetter("get TypedArray.prototype.byteOffset",
                                    JSTypedArray::kByteOffsetOffset);
}

// ES6 section 22.2.3.18 get %TypedArray%.prototype.length
TF_BUILTIN(TypedArrayPrototypeLength, TypedArrayBuiltinsAssembler) {
  GenerateTypedArrayPrototypeGetter("get TypedArray.prototype.length",
                                    JSTypedArray::kLengthOffset);
}

template <IterationKind kIterationKind>
void TypedArrayBuiltinsAssembler::GenerateTypedArrayPrototypeIterationMethod(
    const char* method_name) {
  Node* receiver = Parameter(0);
  Node* context = Parameter(3);

  Label throw_bad_receiver(this, Label::kDeferred);
  Label throw_typeerror(this, Label::kDeferred);

  GotoIf(TaggedIsSmi(receiver), &throw_bad_receiver);

  Node* map = LoadMap(receiver);
  Node* instance_type = LoadMapInstanceType(map);
  GotoIf(Word32NotEqual(instance_type, Int32Constant(JS_TYPED_ARRAY_TYPE)),
         &throw_bad_receiver);

  // Check if the {receiver}'s JSArrayBuffer was neutered.
  Node* receiver_buffer =
      LoadObjectField(receiver, JSTypedArray::kBufferOffset);
  Label if_receiverisneutered(this, Label::kDeferred);
  GotoIf(IsDetachedBuffer(receiver_buffer), &if_receiverisneutered);

  Return(CreateArrayIterator(receiver, map, instance_type, context,
                             kIterationKind));

  Variable var_message(this, MachineRepresentation::kTagged);
  Bind(&throw_bad_receiver);
  var_message.Bind(SmiConstant(MessageTemplate::kNotTypedArray));
  Goto(&throw_typeerror);

  Bind(&if_receiverisneutered);
  var_message.Bind(
      SmiConstant(Smi::FromInt(MessageTemplate::kDetachedOperation)));
  Goto(&throw_typeerror);

  Bind(&throw_typeerror);
  {
    Node* method_arg = HeapConstant(
        isolate()->factory()->NewStringFromAsciiChecked(method_name, TENURED));
    Node* result = CallRuntime(Runtime::kThrowTypeError, context,
                               var_message.value(), method_arg);
    Return(result);
  }
}

TF_BUILTIN(TypedArrayPrototypeValues, TypedArrayBuiltinsAssembler) {
  GenerateTypedArrayPrototypeIterationMethod<IterationKind::kValues>(
      "%TypedArray%.prototype.values()");
}

TF_BUILTIN(TypedArrayPrototypeEntries, TypedArrayBuiltinsAssembler) {
  GenerateTypedArrayPrototypeIterationMethod<IterationKind::kEntries>(
      "%TypedArray%.prototype.entries()");
}

TF_BUILTIN(TypedArrayPrototypeKeys, TypedArrayBuiltinsAssembler) {
  GenerateTypedArrayPrototypeIterationMethod<IterationKind::kKeys>(
      "%TypedArray%.prototype.keys()");
}

namespace {

int64_t CapRelativeIndex(Handle<Object> num, int64_t minimum, int64_t maximum) {
  int64_t relative;
  if (V8_LIKELY(num->IsSmi())) {
    relative = Smi::cast(*num)->value();
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

}  // namespace

BUILTIN(TypedArrayPrototypeCopyWithin) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.copyWithin";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  if (V8_UNLIKELY(array->WasNeutered())) return *array;

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

  Handle<Object> search_element = args.at<Object>(1);
  ElementsAccessor* elements = array->GetElementsAccessor();
  Maybe<bool> result = elements->IncludesValue(isolate, array, search_element,
                                               static_cast<uint32_t>(index),
                                               static_cast<uint32_t>(len));
  return *isolate->factory()->ToBoolean(result.FromJust());
}

}  // namespace internal
}  // namespace v8
