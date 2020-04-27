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

class V8_EXPORT_PRIVATE ObjectAllocator final {
 public:
  explicit ObjectAllocator(RawHeap* heap);

  inline void* AllocateObject(size_t size, GCInfoIndex gcinfo);

 private:
  inline static RawHeap::SpaceType GetSpaceIndexForSize(size_t size);

  inline void* AllocateObjectOnSpace(NormalPageSpace* space, size_t size,
                                     GCInfoIndex gcinfo);
  void* OutOfLineAllocate(NormalPageSpace*, size_t, GCInfoIndex);
  void* AllocateFromFreeList(NormalPageSpace*, size_t, GCInfoIndex);

  RawHeap* raw_heap_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_OBJECT_ALLOCATOR_H_
