// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/factory-base.h"

#include "src/ast/ast.h"
#include "src/execution/off-thread-isolate.h"
#include "src/handles/handles-inl.h"
#include "src/heap/factory.h"
#include "src/heap/off-thread-factory-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/oddball.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/objects/template-objects-inl.h"

namespace v8 {
namespace internal {

template <typename Impl>
template <AllocationType allocation>
HandleFor<Impl, HeapNumber> FactoryBase<Impl>::NewHeapNumber() {
  STATIC_ASSERT(HeapNumber::kSize <= kMaxRegularHeapObjectSize);
  Map map = read_only_roots().heap_number_map();
  HeapObject result = AllocateRawWithImmortalMap(HeapNumber::kSize, allocation,
                                                 map, kDoubleUnaligned);
  return handle(HeapNumber::cast(result), isolate());
}

template V8_EXPORT_PRIVATE Handle<HeapNumber>
FactoryBase<Factory>::NewHeapNumber<AllocationType::kYoung>();
template V8_EXPORT_PRIVATE Handle<HeapNumber>
FactoryBase<Factory>::NewHeapNumber<AllocationType::kOld>();
template V8_EXPORT_PRIVATE Handle<HeapNumber>
FactoryBase<Factory>::NewHeapNumber<AllocationType::kReadOnly>();

template V8_EXPORT_PRIVATE OffThreadHandle<HeapNumber>
FactoryBase<OffThreadFactory>::NewHeapNumber<AllocationType::kOld>();

template <typename Impl>
HandleFor<Impl, Struct> FactoryBase<Impl>::NewStruct(
    InstanceType type, AllocationType allocation) {
  Map map = Map::GetStructMap(read_only_roots(), type);
  int size = map.instance_size();
  HeapObject result = AllocateRawWithImmortalMap(size, allocation, map);
  HandleFor<Impl, Struct> str = handle(Struct::cast(result), isolate());
  str->InitializeBody(size);
  return str;
}

template <typename Impl>
HandleFor<Impl, FixedArray> FactoryBase<Impl>::NewFixedArray(
    int length, AllocationType allocation) {
  DCHECK_LE(0, length);
  if (length == 0) return impl()->empty_fixed_array();
  return NewFixedArrayWithFiller(read_only_roots().fixed_array_map(), length,
                                 read_only_roots().undefined_value(),
                                 allocation);
}

template <typename Impl>
HandleFor<Impl, FixedArray> FactoryBase<Impl>::NewFixedArrayWithMap(
    Map map, int length, AllocationType allocation) {
  // Zero-length case must be handled outside, where the knowledge about
  // the map is.
  DCHECK_LT(0, length);
  return NewFixedArrayWithFiller(
      map, length, read_only_roots().undefined_value(), allocation);
}

template <typename Impl>
HandleFor<Impl, FixedArray> FactoryBase<Impl>::NewFixedArrayWithHoles(
    int length, AllocationType allocation) {
  DCHECK_LE(0, length);
  if (length == 0) return impl()->empty_fixed_array();
  return NewFixedArrayWithFiller(read_only_roots().fixed_array_map(), length,
                                 read_only_roots().the_hole_value(),
                                 allocation);
}

template <typename Impl>
HandleFor<Impl, FixedArray> FactoryBase<Impl>::NewFixedArrayWithFiller(
    Map map, int length, Oddball filler, AllocationType allocation) {
  HeapObject result = AllocateRawFixedArray(length, allocation);
  DCHECK(ReadOnlyHeap::Contains(map));
  DCHECK(ReadOnlyHeap::Contains(filler));
  result.set_map_after_allocation(map, SKIP_WRITE_BARRIER);
  HandleFor<Impl, FixedArray> array =
      handle(FixedArray::cast(result), isolate());
  array->set_length(length);
  MemsetTagged(array->data_start(), filler, length);
  return array;
}

template <typename Impl>
HandleFor<Impl, FixedArrayBase> FactoryBase<Impl>::NewFixedDoubleArray(
    int length, AllocationType allocation) {
  if (length == 0) return impl()->empty_fixed_array();
  if (length < 0 || length > FixedDoubleArray::kMaxLength) {
    isolate()->FatalProcessOutOfHeapMemory("invalid array length");
  }
  int size = FixedDoubleArray::SizeFor(length);
  Map map = read_only_roots().fixed_double_array_map();
  HeapObject result =
      AllocateRawWithImmortalMap(size, allocation, map, kDoubleAligned);
  HandleFor<Impl, FixedDoubleArray> array =
      handle(FixedDoubleArray::cast(result), isolate());
  array->set_length(length);
  return array;
}

template <typename Impl>
HandleFor<Impl, ObjectBoilerplateDescription>
FactoryBase<Impl>::NewObjectBoilerplateDescription(int boilerplate,
                                                   int all_properties,
                                                   int index_keys,
                                                   bool has_seen_proto) {
  DCHECK_GE(boilerplate, 0);
  DCHECK_GE(all_properties, index_keys);
  DCHECK_GE(index_keys, 0);

  int backing_store_size =
      all_properties - index_keys - (has_seen_proto ? 1 : 0);
  DCHECK_GE(backing_store_size, 0);
  bool has_different_size_backing_store = boilerplate != backing_store_size;

  // Space for name and value for every boilerplate property + LiteralType flag.
  int size =
      2 * boilerplate + ObjectBoilerplateDescription::kDescriptionStartIndex;

  if (has_different_size_backing_store) {
    // An extra entry for the backing store size.
    size++;
  }

  HandleFor<Impl, ObjectBoilerplateDescription> description =
      HandleFor<Impl, ObjectBoilerplateDescription>::cast(NewFixedArrayWithMap(
          read_only_roots().object_boilerplate_description_map(), size,
          AllocationType::kOld));

  if (has_different_size_backing_store) {
    DCHECK_IMPLIES((boilerplate == (all_properties - index_keys)),
                   has_seen_proto);
    description->set_backing_store_size(backing_store_size);
  }

  description->set_flags(0);

  return description;
}

template <typename Impl>
HandleFor<Impl, ArrayBoilerplateDescription>
FactoryBase<Impl>::NewArrayBoilerplateDescription(
    ElementsKind elements_kind,
    HandleFor<Impl, FixedArrayBase> constant_values) {
  HandleFor<Impl, ArrayBoilerplateDescription> result =
      HandleFor<Impl, ArrayBoilerplateDescription>::cast(
          NewStruct(ARRAY_BOILERPLATE_DESCRIPTION_TYPE, AllocationType::kOld));
  result->set_elements_kind(elements_kind);
  result->set_constant_elements(*constant_values);
  return result;
}

template <typename Impl>
HandleFor<Impl, TemplateObjectDescription>
FactoryBase<Impl>::NewTemplateObjectDescription(
    HandleFor<Impl, FixedArray> raw_strings,
    HandleFor<Impl, FixedArray> cooked_strings) {
  DCHECK_EQ(raw_strings->length(), cooked_strings->length());
  DCHECK_LT(0, raw_strings->length());
  HandleFor<Impl, TemplateObjectDescription> result =
      HandleFor<Impl, TemplateObjectDescription>::cast(
          NewStruct(TEMPLATE_OBJECT_DESCRIPTION_TYPE, AllocationType::kOld));
  result->set_raw_strings(*raw_strings);
  result->set_cooked_strings(*cooked_strings);
  return result;
}

template <typename Impl>
HandleFor<Impl, SeqOneByteString>
FactoryBase<Impl>::NewOneByteInternalizedString(
    const Vector<const uint8_t>& str, uint32_t hash_field) {
  HandleFor<Impl, SeqOneByteString> result =
      AllocateRawOneByteInternalizedString(str.length(), hash_field);
  DisallowHeapAllocation no_gc;
  MemCopy(result->GetChars(no_gc), str.begin(), str.length());
  return result;
}

template <typename Impl>
HandleFor<Impl, SeqTwoByteString>
FactoryBase<Impl>::NewTwoByteInternalizedString(const Vector<const uc16>& str,
                                                uint32_t hash_field) {
  HandleFor<Impl, SeqTwoByteString> result =
      AllocateRawTwoByteInternalizedString(str.length(), hash_field);
  DisallowHeapAllocation no_gc;
  MemCopy(result->GetChars(no_gc), str.begin(), str.length() * kUC16Size);
  return result;
}

template <typename Impl>
MaybeHandleFor<Impl, SeqOneByteString> FactoryBase<Impl>::NewRawOneByteString(
    int length, AllocationType allocation) {
  if (length > String::kMaxLength || length < 0) {
    THROW_NEW_ERROR(isolate(), NewInvalidStringLengthError(), SeqOneByteString);
  }
  DCHECK_GT(length, 0);  // Use Factory::empty_string() instead.
  int size = SeqOneByteString::SizeFor(length);
  DCHECK_GE(SeqOneByteString::kMaxSize, size);

  HeapObject result = AllocateRawWithImmortalMap(
      size, allocation, read_only_roots().one_byte_string_map());
  HandleFor<Impl, SeqOneByteString> string =
      handle(SeqOneByteString::cast(result), isolate());
  string->set_length(length);
  string->set_hash_field(String::kEmptyHashField);
  DCHECK_EQ(size, string->Size());
  return string;
}

template <typename Impl>
MaybeHandleFor<Impl, SeqTwoByteString> FactoryBase<Impl>::NewRawTwoByteString(
    int length, AllocationType allocation) {
  if (length > String::kMaxLength || length < 0) {
    THROW_NEW_ERROR(isolate(), NewInvalidStringLengthError(), SeqTwoByteString);
  }
  DCHECK_GT(length, 0);  // Use Factory::empty_string() instead.
  int size = SeqTwoByteString::SizeFor(length);
  DCHECK_GE(SeqTwoByteString::kMaxSize, size);

  HeapObject result = AllocateRawWithImmortalMap(
      size, allocation, read_only_roots().string_map());
  HandleFor<Impl, SeqTwoByteString> string =
      handle(SeqTwoByteString::cast(result), isolate());
  string->set_length(length);
  string->set_hash_field(String::kEmptyHashField);
  DCHECK_EQ(size, string->Size());
  return string;
}

template <typename Impl>
MaybeHandleFor<Impl, String> FactoryBase<Impl>::NewConsString(
    HandleFor<Impl, String> left, HandleFor<Impl, String> right,
    AllocationType allocation) {
  if (left->IsThinString()) {
    left = handle(ThinString::cast(*left).actual(), isolate());
  }
  if (right->IsThinString()) {
    right = handle(ThinString::cast(*right).actual(), isolate());
  }
  int left_length = left->length();
  if (left_length == 0) return right;
  int right_length = right->length();
  if (right_length == 0) return left;

  int length = left_length + right_length;

  if (length == 2) {
    uint16_t c1 = left->Get(0);
    uint16_t c2 = right->Get(0);
    return impl()->MakeOrFindTwoCharacterString(c1, c2);
  }

  // Make sure that an out of memory exception is thrown if the length
  // of the new cons string is too large.
  if (length > String::kMaxLength || length < 0) {
    THROW_NEW_ERROR(isolate(), NewInvalidStringLengthError(), String);
  }

  bool left_is_one_byte = left->IsOneByteRepresentation();
  bool right_is_one_byte = right->IsOneByteRepresentation();
  bool is_one_byte = left_is_one_byte && right_is_one_byte;

  // If the resulting string is small make a flat string.
  if (length < ConsString::kMinLength) {
    // Note that neither of the two inputs can be a slice because:
    STATIC_ASSERT(ConsString::kMinLength <= SlicedString::kMinLength);
    DCHECK(left->IsFlat());
    DCHECK(right->IsFlat());

    STATIC_ASSERT(ConsString::kMinLength <= String::kMaxLength);
    if (is_one_byte) {
      HandleFor<Impl, SeqOneByteString> result =
          NewRawOneByteString(length, allocation).ToHandleChecked();
      DisallowHeapAllocation no_gc;
      uint8_t* dest = result->GetChars(no_gc);
      // Copy left part.
      const uint8_t* src = left->template GetChars<uint8_t>(no_gc);
      CopyChars(dest, src, left_length);
      // Copy right part.
      src = right->template GetChars<uint8_t>(no_gc);
      CopyChars(dest + left_length, src, right_length);
      return result;
    }

    HandleFor<Impl, SeqTwoByteString> result =
        NewRawTwoByteString(length, allocation).ToHandleChecked();

    DisallowHeapAllocation pointer_stays_valid;
    uc16* sink = result->GetChars(pointer_stays_valid);
    String::WriteToFlat(*left, sink, 0, left->length());
    String::WriteToFlat(*right, sink + left->length(), 0, right->length());
    return result;
  }

  return NewConsString(left, right, length, is_one_byte, allocation);
}

template <typename Impl>
HandleFor<Impl, String> FactoryBase<Impl>::NewConsString(
    HandleFor<Impl, String> left, HandleFor<Impl, String> right, int length,
    bool one_byte, AllocationType allocation) {
  DCHECK(!left->IsThinString());
  DCHECK(!right->IsThinString());
  DCHECK_GE(length, ConsString::kMinLength);
  DCHECK_LE(length, String::kMaxLength);

  HandleFor<Impl, ConsString> result = handle(
      ConsString::cast(
          one_byte
              ? NewWithImmortalMap(read_only_roots().cons_one_byte_string_map(),
                                   allocation)
              : NewWithImmortalMap(read_only_roots().cons_string_map(),
                                   allocation)),
      isolate());

  DisallowHeapAllocation no_gc;
  WriteBarrierMode mode = result->GetWriteBarrierMode(no_gc);

  result->set_hash_field(String::kEmptyHashField);
  result->set_length(length);
  result->set_first(*left, mode);
  result->set_second(*right, mode);
  return result;
}

template <typename Impl>
HandleFor<Impl, FreshlyAllocatedBigInt> FactoryBase<Impl>::NewBigInt(
    int length, AllocationType allocation) {
  if (length < 0 || length > BigInt::kMaxLength) {
    isolate()->FatalProcessOutOfHeapMemory("invalid BigInt length");
  }
  HeapObject result = AllocateRawWithImmortalMap(
      BigInt::SizeFor(length), allocation, read_only_roots().bigint_map());
  FreshlyAllocatedBigInt bigint = FreshlyAllocatedBigInt::cast(result);
  bigint.clear_padding();
  return handle(bigint, isolate());
}

template <typename Impl>
HandleFor<Impl, SeqOneByteString>
FactoryBase<Impl>::AllocateRawOneByteInternalizedString(int length,
                                                        uint32_t hash_field) {
  CHECK_GE(String::kMaxLength, length);
  // The canonical empty_string is the only zero-length string we allow.
  DCHECK_IMPLIES(length == 0, !impl()->EmptyStringRootIsInitialized());

  Map map = read_only_roots().one_byte_internalized_string_map();
  int size = SeqOneByteString::SizeFor(length);
  HeapObject result = AllocateRawWithImmortalMap(
      size,
      impl()->CanAllocateInReadOnlySpace() ? AllocationType::kReadOnly
                                           : AllocationType::kOld,
      map);
  HandleFor<Impl, SeqOneByteString> answer =
      handle(SeqOneByteString::cast(result), isolate());
  answer->set_length(length);
  answer->set_hash_field(hash_field);
  DCHECK_EQ(size, answer->Size());
  return answer;
}

template <typename Impl>
HandleFor<Impl, SeqTwoByteString>
FactoryBase<Impl>::AllocateRawTwoByteInternalizedString(int length,
                                                        uint32_t hash_field) {
  CHECK_GE(String::kMaxLength, length);
  DCHECK_NE(0, length);  // Use Heap::empty_string() instead.

  Map map = read_only_roots().internalized_string_map();
  int size = SeqTwoByteString::SizeFor(length);
  HeapObject result =
      AllocateRawWithImmortalMap(size, AllocationType::kOld, map);
  HandleFor<Impl, SeqTwoByteString> answer =
      handle(SeqTwoByteString::cast(result), isolate());
  answer->set_length(length);
  answer->set_hash_field(hash_field);
  DCHECK_EQ(size, result.Size());
  return answer;
}

template <typename Impl>
HeapObject FactoryBase<Impl>::AllocateRawArray(int size,
                                               AllocationType allocation) {
  HeapObject result = AllocateRaw(size, allocation);
  if (size > kMaxRegularHeapObjectSize && FLAG_use_marking_progress_bar) {
    MemoryChunk* chunk = MemoryChunk::FromHeapObject(result);
    chunk->SetFlag<AccessMode::ATOMIC>(MemoryChunk::HAS_PROGRESS_BAR);
  }
  return result;
}

template <typename Impl>
HeapObject FactoryBase<Impl>::AllocateRawFixedArray(int length,
                                                    AllocationType allocation) {
  if (length < 0 || length > FixedArray::kMaxLength) {
    isolate()->FatalProcessOutOfHeapMemory("invalid array length");
  }
  return AllocateRawArray(FixedArray::SizeFor(length), allocation);
}

template <typename Impl>
HeapObject FactoryBase<Impl>::AllocateRawWeakArrayList(
    int capacity, AllocationType allocation) {
  if (capacity < 0 || capacity > WeakArrayList::kMaxCapacity) {
    isolate()->FatalProcessOutOfHeapMemory("invalid array length");
  }
  return AllocateRawArray(WeakArrayList::SizeForCapacity(capacity), allocation);
}

template <typename Impl>
HeapObject FactoryBase<Impl>::NewWithImmortalMap(Map map,
                                                 AllocationType allocation) {
  return AllocateRawWithImmortalMap(map.instance_size(), allocation, map);
}

template <typename Impl>
HeapObject FactoryBase<Impl>::AllocateRawWithImmortalMap(
    int size, AllocationType allocation, Map map,
    AllocationAlignment alignment) {
  HeapObject result = AllocateRaw(size, allocation, alignment);
  result.set_map_after_allocation(map, SKIP_WRITE_BARRIER);
  return result;
}

template <typename Impl>
HeapObject FactoryBase<Impl>::AllocateRaw(int size, AllocationType allocation,
                                          AllocationAlignment alignment) {
  return impl()->AllocateRaw(size, allocation, alignment);
}

// Instantiate FactoryBase for the two variants we want.
template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) FactoryBase<Factory>;
template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    FactoryBase<OffThreadFactory>;

}  // namespace internal
}  // namespace v8
