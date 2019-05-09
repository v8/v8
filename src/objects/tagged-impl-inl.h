// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TAGGED_IMPL_INL_H_
#define V8_OBJECTS_TAGGED_IMPL_INL_H_

#include "src/objects/tagged-impl.h"

#ifdef V8_COMPRESS_POINTERS
#include "src/isolate.h"
#endif
#include "src/objects/heap-object.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::ToSmi(Smi* value) const {
  if (HAS_SMI_TAG(ptr_)) {
    *value = Smi::cast(Object(ptr_));
    return true;
  }
  return false;
}

template <HeapObjectReferenceType kRefType, typename StorageType>
Smi TaggedImpl<kRefType, StorageType>::ToSmi() const {
  DCHECK(HAS_SMI_TAG(ptr_));
  return Smi::cast(Object(ptr_));
}

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::GetHeapObject(
    HeapObject* result) const {
  if (!IsStrongOrWeak()) return false;
  *result = GetHeapObject();
  return true;
}

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::GetHeapObject(
    HeapObject* result, HeapObjectReferenceType* reference_type) const {
  if (!IsStrongOrWeak()) return false;
  *reference_type = IsWeakOrCleared() ? HeapObjectReferenceType::WEAK
                                      : HeapObjectReferenceType::STRONG;
  *result = GetHeapObject();
  return true;
}

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::GetHeapObjectIfStrong(
    HeapObject* result) const {
  if (IsStrong()) {
    *result = HeapObject::cast(Object(ptr_));
    return true;
  }
  return false;
}

template <HeapObjectReferenceType kRefType, typename StorageType>
HeapObject TaggedImpl<kRefType, StorageType>::GetHeapObjectAssumeStrong()
    const {
  DCHECK(IsStrong());
  return HeapObject::cast(Object(ptr_));
}

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::GetHeapObjectIfWeak(
    HeapObject* result) const {
  if (kCanBeWeak) {
    if (IsWeak()) {
      *result = GetHeapObject();
      return true;
    }
    return false;
  } else {
    DCHECK(!HAS_WEAK_HEAP_OBJECT_TAG(ptr_));
    return false;
  }
}

template <HeapObjectReferenceType kRefType, typename StorageType>
HeapObject TaggedImpl<kRefType, StorageType>::GetHeapObjectAssumeWeak() const {
  DCHECK(IsWeak());
  return GetHeapObject();
}

template <HeapObjectReferenceType kRefType, typename StorageType>
HeapObject TaggedImpl<kRefType, StorageType>::GetHeapObject() const {
  DCHECK(!IsSmi());
  if (kCanBeWeak) {
    DCHECK(!IsCleared());
    return HeapObject::cast(Object(ptr_ & ~kWeakHeapObjectMask));
  } else {
    DCHECK(!HAS_WEAK_HEAP_OBJECT_TAG(ptr_));
    return HeapObject::cast(Object(ptr_));
  }
}

template <HeapObjectReferenceType kRefType, typename StorageType>
Object TaggedImpl<kRefType, StorageType>::GetHeapObjectOrSmi() const {
  if (IsSmi()) {
    return Object(ptr_);
  }
  return GetHeapObject();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_TAGGED_IMPL_INL_H_
