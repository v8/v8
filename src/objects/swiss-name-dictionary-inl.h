// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SWISS_NAME_DICTIONARY_INL_H_
#define V8_OBJECTS_SWISS_NAME_DICTIONARY_INL_H_

#include <algorithm>

#include "src/base/macros.h"
#include "src/execution/isolate-utils-inl.h"
#include "src/heap/heap.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/js-collection-iterator.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "src/objects/swiss-name-dictionary.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/swiss-name-dictionary-tq-inl.inc"

CAST_ACCESSOR(SwissNameDictionary)
OBJECT_CONSTRUCTORS_IMPL(SwissNameDictionary, HeapObject)

const swiss_table::ctrl_t* SwissNameDictionary::CtrlTable() {
  return reinterpret_cast<ctrl_t*>(
      field_address(CtrlTableStartOffset(Capacity())));
}

int SwissNameDictionary::Capacity() {
  return ReadField<int32_t>(CapacityOffset());
}

void SwissNameDictionary::SetCapacity(int capacity) {
  DCHECK(IsValidCapacity(capacity));

  WriteField(CapacityOffset(), capacity);
}

int SwissNameDictionary::NumberOfElements() {
  return GetMetaTableField(kMetaTableElementCountOffset);
}

int SwissNameDictionary::NumberOfDeletedElements() {
  return GetMetaTableField(kMetaTableDeletedElementCountOffset);
}

void SwissNameDictionary::SetNumberOfElements(int elements) {
  SetMetaTableField(kMetaTableElementCountOffset, elements);
}

void SwissNameDictionary::SetNumberOfDeletedElements(int deleted_elements) {
  SetMetaTableField(kMetaTableDeletedElementCountOffset, deleted_elements);
}

int SwissNameDictionary::UsedCapacity() {
  return NumberOfElements() + NumberOfDeletedElements();
}

// static
constexpr bool SwissNameDictionary::IsValidCapacity(int capacity) {
  return capacity == 0 || (capacity >= kInitialCapacity &&
                           // Must be power of 2.
                           ((capacity & (capacity - 1)) == 0));
}

// static
constexpr int SwissNameDictionary::DataTableSize(int capacity) {
  return capacity * kTaggedSize * kDataTableEntryCount;
}

// static
constexpr int SwissNameDictionary::CtrlTableSize(int capacity) {
  // Doing + |kGroupWidth| due to the copy of first group at the end of control
  // table.
  return (capacity + kGroupWidth) * kOneByteSize;
}

// static
constexpr int SwissNameDictionary::SizeFor(int capacity) {
  CONSTEXPR_DCHECK(IsValidCapacity(capacity));
  return PropertyDetailsTableStartOffset(capacity) + capacity;
}

// We use 7/8th as maximum load factor for non-special cases.
// For 16-wide groups, that gives an average of two empty slots per group.
// Similar to Abseil's CapacityToGrowth.
// static
constexpr int SwissNameDictionary::MaxUsableCapacity(int capacity) {
  CONSTEXPR_DCHECK(IsValidCapacity(capacity));

  if (Group::kWidth == 8 && capacity == 4) {
    // If the group size is 16 we can fully utilize capacity 4: There will be
    // enough kEmpty entries in the ctrl table.
    return 3;
  }
  return capacity - capacity / 8;
}

// Returns |at_least_space_for| * 8/7 for non-special cases. Similar to Abseil's
// GrowthToLowerboundCapacity.
// static
int SwissNameDictionary::CapacityFor(int at_least_space_for) {
  if (at_least_space_for <= 4) {
    if (at_least_space_for == 0) {
      return 0;
    } else if (at_least_space_for < 4) {
      return 4;
    } else if (kGroupWidth == 16) {
      DCHECK_EQ(4, at_least_space_for);
      return 4;
    } else if (kGroupWidth == 8) {
      DCHECK_EQ(4, at_least_space_for);
      return 8;
    }
  }

  int non_normalized = at_least_space_for + at_least_space_for / 7;
  return base::bits::RoundUpToPowerOfTwo32(non_normalized);
}

void SwissNameDictionary::SetMetaTableField(int field_index, int value) {
  // See the STATIC_ASSERTs on |kMax1ByteMetaTableCapacity| and
  // |kMax2ByteMetaTableCapacity| in the .cc file for an explanation of these
  // constants.
  int capacity = Capacity();
  ByteArray meta_table = this->meta_table();
  if (capacity <= kMax1ByteMetaTableCapacity) {
    SetMetaTableField<uint8_t>(meta_table, field_index, value);
  } else if (capacity <= kMax2ByteMetaTableCapacity) {
    SetMetaTableField<uint16_t>(meta_table, field_index, value);
  } else {
    SetMetaTableField<uint32_t>(meta_table, field_index, value);
  }
}

int SwissNameDictionary::GetMetaTableField(int field_index) {
  // See the STATIC_ASSERTs on |kMax1ByteMetaTableCapacity| and
  // |kMax2ByteMetaTableCapacity| in the .cc file for an explanation of these
  // constants.
  int capacity = Capacity();
  ByteArray meta_table = this->meta_table();
  if (capacity <= kMax1ByteMetaTableCapacity) {
    return GetMetaTableField<uint8_t>(meta_table, field_index);
  } else if (capacity <= kMax2ByteMetaTableCapacity) {
    return GetMetaTableField<uint16_t>(meta_table, field_index);
  } else {
    return GetMetaTableField<uint32_t>(meta_table, field_index);
  }
}

// static
template <typename T>
void SwissNameDictionary::SetMetaTableField(ByteArray meta_table,
                                            int field_index, int value) {
  STATIC_ASSERT((std::is_same<T, uint8_t>::value) ||
                (std::is_same<T, uint16_t>::value) ||
                (std::is_same<T, uint32_t>::value));
  DCHECK_LE(value, std::numeric_limits<T>::max());
  DCHECK_LT(meta_table.GetDataStartAddress() + field_index * sizeof(T),
            meta_table.GetDataEndAddress());
  T* raw_data = reinterpret_cast<T*>(meta_table.GetDataStartAddress());
  raw_data[field_index] = value;
}

// static
template <typename T>
int SwissNameDictionary::GetMetaTableField(ByteArray meta_table,
                                           int field_index) {
  STATIC_ASSERT((std::is_same<T, uint8_t>::value) ||
                (std::is_same<T, uint16_t>::value) ||
                (std::is_same<T, uint32_t>::value));
  DCHECK_LT(meta_table.GetDataStartAddress() + field_index * sizeof(T),
            meta_table.GetDataEndAddress());
  T* raw_data = reinterpret_cast<T*>(meta_table.GetDataStartAddress());
  return raw_data[field_index];
}

constexpr int SwissNameDictionary::MetaTableSizePerEntryFor(int capacity) {
  CONSTEXPR_DCHECK(IsValidCapacity(capacity));

  // See the STATIC_ASSERTs on |kMax1ByteMetaTableCapacity| and
  // |kMax2ByteMetaTableCapacity| in the .cc file for an explanation of these
  // constants.
  if (capacity <= kMax1ByteMetaTableCapacity) {
    return sizeof(uint8_t);
  } else if (capacity <= kMax2ByteMetaTableCapacity) {
    return sizeof(uint16_t);
  } else {
    return sizeof(uint32_t);
  }
}

constexpr int SwissNameDictionary::MetaTableSizeFor(int capacity) {
  CONSTEXPR_DCHECK(IsValidCapacity(capacity));

  int per_entry_size = MetaTableSizePerEntryFor(capacity);

  // The enumeration table only needs to have as many slots as there can be
  // present + deleted entries in the hash table (= maximum load factor *
  // capactiy). Two more slots to store the number of present and deleted
  // entries.
  return per_entry_size * (MaxUsableCapacity(capacity) + 2);
}

template <typename LocalIsolate>
void SwissNameDictionary::Initialize(LocalIsolate* isolate,
                                     ByteArray meta_table, int capacity) {
  DCHECK(IsValidCapacity(capacity));
  DisallowHeapAllocation no_gc;
  ReadOnlyRoots roots(isolate);

  SetCapacity(capacity);
  SetHash(PropertyArray::kNoHashSentinel);

  ctrl_t* ctrl_table = reinterpret_cast<ctrl_t*>(
      field_address(CtrlTableStartOffset(Capacity())));
  memset(ctrl_table, Ctrl::kEmpty, CtrlTableSize(capacity));

  MemsetTagged(RawField(DataTableStartOffset()), roots.the_hole_value(),
               capacity * kDataTableEntryCount);

  set_meta_table(meta_table);

  SetNumberOfElements(0);
  SetNumberOfDeletedElements(0);

  // We leave the enumeration table PropertyDetails table and uninitialized.
}

void SwissNameDictionary::SetHash(int32_t hash) {
  WriteField(PrefixOffset(), hash);
}

int SwissNameDictionary::Hash() { return ReadField<int32_t>(PrefixOffset()); }

// static
constexpr int SwissNameDictionary::MaxCapacity() {
  int const_size =
      DataTableStartOffset() + ByteArray::kHeaderSize +
      // Size for present and deleted element count at max capacity:
      2 * sizeof(uint32_t);
  int per_entry_size =
      // size of data table entries:
      kDataTableEntryCount * kTaggedSize +
      // ctrl table entry size:
      kOneByteSize +
      // PropertyDetails table entry size:
      kOneByteSize +
      // Enumeration table entry size at maximum capacity:
      sizeof(uint32_t);

  int result = (FixedArray::kMaxSize - const_size) / per_entry_size;
  CONSTEXPR_DCHECK(result <= Smi::kMaxValue);

  return result;
}

// static
constexpr int SwissNameDictionary::PrefixOffset() {
  return HeapObject::kHeaderSize;
}

// static
constexpr int SwissNameDictionary::CapacityOffset() {
  return PrefixOffset() + sizeof(uint32_t);
}

// static
constexpr int SwissNameDictionary::MetaTablePointerOffset() {
  return CapacityOffset() + sizeof(int32_t);
}

// static
constexpr int SwissNameDictionary::DataTableStartOffset() {
  return MetaTablePointerOffset() + kTaggedSize;
}

// static
constexpr int SwissNameDictionary::DataTableEndOffset(int capacity) {
  return CtrlTableStartOffset(capacity);
}

// static
constexpr int SwissNameDictionary::CtrlTableStartOffset(int capacity) {
  return DataTableStartOffset() + DataTableSize(capacity);
}

// static
constexpr int SwissNameDictionary::PropertyDetailsTableStartOffset(
    int capacity) {
  return CtrlTableStartOffset(capacity) + CtrlTableSize(capacity);
}

ACCESSORS_CHECKED2(SwissNameDictionary, meta_table, ByteArray,
                   MetaTablePointerOffset(), true,
                   value.length() >= kMetaTableEnumerationTableStartOffset)

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_SWISS_NAME_DICTIONARY_INL_H_
