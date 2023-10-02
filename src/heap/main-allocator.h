// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MAIN_ALLOCATOR_H_
#define V8_HEAP_MAIN_ALLOCATOR_H_

#include "src/base/optional.h"
#include "src/common/globals.h"
#include "src/heap/allocation-observer.h"
#include "src/heap/allocation-result.h"
#include "src/heap/linear-allocation-area.h"
#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

class Heap;
class SpaceWithLinearArea;

class MainAllocator {
 public:
  enum SupportsExtendingLAB { kYes, kNo };

  MainAllocator(Heap* heap, SpaceWithLinearArea* space,
                CompactionSpaceKind compaction_space_kind,
                SupportsExtendingLAB supports_extending_lab);

  // This constructor allows to pass in the address of a LinearAllocationArea.
  MainAllocator(Heap* heap, SpaceWithLinearArea* space,
                CompactionSpaceKind compaction_space_kind,
                SupportsExtendingLAB supports_extending_lab,
                LinearAllocationArea& allocation_info);

  // Returns the allocation pointer in this space.
  Address start() const { return allocation_info_.start(); }
  Address top() const { return allocation_info_.top(); }
  Address limit() const { return allocation_info_.limit(); }

  // The allocation top address.
  Address* allocation_top_address() const {
    return allocation_info_.top_address();
  }

  // The allocation limit address.
  Address* allocation_limit_address() const {
    return allocation_info_.limit_address();
  }

  Address original_top() const {
    return lab_origins_handle_.top_and_limit().first;
  }

  Address original_limit() const {
    return lab_origins_handle_.top_and_limit().second;
  }

  void MoveOriginalTopForward();
  V8_EXPORT_PRIVATE void ResetLab(Address start, Address end,
                                  Address extended_end);
  V8_EXPORT_PRIVATE bool IsPendingAllocation(Address object_address);
  void MaybeFreeUnusedLab(LinearAllocationArea lab);

  LinearAllocationArea& allocation_info() { return allocation_info_; }

  const LinearAllocationArea& allocation_info() const {
    return allocation_info_;
  }

  AllocationCounter& allocation_counter() { return allocation_counter_; }

  const AllocationCounter& allocation_counter() const {
    return allocation_counter_;
  }

  V8_WARN_UNUSED_RESULT V8_INLINE AllocationResult
  AllocateRaw(int size_in_bytes, AllocationAlignment alignment,
              AllocationOrigin origin);

  V8_WARN_UNUSED_RESULT V8_EXPORT_PRIVATE AllocationResult
  AllocateRawForceAlignmentForTesting(int size_in_bytes,
                                      AllocationAlignment alignment,
                                      AllocationOrigin);

  V8_EXPORT_PRIVATE void AddAllocationObserver(AllocationObserver* observer);
  V8_EXPORT_PRIVATE void RemoveAllocationObserver(AllocationObserver* observer);
  void PauseAllocationObservers();
  void ResumeAllocationObservers();

  V8_EXPORT_PRIVATE void AdvanceAllocationObservers();
  V8_EXPORT_PRIVATE void InvokeAllocationObservers(Address soon_object,
                                                   size_t size_in_bytes,
                                                   size_t aligned_size_in_bytes,
                                                   size_t allocation_size);

  void MarkLabStartInitialized();

  void MakeLinearAllocationAreaIterable();

  V8_INLINE bool TryFreeLast(Address object_address, int object_size);

  // When allocation observers are active we may use a lower limit to allow the
  // observers to 'interrupt' earlier than the natural limit. Given a linear
  // area bounded by [start, end), this function computes the limit to use to
  // allow proper observation based on existing observers. min_size specifies
  // the minimum size that the limited area should have.
  Address ComputeLimit(Address start, Address end, size_t min_size) const;

  LabOriginalLimits::LabHandle& lab_origins_handle() {
    return lab_origins_handle_;
  }

#if DEBUG
  void Verify() const;
#endif  // DEBUG

  // Checks whether the LAB is currently in use.
  V8_INLINE bool IsLabValid() { return allocation_info_.top() != kNullAddress; }

  void UpdateInlineAllocationLimit();

  V8_EXPORT_PRIVATE void FreeLinearAllocationArea();

  void ExtendLAB(Address limit);

  bool supports_extending_lab() const {
    return supports_extending_lab_ == SupportsExtendingLAB::kYes;
  }

 private:
  // Allocates an object from the linear allocation area. Assumes that the
  // linear allocation area is large enough to fit the object.
  V8_WARN_UNUSED_RESULT V8_INLINE AllocationResult
  AllocateFastUnaligned(int size_in_bytes, AllocationOrigin origin);

  // Tries to allocate an aligned object from the linear allocation area.
  // Returns nullptr if the linear allocation area does not fit the object.
  // Otherwise, returns the object pointer and writes the allocation size
  // (object size + alignment filler size) to the result_aligned_size_in_bytes.
  V8_WARN_UNUSED_RESULT V8_INLINE AllocationResult
  AllocateFastAligned(int size_in_bytes, int* result_aligned_size_in_bytes,
                      AllocationAlignment alignment, AllocationOrigin origin);

  // Slow path of allocation function
  V8_WARN_UNUSED_RESULT V8_EXPORT_PRIVATE AllocationResult
  AllocateRawSlow(int size_in_bytes, AllocationAlignment alignment,
                  AllocationOrigin origin);

  // Allocate the requested number of bytes in the space if possible, return a
  // failure object if not.
  V8_WARN_UNUSED_RESULT AllocationResult AllocateRawSlowUnaligned(
      int size_in_bytes, AllocationOrigin origin = AllocationOrigin::kRuntime);

  // Allocate the requested number of bytes in the space double aligned if
  // possible, return a failure object if not.
  V8_WARN_UNUSED_RESULT AllocationResult
  AllocateRawSlowAligned(int size_in_bytes, AllocationAlignment alignment,
                         AllocationOrigin origin = AllocationOrigin::kRuntime);

  bool EnsureAllocation(int size_in_bytes, AllocationAlignment alignment,
                        AllocationOrigin origin, int* out_max_aligned_size);

  int ObjectAlignment() const;

  AllocationSpace identity() const;

  bool SupportsAllocationObserver() const { return !is_compaction_space(); }

  bool is_compaction_space() const {
    return compaction_space_kind_ != CompactionSpaceKind::kNone;
  }

  Heap* heap() const { return heap_; }

  Heap* heap_;
  SpaceWithLinearArea* space_;
  CompactionSpaceKind compaction_space_kind_;
  const SupportsExtendingLAB supports_extending_lab_;

  AllocationCounter allocation_counter_;
  LinearAllocationArea& allocation_info_;
  // This memory is used if no LinearAllocationArea& is passed in as argument.
  LinearAllocationArea owned_allocation_info_;
  LabOriginalLimits::LabHandle lab_origins_handle_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MAIN_ALLOCATOR_H_
