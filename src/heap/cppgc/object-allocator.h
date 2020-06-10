// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_OBJECT_ALLOCATOR_H_
#define V8_HEAP_CPPGC_OBJECT_ALLOCATOR_H_

#include "include/cppgc/internal/gc-info.h"
#include "src/heap/cppgc/heap-space.h"
#include "src/heap/cppgc/raw-heap.h"

namespace cppgc {
namespace internal {

class StatsCollector;
class PageBackend;

class V8_EXPORT_PRIVATE ObjectAllocator final {
 public:
  ObjectAllocator(RawHeap* heap, PageBackend* page_backend,
                  StatsCollector* stats_collector);

  inline void* AllocateObject(size_t size, GCInfoIndex gcinfo);
  inline void* AllocateObject(size_t size, GCInfoIndex gcinfo,
                              CustomSpaceIndex space_index);

  void ResetLinearAllocationBuffers();

 private:
  // Returns the initially tried SpaceType to allocate an object of |size| bytes
  // on. Returns the largest regular object size bucket for large objects.
  inline static RawHeap::RegularSpaceType GetInitialSpaceIndexForSize(
      size_t size);

  inline void* AllocateObjectOnSpace(NormalPageSpace* space, size_t size,
                                     GCInfoIndex gcinfo);
  void* OutOfLineAllocate(NormalPageSpace*, size_t, GCInfoIndex);
  void* OutOfLineAllocateImpl(NormalPageSpace*, size_t, GCInfoIndex);
  void* AllocateFromFreeList(NormalPageSpace*, size_t, GCInfoIndex);

  RawHeap* raw_heap_;
  PageBackend* page_backend_;
  StatsCollector* stats_collector_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_OBJECT_ALLOCATOR_H_
