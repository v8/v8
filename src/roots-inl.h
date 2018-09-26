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

V8_INLINE bool operator<(RootIndex lhs, RootIndex rhs) {
  typedef typename std::underlying_type<RootIndex>::type type;
  return static_cast<type>(lhs) < static_cast<type>(rhs);
}

V8_INLINE RootIndex operator++(RootIndex& index) {
  typedef typename std::underlying_type<RootIndex>::type type;
  index = static_cast<RootIndex>(static_cast<type>(index) + 1);
  return index;
}

ReadOnlyRoots::ReadOnlyRoots(Isolate* isolate) : heap_(isolate->heap()) {}

#define ROOT_ACCESSOR(type, name, CamelName)                        \
  type* ReadOnlyRoots::name() {                                     \
    return type::cast(heap_->roots_[RootIndex::k##CamelName]);      \
  }                                                                 \
  Handle<type> ReadOnlyRoots::name##_handle() {                     \
    return Handle<type>(                                            \
        bit_cast<type**>(&heap_->roots_[RootIndex::k##CamelName])); \
  }

READ_ONLY_ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

FixedTypedArrayBase* ReadOnlyRoots::EmptyFixedTypedArrayForMap(const Map* map) {
  // TODO(delphick): All of these empty fixed type arrays are in RO_SPACE so
  // this the method below can be moved into ReadOnlyRoots.
  return heap_->EmptyFixedTypedArrayForMap(map);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ROOTS_INL_H_
