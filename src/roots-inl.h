// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ROOTS_INL_H_
#define V8_ROOTS_INL_H_

#include "src/roots.h"

#include "src/heap/heap-inl.h"
#include "src/objects/api-callbacks.h"

namespace v8 {

namespace internal {

ReadOnlyRoots::ReadOnlyRoots(Isolate* isolate) : heap_(isolate->heap()) {}

#define ROOT_ACCESSOR(type, name, CamelName)                        \
  type* ReadOnlyRoots::name() {                                     \
    return type::cast(heap_->roots_[RootIndex::k##CamelName]);      \
  }                                                                 \
  Handle<type> ReadOnlyRoots::name##_handle() {                     \
    return Handle<type>(                                            \
        bit_cast<type**>(&heap_->roots_[RootIndex::k##CamelName])); \
  }

STRONG_READ_ONLY_ROOT_LIST(ROOT_ACCESSOR)
INTERNALIZED_STRING_ROOT_LIST(ROOT_ACCESSOR)
PRIVATE_SYMBOL_ROOT_LIST(ROOT_ACCESSOR)
PUBLIC_SYMBOL_ROOT_LIST(ROOT_ACCESSOR)
WELL_KNOWN_SYMBOL_ROOT_LIST(ROOT_ACCESSOR)
STRUCT_MAPS_LIST(ROOT_ACCESSOR)
ALLOCATION_SITE_MAPS_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

FixedTypedArrayBase* ReadOnlyRoots::EmptyFixedTypedArrayForMap(const Map* map) {
  // TODO(delphick): All of these empty fixed type arrays are in RO_SPACE so
  // this the method below can be moved into ReadOnlyRoots.
  return heap_->EmptyFixedTypedArrayForMap(map);
}

}  // namespace internal

}  // namespace v8

#endif  // V8_ROOTS_INL_H_
