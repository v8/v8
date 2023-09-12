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

  if (v8_flags.trace_allocations_origins) {
    space_->UpdateAllocationOrigins(origin);
  }

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

  if (v8_flags.trace_allocations_origins) {
    space_->UpdateAllocationOrigins(origin);
  }

  space_->InvokeAllocationObservers(result.ToAddress(), size_in_bytes,
                                    aligned_size_in_bytes, max_aligned_size);

  return result;
}

}  // namespace internal
}  // namespace v8
