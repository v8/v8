// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_OBJECT_ALLOCATOR_INL_H_
#define V8_HEAP_CPPGC_OBJECT_ALLOCATOR_INL_H_

#include "src/heap/cppgc/object-allocator.h"

#include <new>

#include "src/base/logging.h"
#include "src/heap/cppgc/heap-object-header-inl.h"
#include "src/heap/cppgc/heap-object-header.h"

namespace cppgc {
namespace internal {

void* ObjectAllocator::AllocateObject(size_t size, GCInfoIndex gcinfo) {
  const size_t allocation_size =
      RoundUp(size + sizeof(HeapObjectHeader), kAllocationGranularity);
  const RawHeap::SpaceType type = GetSpaceIndexForSize(allocation_size);
  return AllocateObjectOnSpace(NormalPageSpace::From(raw_heap_->Space(type)),
                               allocation_size, gcinfo);
}

// static
inline RawHeap::SpaceType ObjectAllocator::GetSpaceIndexForSize(size_t size) {
  if (size < 64) {
    if (size < 32) return RawHeap::SpaceType::kNormal1;
    return RawHeap::SpaceType::kNormal2;
  }
  if (size < 128) return RawHeap::SpaceType::kNormal3;
  return RawHeap::SpaceType::kNormal4;
}

void* ObjectAllocator::AllocateObjectOnSpace(NormalPageSpace* space,
                                             size_t size, GCInfoIndex gcinfo) {
  DCHECK_LT(0u, gcinfo);
  NormalPageSpace::LinearAllocationBuffer& current_lab =
      space->linear_allocation_buffer();
  if (current_lab.size() < size) {
    return OutOfLineAllocate(space, size, gcinfo);
  }
  auto* header =
      new (current_lab.Allocate(size)) HeapObjectHeader(size, gcinfo);
  return header->Payload();
}
}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_OBJECT_ALLOCATOR_INL_H_
