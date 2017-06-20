// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_HASH_TABLE_INL_H_
#define V8_OBJECTS_HASH_TABLE_INL_H_

#include "src/objects/hash-table.h"

namespace v8 {
namespace internal {

template <typename Derived, typename Shape>
uint32_t HashTable<Derived, Shape>::Hash(Key key) {
  if (Shape::UsesSeed) {
    return Shape::SeededHash(key, GetHeap()->HashSeed());
  } else {
    return Shape::Hash(key);
  }
}

template <typename Derived, typename Shape>
uint32_t HashTable<Derived, Shape>::HashForObject(Object* object) {
  if (Shape::UsesSeed) {
    return Shape::SeededHashForObject(GetHeap()->HashSeed(), object);
  } else {
    return Shape::HashForObject(object);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_HASH_TABLE_INL_H_
