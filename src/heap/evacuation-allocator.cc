// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/evacuation-allocator.h"

#include "src/heap/main-allocator-inl.h"

namespace v8 {
namespace internal {

EvacuationAllocator::EvacuationAllocator(Heap* heap)
    : heap_(heap), new_space_(heap->new_space()) {
  if (new_space_) {
    DCHECK(!heap_->allocator()->new_space_allocator()->IsLabValid());
    new_space_allocator_.emplace(heap, new_space_, MainAllocator::kInGC);
  }

  old_space_allocator_.emplace(heap, heap->old_space(), MainAllocator::kInGC);
  code_space_allocator_.emplace(heap, heap->code_space(), MainAllocator::kInGC);
  if (heap->shared_space()) {
    shared_space_allocator_.emplace(heap, heap->shared_space(),
                                    MainAllocator::kInGC);
  }
  trusted_space_allocator_.emplace(heap, heap->trusted_space(),
                                   MainAllocator::kInGC);
}

void EvacuationAllocator::FreeLast(AllocationSpace space,
                                   Tagged<HeapObject> object, int object_size) {
  object_size = ALIGN_TO_ALLOCATION_ALIGNMENT(object_size);
  switch (space) {
    case NEW_SPACE:
      FreeLastInMainAllocator(new_space_allocator(), object, object_size);
      return;
    case OLD_SPACE:
      FreeLastInMainAllocator(old_space_allocator(), object, object_size);
      return;
    case SHARED_SPACE:
      FreeLastInMainAllocator(shared_space_allocator(), object, object_size);
      return;
    default:
      // Only new and old space supported.
      UNREACHABLE();
  }
}

void EvacuationAllocator::FreeLastInMainAllocator(MainAllocator* allocator,
                                                  Tagged<HeapObject> object,
                                                  int object_size) {
  if (!allocator->TryFreeLast(object.address(), object_size)) {
    // We couldn't free the last object so we have to write a proper filler.
    heap_->CreateFillerObjectAt(object.address(), object_size);
  }
}

void EvacuationAllocator::Finalize() {
  if (new_space_) {
    new_space_allocator()->FreeLinearAllocationArea();
  }

  old_space_allocator()->FreeLinearAllocationArea();
  code_space_allocator()->FreeLinearAllocationArea();

  if (heap_->shared_space()) {
    shared_space_allocator()->FreeLinearAllocationArea();
  }

  trusted_space_allocator()->FreeLinearAllocationArea();
}

}  // namespace internal
}  // namespace v8
