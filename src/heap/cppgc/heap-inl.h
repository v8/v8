// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_HEAP_INL_H_
#define V8_HEAP_CPPGC_HEAP_INL_H_

#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/object-allocator-inl.h"

namespace cppgc {
namespace internal {

void* Heap::Allocate(size_t size, GCInfoIndex index) {
  DCHECK(is_allocation_allowed());
  return object_allocator_.AllocateObject(size, index);
}

void* Heap::Allocate(size_t size, GCInfoIndex index,
                     CustomSpaceIndex space_index) {
  DCHECK(is_allocation_allowed());
  return object_allocator_.AllocateObject(size, index, space_index);
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_HEAP_INL_H_
