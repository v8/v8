// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_FIELD_INDEX_INL_H_
#define V8_FIELD_INDEX_INL_H_

#include "src/field-index.h"

namespace v8 {
namespace internal {


inline FieldIndex FieldIndex::ForInObjectOffset(int offset, Map* map) {
  ASSERT((offset % kPointerSize) == 0);
  int index = offset / kPointerSize;
  if (map == NULL) {
    return FieldIndex(true, index, false, index + 1, 0, true);
  }
  int first_inobject_offset = map->GetInObjectPropertyOffset(0);
  if (offset < first_inobject_offset) {
    return FieldIndex(true, index, false, 0, 0, true);
  } else {
    return FieldIndex::ForPropertyIndex(map, offset / kPointerSize);
  }
}


inline FieldIndex FieldIndex::ForPropertyIndex(Map* map,
                                               int property_index,
                                               bool is_double) {
  ASSERT(map->instance_type() >= FIRST_NONSTRING_TYPE);
  int inobject_properties = map->inobject_properties();
  bool is_inobject = property_index < inobject_properties;
  int first_inobject_offset;
  if (is_inobject) {
    first_inobject_offset = map->GetInObjectPropertyOffset(0);
  } else {
    first_inobject_offset = FixedArray::kHeaderSize;
    property_index -= inobject_properties;
  }
  return FieldIndex(is_inobject,
                    property_index + first_inobject_offset / kPointerSize,
                    is_double, inobject_properties, first_inobject_offset);
}


inline FieldIndex FieldIndex::ForLoadByFieldIndex(Map* map, int orig_index) {
  int field_index = orig_index;
  int is_inobject = true;
  bool is_double = field_index & 1;
  int first_inobject_offset = 0;
  field_index >>= 1;
  if (field_index < 0) {
    field_index = -(field_index + 1);
    is_inobject = false;
    first_inobject_offset = FixedArray::kHeaderSize;
    field_index += FixedArray::kHeaderSize / kPointerSize;
  } else {
    first_inobject_offset = map->GetInObjectPropertyOffset(0);
    field_index += JSObject::kHeaderSize / kPointerSize;
  }
  return FieldIndex(is_inobject, field_index, is_double,
                    map->inobject_properties(), first_inobject_offset);
}


inline FieldIndex FieldIndex::ForDescriptor(Map* map, int descriptor_index) {
  PropertyDetails details =
      map->instance_descriptors()->GetDetails(descriptor_index);
  int field_index =
      map->instance_descriptors()->GetFieldIndex(descriptor_index);
  return ForPropertyIndex(map, field_index,
                          details.representation().IsDouble());
}


} }  // namespace v8::internal

#endif
