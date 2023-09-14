// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/main-allocator-inl.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

MainAllocator::MainAllocator(Heap* heap, SpaceWithLinearArea* space,
                             AllocationCounter& allocation_counter,
                             LinearAllocationArea& allocation_info,
                             LinearAreaOriginalData& linear_area_original_data)
    : heap_(heap),
      space_(space),
      allocation_counter_(allocation_counter),
      allocation_info_(allocation_info),
      linear_area_original_data_(linear_area_original_data) {
  USE(heap_);
  USE(space_);
  USE(allocation_counter_);
}

AllocationResult MainAllocator::AllocateRawForceAlignmentForTesting(
    int size_in_bytes, AllocationAlignment alignment, AllocationOrigin origin) {
  size_in_bytes = ALIGN_TO_ALLOCATION_ALIGNMENT(size_in_bytes);

  AllocationResult result =
      AllocateFastAligned(size_in_bytes, nullptr, alignment, origin);

  return V8_UNLIKELY(result.IsFailure())
             ? AllocateRawSlowAligned(size_in_bytes, alignment, origin)
             : result;
}

void MainAllocator::AddAllocationObserver(AllocationObserver* observer) {
  if (!allocation_counter().IsStepInProgress()) {
    AdvanceAllocationObservers();
    allocation_counter().AddAllocationObserver(observer);
    space_->UpdateInlineAllocationLimit();
  } else {
    allocation_counter().AddAllocationObserver(observer);
  }
}

void MainAllocator::RemoveAllocationObserver(AllocationObserver* observer) {
  if (!allocation_counter().IsStepInProgress()) {
    AdvanceAllocationObservers();
    allocation_counter().RemoveAllocationObserver(observer);
    space_->UpdateInlineAllocationLimit();
  } else {
    allocation_counter().RemoveAllocationObserver(observer);
  }
}

void MainAllocator::PauseAllocationObservers() { AdvanceAllocationObservers(); }

void MainAllocator::ResumeAllocationObservers() {
  MarkLabStartInitialized();
  space_->UpdateInlineAllocationLimit();
}

void MainAllocator::AdvanceAllocationObservers() {
  if (allocation_info().top() &&
      allocation_info().start() != allocation_info().top()) {
    if (heap()->IsAllocationObserverActive()) {
      allocation_counter().AdvanceAllocationObservers(
          allocation_info().top() - allocation_info().start());
    }
    MarkLabStartInitialized();
  }
}

void MainAllocator::MarkLabStartInitialized() {
  allocation_info().ResetStart();
  if (identity() == NEW_SPACE) {
    heap()->new_space()->MoveOriginalTopForward();

#if DEBUG
    heap()->VerifyNewSpaceTop();
#endif
  }
}

// Perform an allocation step when the step is reached. size_in_bytes is the
// actual size needed for the object (required for InvokeAllocationObservers).
// aligned_size_in_bytes is the size of the object including the filler right
// before it to reach the right alignment (required to DCHECK the start of the
// object). allocation_size is the size of the actual allocation which needs to
// be used for the accounting. It can be different from aligned_size_in_bytes in
// PagedSpace::AllocateRawAligned, where we have to overallocate in order to be
// able to align the allocation afterwards.
void MainAllocator::InvokeAllocationObservers(Address soon_object,
                                              size_t size_in_bytes,
                                              size_t aligned_size_in_bytes,
                                              size_t allocation_size) {
  DCHECK_LE(size_in_bytes, aligned_size_in_bytes);
  DCHECK_LE(aligned_size_in_bytes, allocation_size);
  DCHECK(size_in_bytes == aligned_size_in_bytes ||
         aligned_size_in_bytes == allocation_size);

  if (!space_->SupportsAllocationObserver() ||
      !heap()->IsAllocationObserverActive())
    return;

  if (allocation_size >= allocation_counter().NextBytes()) {
    // Only the first object in a LAB should reach the next step.
    DCHECK_EQ(soon_object, allocation_info().start() + aligned_size_in_bytes -
                               size_in_bytes);

    // Right now the LAB only contains that one object.
    DCHECK_EQ(allocation_info().top() + allocation_size - aligned_size_in_bytes,
              allocation_info().limit());

    // Ensure that there is a valid object
    heap_->CreateFillerObjectAt(soon_object, static_cast<int>(size_in_bytes));

#if DEBUG
    // Ensure that allocation_info_ isn't modified during one of the
    // AllocationObserver::Step methods.
    LinearAllocationArea saved_allocation_info = allocation_info();
#endif

    // Run AllocationObserver::Step through the AllocationCounter.
    allocation_counter().InvokeAllocationObservers(soon_object, size_in_bytes,
                                                   allocation_size);

    // Ensure that start/top/limit didn't change.
    DCHECK_EQ(saved_allocation_info.start(), allocation_info().start());
    DCHECK_EQ(saved_allocation_info.top(), allocation_info().top());
    DCHECK_EQ(saved_allocation_info.limit(), allocation_info().limit());
  }

  DCHECK_LT(allocation_info().limit() - allocation_info().start(),
            allocation_counter().NextBytes());
}

AllocationResult MainAllocator::AllocateRawSlow(int size_in_bytes,
                                                AllocationAlignment alignment,
                                                AllocationOrigin origin) {
  AllocationResult result =
      USE_ALLOCATION_ALIGNMENT_BOOL && alignment != kTaggedAligned
          ? AllocateRawSlowAligned(size_in_bytes, alignment, origin)
          : AllocateRawSlowUnaligned(size_in_bytes, origin);
  return result;
}

AllocationResult MainAllocator::AllocateRawSlowUnaligned(
    int size_in_bytes, AllocationOrigin origin) {
  DCHECK(!v8_flags.enable_third_party_heap);
  int max_aligned_size;
  if (!space_->EnsureAllocation(size_in_bytes, kTaggedAligned, origin,
                                &max_aligned_size)) {
    return AllocationResult::Failure();
  }

  DCHECK_EQ(max_aligned_size, size_in_bytes);
  DCHECK_LE(allocation_info().start(), allocation_info().top());

  AllocationResult result = AllocateFastUnaligned(size_in_bytes, origin);
  DCHECK(!result.IsFailure());

  space_->InvokeAllocationObservers(result.ToAddress(), size_in_bytes,
                                    size_in_bytes, size_in_bytes);

  return result;
}

AllocationResult MainAllocator::AllocateRawSlowAligned(
    int size_in_bytes, AllocationAlignment alignment, AllocationOrigin origin) {
  DCHECK(!v8_flags.enable_third_party_heap);
  int max_aligned_size;
  if (!space_->EnsureAllocation(size_in_bytes, alignment, origin,
                                &max_aligned_size)) {
    return AllocationResult::Failure();
  }

  DCHECK_GE(max_aligned_size, size_in_bytes);
  DCHECK_LE(allocation_info().start(), allocation_info().top());

  int aligned_size_in_bytes;

  AllocationResult result = AllocateFastAligned(
      size_in_bytes, &aligned_size_in_bytes, alignment, origin);
  DCHECK_GE(max_aligned_size, aligned_size_in_bytes);
  DCHECK(!result.IsFailure());

  space_->InvokeAllocationObservers(result.ToAddress(), size_in_bytes,
                                    aligned_size_in_bytes, max_aligned_size);

  return result;
}

void MainAllocator::MakeLinearAllocationAreaIterable() {
  Address current_top = top();
  Address current_limit = original_limit_relaxed();
  DCHECK_GE(current_limit, limit());
  if (current_top != kNullAddress && current_top != current_limit) {
    heap_->CreateFillerObjectAt(current_top,
                                static_cast<int>(current_limit - current_top));
  }
}

void MainAllocator::MarkLinearAllocationAreaBlack() {
  DCHECK(heap()->incremental_marking()->black_allocation());
  Address current_top = top();
  Address current_limit = limit();
  if (current_top != kNullAddress && current_top != current_limit) {
    Page::FromAllocationAreaAddress(current_top)
        ->CreateBlackArea(current_top, current_limit);
  }
}

void MainAllocator::UnmarkLinearAllocationArea() {
  Address current_top = top();
  Address current_limit = limit();
  if (current_top != kNullAddress && current_top != current_limit) {
    Page::FromAllocationAreaAddress(current_top)
        ->DestroyBlackArea(current_top, current_limit);
  }
}

AllocationSpace MainAllocator::identity() const { return space_->identity(); }

}  // namespace internal
}  // namespace v8
