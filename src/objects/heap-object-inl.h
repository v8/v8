// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_HEAP_OBJECT_INL_H_
#define V8_OBJECTS_HEAP_OBJECT_INL_H_

#include "src/objects/heap-object.h"

#include "src/heap/heap-write-barrier-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#define TYPE_CHECK_FORWARDER(Type)                       \
  bool ObjectPtr::Is##Type() const {                     \
    return reinterpret_cast<Object*>(ptr())->Is##Type(); \
  }
HEAP_OBJECT_TYPE_LIST(TYPE_CHECK_FORWARDER)
TYPE_CHECK_FORWARDER(LayoutDescriptor);
TYPE_CHECK_FORWARDER(Primitive);
TYPE_CHECK_FORWARDER(Number);
TYPE_CHECK_FORWARDER(Numeric);
#undef TYPE_CHECK_FORWARDER

#define TYPE_CHECK_FORWARDER(NAME, Name, name)           \
  bool ObjectPtr::Is##Name() const {                     \
    return reinterpret_cast<Object*>(ptr())->Is##Name(); \
  }
STRUCT_LIST(TYPE_CHECK_FORWARDER)
#undef TYPE_CHECK_FORWARDER

#define TYPE_CHECK_FORWARDER(Type, Value)                       \
  bool ObjectPtr::Is##Type(Isolate* isolate) const {            \
    return reinterpret_cast<Object*>(ptr())->Is##Type(isolate); \
  }                                                             \
  bool ObjectPtr::Is##Type(ReadOnlyRoots roots) const {         \
    return reinterpret_cast<Object*>(ptr())->Is##Type(roots);   \
  }                                                             \
  bool ObjectPtr::Is##Type() const {                            \
    return reinterpret_cast<Object*>(ptr())->Is##Type();        \
  }
ODDBALL_LIST(TYPE_CHECK_FORWARDER)
#undef TYPE_CHECK_FORWARDER

bool ObjectPtr::IsHashTableBase() const { return IsHashTable(); }

bool ObjectPtr::IsSmallOrderedHashTable() const {
  return IsSmallOrderedHashSet() || IsSmallOrderedHashMap() ||
         IsSmallOrderedNameDictionary();
}

bool ObjectPtr::GetHeapObjectIfStrong(HeapObject* result) const {
  return GetHeapObject(result);
}

bool ObjectPtr::GetHeapObject(HeapObject* result) const {
  if (!IsHeapObject()) return false;
  *result = HeapObject::cast(*this);
  return true;
}

HeapObject ObjectPtr::GetHeapObject() const {
  DCHECK(IsHeapObject());
  return HeapObject::cast(*this);
}

double ObjectPtr::Number() const {
  return reinterpret_cast<Object*>(ptr())->Number();
}

bool ObjectPtr::ToInt32(int32_t* value) const {
  return reinterpret_cast<Object*>(ptr())->ToInt32(value);
}

bool ObjectPtr::ToUint32(uint32_t* value) const {
  return reinterpret_cast<Object*>(ptr())->ToUint32(value);
}

bool ObjectPtr::FilterKey(PropertyFilter filter) {
  return reinterpret_cast<Object*>(ptr())->FilterKey(filter);
}

Object* ObjectPtr::GetHash() {
  return reinterpret_cast<Object*>(ptr())->GetHash();
}

bool ObjectPtr::ToArrayIndex(uint32_t* index) const {
  return reinterpret_cast<Object*>(ptr())->ToArrayIndex(index);
}

void ObjectPtr::VerifyApiCallResultType() {
  reinterpret_cast<Object*>(ptr())->VerifyApiCallResultType();
}

void ObjectPtr::ShortPrint(FILE* out) const {
  return reinterpret_cast<Object*>(ptr())->ShortPrint(out);
}

void ObjectPtr::Print() const { reinterpret_cast<Object*>(ptr())->Print(); }

void ObjectPtr::Print(std::ostream& os) const {
  reinterpret_cast<Object*>(ptr())->Print(os);
}

HeapObject::HeapObject(Address ptr, AllowInlineSmiStorage allow_smi)
    : ObjectPtr(ptr) {
  SLOW_DCHECK(
      (allow_smi == AllowInlineSmiStorage::kAllowBeingASmi && IsSmi()) ||
      IsHeapObject());
}

HeapObject HeapObject::FromAddress(Address address) {
  DCHECK_TAG_ALIGNED(address);
  return HeapObject(address + kHeapObjectTag);
}

Heap* NeverReadOnlySpaceObjectPtr::GetHeap(const HeapObject object) {
  return GetHeapFromWritableObject(object);
}

Isolate* NeverReadOnlySpaceObjectPtr::GetIsolate(const HeapObject object) {
  return GetHeap(object)->isolate();
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_HEAP_OBJECT_INL_H_
