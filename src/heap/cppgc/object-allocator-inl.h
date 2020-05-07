// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_OBJECT_ALLOCATOR_INL_H_
#define V8_HEAP_CPPGC_OBJECT_ALLOCATOR_INL_H_

#include <new>

#include "src/base/logging.h"
#include "src/heap/cppgc/heap-object-header-inl.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/object-allocator.h"
#include "src/heap/cppgc/sanitizers.h"

namespace cppgc {
namespace internal {

void* ObjectAllocator::AllocateObject(size_t size, GCInfoIndex gcinfo) {
  const size_t allocation_size =
      RoundUp(size + sizeof(HeapObjectHeader), kAllocationGranularity);
  const RawHeap::RegularSpaceType type =
      GetInitialSpaceIndexForSize(allocation_size);
  return AllocateObjectOnSpace(NormalPageSpace::From(raw_heap_->Space(type)),
                               allocation_size, gcinfo);
}

void* ObjectAllocator::AllocateObject(size_t size, GCInfoIndex gcinfo,
                                      CustomSpaceIndex space_index) {
  const size_t allocation_size =
      RoundUp(size + sizeof(HeapObjectHeader), kAllocationGranularity);
  const size_t internal_space_index =
      raw_heap_->SpaceIndexForCustomSpace(space_index);
  return AllocateObjectOnSpace(
      NormalPageSpace::From(raw_heap_->Space(internal_space_index)),
      allocation_size, gcinfo);
}

// static
inline RawHeap::RegularSpaceType ObjectAllocator::GetInitialSpaceIndexForSize(
    size_t size) {
  if (size < 64) {
    if (size < 32) return RawHeap::RegularSpaceType::kNormal1;
    return RawHeap::RegularSpaceType::kNormal2;
  }
  if (size < 128) return RawHeap::RegularSpaceType::kNormal3;
  return RawHeap::RegularSpaceType::kNormal4;
}

void* ObjectAllocator::AllocateObjectOnSpace(NormalPageSpace* space,
                                             size_t size, GCInfoIndex gcinfo) {
  DCHECK_LT(0u, gcinfo);

  NormalPageSpace::LinearAllocationBuffer& current_lab =
      space->linear_allocation_buffer();
  if (current_lab.size() < size) {
    return OutOfLineAllocate(space, size, gcinfo);
  }

  void* raw = current_lab.Allocate(size);
  SET_MEMORY_ACCESIBLE(raw, size);
  auto* header = new (raw) HeapObjectHeader(size, gcinfo);
  return header->Payload();
}
}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_OBJECT_ALLOCATOR_INL_H_
