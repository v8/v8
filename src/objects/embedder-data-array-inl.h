// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_EMBEDDER_DATA_ARRAY_INL_H_
#define V8_OBJECTS_EMBEDDER_DATA_ARRAY_INL_H_

#include "src/objects/embedder-data-array.h"

//#include "src/objects-inl.h"  // Needed for write barriers
#include "src/objects/maybe-object-inl.h"
#include "src/objects/slots.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

CAST_ACCESSOR2(EmbedderDataArray)

SMI_ACCESSORS(EmbedderDataArray, length, kLengthOffset)

OBJECT_CONSTRUCTORS_IMPL(EmbedderDataArray, HeapObjectPtr)

Address EmbedderDataArray::slots_start() {
  return FIELD_ADDR(this, OffsetOfElementAt(0));
}

Address EmbedderDataArray::slots_end() {
  return FIELD_ADDR(this, OffsetOfElementAt(length()));
}

Object* EmbedderDataArray::get(int index) const {
  DCHECK_LT(static_cast<unsigned>(index),
            static_cast<unsigned>(this->length()));
  return RELAXED_READ_FIELD(this, OffsetOfElementAt(index));
}

void EmbedderDataArray::set(int index, Smi value) {
  DCHECK_LT(static_cast<unsigned>(index),
            static_cast<unsigned>(this->length()));
  DCHECK(ObjectPtr(value).IsSmi());
  RELAXED_WRITE_FIELD(this, OffsetOfElementAt(index), value);
}

void EmbedderDataArray::set(int index, Object* value) {
  DCHECK_LT(static_cast<unsigned>(index),
            static_cast<unsigned>(this->length()));
  int offset = OffsetOfElementAt(index);
  RELAXED_WRITE_FIELD(this, offset, value);
  WRITE_BARRIER(this, offset, value);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_EMBEDDER_DATA_ARRAY_INL_H_
