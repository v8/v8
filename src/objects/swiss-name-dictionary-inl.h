// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SWISS_NAME_DICTIONARY_INL_H_
#define V8_OBJECTS_SWISS_NAME_DICTIONARY_INL_H_

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

int SwissNameDictionary::Capacity() {
  return ReadField<int32_t>(CapacityOffset());
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

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_SWISS_NAME_DICTIONARY_INL_H_
