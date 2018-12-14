// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_STRUCT_INL_H_
#define V8_OBJECTS_STRUCT_INL_H_

#include "src/objects/struct.h"

#include "src/heap/heap-write-barrier-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

bool StructPtr::IsStructPtr() const {
  return reinterpret_cast<Struct*>(ptr())->IsStruct();
}
bool Tuple2Ptr::IsTuple2Ptr() const {
  return reinterpret_cast<Tuple2*>(ptr())->IsTuple2();
}

OBJECT_CONSTRUCTORS_IMPL(StructPtr, HeapObjectPtr)
OBJECT_CONSTRUCTORS_IMPL(Tuple2Ptr, StructPtr)
OBJECT_CONSTRUCTORS_IMPL(AccessorPair, StructPtr)

CAST_ACCESSOR2(AccessorPair)
CAST_ACCESSOR(Struct)
CAST_ACCESSOR2(StructPtr)
CAST_ACCESSOR(Tuple2)
CAST_ACCESSOR2(Tuple2Ptr)
CAST_ACCESSOR(Tuple3)

void Struct::InitializeBody(int object_size) {
  Object* value = GetReadOnlyRoots().undefined_value();
  for (int offset = kHeaderSize; offset < object_size; offset += kPointerSize) {
    WRITE_FIELD(this, offset, value);
  }
}

void StructPtr::InitializeBody(int object_size) {
  Object* value = GetReadOnlyRoots().undefined_value();
  for (int offset = kHeaderSize; offset < object_size; offset += kPointerSize) {
    WRITE_FIELD(this, offset, value);
  }
}

ACCESSORS(Tuple2, value1, Object, kValue1Offset)
ACCESSORS(Tuple2Ptr, value1, Object, kValue1Offset)
ACCESSORS(Tuple2, value2, Object, kValue2Offset)
ACCESSORS(Tuple2Ptr, value2, Object, kValue2Offset)
ACCESSORS(Tuple3, value3, Object, kValue3Offset)

ACCESSORS(AccessorPair, getter, Object, kGetterOffset)
ACCESSORS(AccessorPair, setter, Object, kSetterOffset)

Object* AccessorPair::get(AccessorComponent component) {
  return component == ACCESSOR_GETTER ? getter() : setter();
}

void AccessorPair::set(AccessorComponent component, Object* value) {
  if (component == ACCESSOR_GETTER) {
    set_getter(value);
  } else {
    set_setter(value);
  }
}

void AccessorPair::SetComponents(Object* getter, Object* setter) {
  if (!getter->IsNull()) set_getter(getter);
  if (!setter->IsNull()) set_setter(setter);
}

bool AccessorPair::Equals(Object* getter_value, Object* setter_value) {
  return (getter() == getter_value) && (setter() == setter_value);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_STRUCT_INL_H_
