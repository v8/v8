// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/evacuation-allocator.h"

#include "src/heap/main-allocator-inl.h"

namespace v8 {
namespace internal {

EvacuationAllocator::EvacuationAllocator(
    Heap* heap, CompactionSpaceKind compaction_space_kind)
    : heap_(heap),
      new_space_(heap->new_space()),
      compaction_spaces_(heap, compaction_space_kind),
      new_space_lab_(LocalAllocationBuffer::InvalidBuffer()),
      lab_allocation_will_fail_(false) {
  if (new_space_) {
    DCHECK(!heap_->allocator()->new_space_allocator()->IsLabValid());
    new_space_allocator_ = heap_->allocator()->new_space_allocator();
  }

  old_space_allocator_.emplace(heap, compaction_spaces_.Get(OLD_SPACE),
                               MainAllocator::Context::kGC);
  code_space_allocator_.emplace(heap, compaction_spaces_.Get(CODE_SPACE),
                                MainAllocator::Context::kGC);
  shared_space_allocator_.emplace(heap, compaction_spaces_.Get(SHARED_SPACE),
                                  MainAllocator::Context::kGC);
  trusted_space_allocator_.emplace(heap, compaction_spaces_.Get(TRUSTED_SPACE),
                                   MainAllocator::Context::kGC);
}

AllocationResult EvacuationAllocator::AllocateInNewSpaceSynchronized(
    int size_in_bytes, AllocationAlignment alignment) {
  base::MutexGuard guard(new_space_->mutex());
  return new_space_allocator()->AllocateRaw(size_in_bytes, alignment,
                                            AllocationOrigin::kGC);
}

bool EvacuationAllocator::NewLocalAllocationBuffer() {
  if (lab_allocation_will_fail_) return false;
  AllocationResult result =
      AllocateInNewSpaceSynchronized(kLabSize, kTaggedAligned);
  if (result.IsFailure()) {
    lab_allocation_will_fail_ = true;
    return false;
  }
  LocalAllocationBuffer saved_lab = std::move(new_space_lab_);
  new_space_lab_ = LocalAllocationBuffer::FromResult(heap_, result, kLabSize);
  DCHECK(new_space_lab_.IsValid());
  if (!new_space_lab_.TryMerge(&saved_lab)) {
    saved_lab.CloseAndMakeIterable();
  }
  return true;
}

void EvacuationAllocator::FreeLast(AllocationSpace space,
                                   Tagged<HeapObject> object, int object_size) {
  object_size = ALIGN_TO_ALLOCATION_ALIGNMENT(object_size);
  switch (space) {
    case NEW_SPACE:
      FreeLastInNewSpace(object, object_size);
      return;
    case OLD_SPACE:
      FreeLastInCompactionSpace(old_space_allocator(), object, object_size);
      return;
    case SHARED_SPACE:
      FreeLastInCompactionSpace(shared_space_allocator(), object, object_size);
      return;
    default:
      // Only new and old space supported.
      UNREACHABLE();
  }
}

void EvacuationAllocator::FreeLastInNewSpace(Tagged<HeapObject> object,
                                             int object_size) {
  if (!new_space_lab_.TryFreeLast(object, object_size)) {
    // We couldn't free the last object so we have to write a proper filler.
    heap_->CreateFillerObjectAt(object.address(), object_size);
  }
}

void EvacuationAllocator::FreeLastInCompactionSpace(MainAllocator* allocator,
                                                    Tagged<HeapObject> object,
                                                    int object_size) {
  if (!allocator->TryFreeLast(object.address(), object_size)) {
    // We couldn't free the last object so we have to write a proper filler.
    heap_->CreateFillerObjectAt(object.address(), object_size);
  }
}

void EvacuationAllocator::Finalize() {
  // Give back remaining LAB space if this EvacuationAllocator's new space LAB
  // sits right next to new space allocation top.
  const LinearAllocationArea info = new_space_lab_.CloseAndMakeIterable();
  if (new_space_) {
    new_space_allocator()->MaybeFreeUnusedLab(info);
    new_space_allocator()->FreeLinearAllocationArea();
  }

  old_space_allocator()->FreeLinearAllocationArea();
  heap_->old_space()->MergeCompactionSpace(compaction_spaces_.Get(OLD_SPACE));

  code_space_allocator()->FreeLinearAllocationArea();
  heap_->code_space()->MergeCompactionSpace(compaction_spaces_.Get(CODE_SPACE));

  if (heap_->shared_space()) {
    shared_space_allocator()->FreeLinearAllocationArea();
    heap_->shared_space()->MergeCompactionSpace(
        compaction_spaces_.Get(SHARED_SPACE));
  }

  trusted_space_allocator()->FreeLinearAllocationArea();
  heap_->trusted_space()->MergeCompactionSpace(
      compaction_spaces_.Get(TRUSTED_SPACE));
}

}  // namespace internal
}  // namespace v8
