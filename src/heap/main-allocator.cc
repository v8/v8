// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/main-allocator.h"

#include "src/base/optional.h"
#include "src/common/globals.h"
#include "src/execution/vm-state-inl.h"
#include "src/execution/vm-state.h"
#include "src/heap/free-list-inl.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/heap.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/main-allocator-inl.h"
#include "src/heap/new-spaces.h"
#include "src/heap/page-inl.h"
#include "src/heap/paged-spaces.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

MainAllocator::MainAllocator(Heap* heap, SpaceWithLinearArea* space,
                             LinearAllocationArea& allocation_info)
    : local_heap_(heap->main_thread_local_heap()),
      heap_(heap),
      space_(space),
      allocation_info_(allocation_info),
      allocator_policy_(space->CreateAllocatorPolicy(this)),
      supports_extending_lab_(allocator_policy_->SupportsExtendingLAB()) {
  CHECK_NOT_NULL(local_heap_);
  allocation_counter_.emplace();
  linear_area_original_data_.emplace();
}

MainAllocator::MainAllocator(Heap* heap, SpaceWithLinearArea* space)
    : local_heap_(heap->main_thread_local_heap()),
      heap_(heap),
      space_(space),
      allocation_info_(owned_allocation_info_),
      allocator_policy_(space->CreateAllocatorPolicy(this)),
      supports_extending_lab_(allocator_policy_->SupportsExtendingLAB()) {
  CHECK_NOT_NULL(local_heap_);
  allocation_counter_.emplace();
  linear_area_original_data_.emplace();
}

MainAllocator::MainAllocator(Heap* heap, SpaceWithLinearArea* space, InGCTag)
    : local_heap_(nullptr),
      heap_(heap),
      space_(space),
      allocation_info_(owned_allocation_info_),
      allocator_policy_(space->CreateAllocatorPolicy(this)),
      supports_extending_lab_(false) {
  DCHECK(!allocation_counter_.has_value());
  DCHECK(!linear_area_original_data_.has_value());
}

Address MainAllocator::AlignTopForTesting(AllocationAlignment alignment,
                                          int offset) {
  DCHECK(top());

  int filler_size = Heap::GetFillToAlign(top(), alignment);

  if (filler_size + offset) {
    heap_->CreateFillerObjectAt(top(), filler_size + offset);
    allocation_info().IncrementTop(filler_size + offset);
  }

  return top();
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

bool MainAllocator::IsBlackAllocationEnabled() const {
  return identity() != NEW_SPACE &&
         heap()->incremental_marking()->black_allocation();
}

void MainAllocator::AddAllocationObserver(AllocationObserver* observer) {
  // Adding an allocation observer may decrease the inline allocation limit, so
  // we check here that we don't have an existing LAB.
  CHECK(!allocation_counter().IsStepInProgress());
  DCHECK(!IsLabValid());
  allocation_counter().AddAllocationObserver(observer);
}

void MainAllocator::RemoveAllocationObserver(AllocationObserver* observer) {
  // AllocationObserver can remove themselves. So we can't CHECK here that no
  // allocation step is in progress. It is also okay if there are existing LABs
  // because removing an allocation observer can only increase the distance to
  // the next step.
  allocation_counter().RemoveAllocationObserver(observer);
}

void MainAllocator::PauseAllocationObservers() { DCHECK(!IsLabValid()); }

void MainAllocator::ResumeAllocationObservers() { DCHECK(!IsLabValid()); }

void MainAllocator::AdvanceAllocationObservers() {
  if (SupportsAllocationObserver() && allocation_info().top() &&
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
#if DEBUG
  Verify();
#endif
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

  if (!SupportsAllocationObserver() || !heap()->IsAllocationObserverActive())
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
  if (!EnsureAllocation(size_in_bytes, kTaggedAligned, origin)) {
    return AllocationResult::Failure();
  }

  AllocationResult result = AllocateFastUnaligned(size_in_bytes, origin);
  DCHECK(!result.IsFailure());

  InvokeAllocationObservers(result.ToAddress(), size_in_bytes, size_in_bytes,
                            size_in_bytes);

  return result;
}

AllocationResult MainAllocator::AllocateRawSlowAligned(
    int size_in_bytes, AllocationAlignment alignment, AllocationOrigin origin) {
  DCHECK(!v8_flags.enable_third_party_heap);
  if (!EnsureAllocation(size_in_bytes, alignment, origin)) {
    return AllocationResult::Failure();
  }

  int max_aligned_size = size_in_bytes + Heap::GetMaximumFillToAlign(alignment);
  int aligned_size_in_bytes;

  AllocationResult result = AllocateFastAligned(
      size_in_bytes, &aligned_size_in_bytes, alignment, origin);
  DCHECK_GE(max_aligned_size, aligned_size_in_bytes);
  DCHECK(!result.IsFailure());

  InvokeAllocationObservers(result.ToAddress(), size_in_bytes,
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
  DCHECK(IsBlackAllocationEnabled());
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

void MainAllocator::MoveOriginalTopForward() {
  DCHECK(!in_gc());
  base::SharedMutexGuard<base::kExclusive> guard(
      linear_area_original_data().linear_area_lock());
  DCHECK_GE(top(), linear_area_original_data().get_original_top_acquire());
  DCHECK_LE(top(), linear_area_original_data().get_original_limit_relaxed());
  linear_area_original_data().set_original_top_release(top());
}

void MainAllocator::ResetLab(Address start, Address end, Address extended_end) {
  DCHECK_LE(start, end);
  DCHECK_LE(end, extended_end);

  if (IsLabValid()) {
    BasicMemoryChunk::UpdateHighWaterMark(top());
  }

  allocation_info().Reset(start, end);

  if (!in_gc()) {
    base::SharedMutexGuard<base::kExclusive> guard(
        linear_area_original_data().linear_area_lock());
    linear_area_original_data().set_original_limit_relaxed(extended_end);
    linear_area_original_data().set_original_top_release(start);
  }
}

bool MainAllocator::IsPendingAllocation(Address object_address) {
  DCHECK(!in_gc());
  base::SharedMutexGuard<base::kShared> guard(
      linear_area_original_data().linear_area_lock());
  Address top = original_top_acquire();
  Address limit = original_limit_relaxed();
  DCHECK_LE(top, limit);
  return top && top <= object_address && object_address < limit;
}

bool MainAllocator::EnsureAllocation(int size_in_bytes,
                                     AllocationAlignment alignment,
                                     AllocationOrigin origin) {
#ifdef V8_RUNTIME_CALL_STATS
  base::Optional<RuntimeCallTimerScope> rcs_scope;
  if (is_main_thread()) {
    rcs_scope.emplace(heap()->isolate(),
                      RuntimeCallCounterId::kGC_Custom_SlowAllocateRaw);
  }
#endif  // V8_RUNTIME_CALL_STATS
  base::Optional<VMState<GC>> vmstate;
  if (is_main_thread()) {
    vmstate.emplace(heap()->isolate());
  }
  return allocator_policy_->EnsureAllocation(size_in_bytes, alignment, origin);
}

void MainAllocator::FreeLinearAllocationArea() {
  if (!IsLabValid()) return;

#if DEBUG
  Verify();
#endif  // DEBUG

  BasicMemoryChunk::UpdateHighWaterMark(top());
  allocator_policy_->FreeLinearAllocationArea();
}

void MainAllocator::ExtendLAB(Address limit) {
  DCHECK(supports_extending_lab());
  DCHECK_LE(limit, original_limit_relaxed());
  allocation_info().SetLimit(limit);
}

Address MainAllocator::ComputeLimit(Address start, Address end,
                                    size_t min_size) const {
  DCHECK_GE(end - start, min_size);

  // During GCs we always use the full LAB.
  if (heap()->IsInGC()) return end;

  if (!heap()->IsInlineAllocationEnabled()) {
    // LABs are disabled, so we fit the requested area exactly.
    return start + min_size;
  }

  // When LABs are enabled, pick the largest possible LAB size by default.
  size_t step_size = end - start;

  if (SupportsAllocationObserver() && heap()->IsAllocationObserverActive()) {
    // Ensure there are no unaccounted allocations.
    DCHECK_EQ(allocation_info().start(), allocation_info().top());

    size_t step = allocation_counter().NextBytes();
    DCHECK_NE(step, 0);
    // Generated code may allocate inline from the linear allocation area. To
    // make sure we can observe these allocations, we use a lower limit.
    size_t rounded_step = static_cast<size_t>(
        RoundDown(static_cast<int>(step - 1), ObjectAlignment()));
    step_size = std::min(step_size, rounded_step);
  }

  if (v8_flags.stress_marking) {
    step_size = std::min(step_size, static_cast<size_t>(64));
  }

  DCHECK_LE(start + step_size, end);
  return start + std::max(step_size, min_size);
}

#if DEBUG
void MainAllocator::Verify() const {
  // Ensure validity of LAB: start <= top.
  DCHECK_LE(allocation_info().start(), allocation_info().top());

  if (top()) {
    Page* page = Page::FromAllocationAreaAddress(top());
    // Can't compare owner directly because of new space semi spaces.
    DCHECK_EQ(page->owner_identity(), identity());
  }

  if (in_gc()) {
    DCHECK_LE(allocation_info().top(), allocation_info().limit());
  } else {
    // Ensure that original_top <= top <= limit <= original_limit.
    DCHECK_LE(linear_area_original_data().get_original_top_acquire(),
              allocation_info().top());
    DCHECK_LE(allocation_info().top(), allocation_info().limit());
    DCHECK_LE(allocation_info().limit(),
              linear_area_original_data().get_original_limit_relaxed());
  }
}
#endif  // DEBUG

bool MainAllocator::EnsureAllocationForTesting(int size_in_bytes,
                                               AllocationAlignment alignment,
                                               AllocationOrigin origin) {
  base::Optional<CodePageHeaderModificationScope> optional_scope;
  if (identity() == CODE_SPACE) {
    optional_scope.emplace("Slow allocation path writes to the page header.");
  }

  return EnsureAllocation(size_in_bytes, alignment, origin);
}

int MainAllocator::ObjectAlignment() const {
  if (identity() == CODE_SPACE) {
    return kCodeAlignment;
  } else if (V8_COMPRESS_POINTERS_8GB_BOOL) {
    return kObjectAlignment8GbHeap;
  } else {
    return kTaggedSize;
  }
}

AllocationSpace MainAllocator::identity() const { return space_->identity(); }

bool MainAllocator::is_main_thread() const {
  return !in_gc() && local_heap()->is_main_thread();
}

AllocatorPolicy::AllocatorPolicy(MainAllocator* allocator)
    : allocator_(allocator), heap_(allocator->heap()) {}

bool SemiSpaceNewSpaceAllocatorPolicy::EnsureAllocation(
    int size_in_bytes, AllocationAlignment alignment, AllocationOrigin origin) {
  base::Optional<base::MutexGuard> guard;
  if (allocator_->in_gc()) guard.emplace(space_->mutex());

  FreeLinearAllocationAreaUnsynchronized();

  base::Optional<std::pair<Address, Address>> allocation_result =
      space_->Allocate(size_in_bytes, alignment);
  if (!allocation_result) return false;

  Address start = allocation_result->first;
  Address end = allocation_result->second;

  int filler_size = Heap::GetFillToAlign(start, alignment);
  int aligned_size_in_bytes = size_in_bytes + filler_size;
  DCHECK_LE(start + aligned_size_in_bytes, end);

  Address limit;

  if (allocator_->in_gc()) {
    // During GC we allow multiple LABs in new space and since Allocate() above
    // returns the whole remaining page by default, we limit the size of the LAB
    // here.
    size_t used = std::max(aligned_size_in_bytes, kLabSizeInGC);
    limit = std::min(end, start + used);
  } else {
    limit = allocator_->ComputeLimit(start, end, aligned_size_in_bytes);
  }
  CHECK_LE(limit, end);

  if (limit != end) {
    space_->Free(limit, end);
  }

  allocator_->ResetLab(start, limit, limit);

  space_->to_space().AddRangeToActiveSystemPages(allocator_->top(),
                                                 allocator_->limit());
  return true;
}

void SemiSpaceNewSpaceAllocatorPolicy::FreeLinearAllocationArea() {
  if (!allocator_->IsLabValid()) return;

#if DEBUG
  allocator_->Verify();
#endif  // DEBUG

  base::Optional<base::MutexGuard> guard;
  if (allocator_->in_gc()) guard.emplace(space_->mutex());

  FreeLinearAllocationAreaUnsynchronized();
}

void SemiSpaceNewSpaceAllocatorPolicy::
    FreeLinearAllocationAreaUnsynchronized() {
  if (!allocator_->IsLabValid()) return;

  Address current_top = allocator_->top();
  Address current_limit = allocator_->limit();

  allocator_->AdvanceAllocationObservers();
  allocator_->ResetLab(kNullAddress, kNullAddress, kNullAddress);

  space_->Free(current_top, current_limit);
}

PagedNewSpaceAllocatorPolicy::PagedNewSpaceAllocatorPolicy(
    PagedNewSpace* space, MainAllocator* allocator)
    : AllocatorPolicy(allocator),
      space_(space),
      paged_space_allocator_policy_(
          new PagedSpaceAllocatorPolicy(space->paged_space(), allocator)) {}

bool PagedNewSpaceAllocatorPolicy::EnsureAllocation(
    int size_in_bytes, AllocationAlignment alignment, AllocationOrigin origin) {
  if (space_->paged_space()->last_lab_page_) {
    space_->paged_space()->last_lab_page_->DecreaseAllocatedLabSize(
        allocator_->limit() - allocator_->top());
    allocator_->ExtendLAB(allocator_->top());
    // No need to write a filler to the remaining lab because it will either be
    // reallocated if the lab can be extended or freed otherwise.
  }

  if (!paged_space_allocator_policy_->EnsureAllocation(size_in_bytes, alignment,
                                                       origin)) {
    if (!AddPageBeyondCapacity(size_in_bytes, origin)) {
      if (!WaitForSweepingForAllocation(size_in_bytes, origin)) {
        return false;
      }
    }
  }

  space_->paged_space()->last_lab_page_ =
      Page::FromAllocationAreaAddress(allocator_->top());
  DCHECK_NOT_NULL(space_->paged_space()->last_lab_page_);
  space_->paged_space()->last_lab_page_->IncreaseAllocatedLabSize(
      allocator_->limit() - allocator_->top());

  return true;
}

bool PagedNewSpaceAllocatorPolicy::WaitForSweepingForAllocation(
    int size_in_bytes, AllocationOrigin origin) {
  // This method should be called only when there are no more pages for main
  // thread to sweep.
  DCHECK(heap()->sweeper()->IsSweepingDoneForSpace(NEW_SPACE));
  if (!v8_flags.concurrent_sweeping || !heap()->sweeping_in_progress())
    return false;
  Sweeper* sweeper = heap()->sweeper();
  if (!sweeper->AreMinorSweeperTasksRunning() &&
      !sweeper->ShouldRefillFreelistForSpace(NEW_SPACE)) {
#if DEBUG
    for (Page* p : *space_) {
      DCHECK(p->SweepingDone());
      p->ForAllFreeListCategories(
          [space = space_->paged_space()](FreeListCategory* category) {
            DCHECK_IMPLIES(!category->is_empty(),
                           category->is_linked(space->free_list()));
          });
    }
#endif  // DEBUG
    // All pages are already swept and relinked to the free list
    return false;
  }
  // When getting here we know that any unswept new space page is currently
  // being handled by a concurrent sweeping thread. Rather than try to cancel
  // tasks and restart them, we wait "per page". This should be faster.
  for (Page* p : *space_) {
    if (!p->SweepingDone()) sweeper->WaitForPageToBeSwept(p);
  }
  space_->paged_space()->RefillFreeList();
  DCHECK(!sweeper->ShouldRefillFreelistForSpace(NEW_SPACE));
  return paged_space_allocator_policy_->TryAllocationFromFreeListMain(
      static_cast<size_t>(size_in_bytes), origin);
}

bool PagedNewSpaceAllocatorPolicy::AddPageBeyondCapacity(
    int size_in_bytes, AllocationOrigin origin) {
  if (space_->paged_space()->AddPageBeyondCapacity(size_in_bytes, origin)) {
    return paged_space_allocator_policy_->TryAllocationFromFreeListMain(
        size_in_bytes, origin);
  }

  return false;
}

void PagedNewSpaceAllocatorPolicy::FreeLinearAllocationArea() {
  if (!allocator_->IsLabValid()) return;
  Page::FromAllocationAreaAddress(allocator_->top())
      ->DecreaseAllocatedLabSize(allocator_->limit() - allocator_->top());
  paged_space_allocator_policy_->FreeLinearAllocationAreaUnsynchronized();
}

bool PagedSpaceAllocatorPolicy::EnsureAllocation(int size_in_bytes,
                                                 AllocationAlignment alignment,
                                                 AllocationOrigin origin) {
  if (!allocator_->in_gc() && !((allocator_->identity() == NEW_SPACE) &&
                                heap_->ShouldOptimizeForLoadTime())) {
    // Start incremental marking before the actual allocation, this allows the
    // allocation function to mark the object black when incremental marking is
    // running.
    heap()->StartIncrementalMarkingIfAllocationLimitIsReached(
        heap()->GCFlagsForIncrementalMarking(),
        kGCCallbackScheduleIdleGarbageCollection);
  }
  if (allocator_->identity() == NEW_SPACE &&
      heap()->incremental_marking()->IsStopped()) {
    heap()->StartMinorMSIncrementalMarkingIfNeeded();
  }

  // We don't know exactly how much filler we need to align until space is
  // allocated, so assume the worst case.
  size_in_bytes += Heap::GetMaximumFillToAlign(alignment);
  if (allocator_->allocation_info().top() + size_in_bytes <=
      allocator_->allocation_info().limit()) {
    return true;
  }
  return RefillLabMain(size_in_bytes, origin);
}

bool PagedSpaceAllocatorPolicy::RefillLabMain(int size_in_bytes,
                                              AllocationOrigin origin) {
  // Allocation in this space has failed.
  DCHECK_GE(size_in_bytes, 0);

  if (TryExtendLAB(size_in_bytes)) return true;

  static constexpr int kMaxPagesToSweep = 1;

  if (TryAllocationFromFreeListMain(size_in_bytes, origin)) return true;

  const bool is_main_thread =
      heap()->IsMainThread() || heap()->IsSharedMainThread();
  const auto sweeping_scope_kind =
      is_main_thread ? ThreadKind::kMain : ThreadKind::kBackground;
  const auto sweeping_scope_id = heap()->sweeper()->GetTracingScope(
      allocator_->identity(), is_main_thread);
  // Sweeping is still in progress.
  if (heap()->sweeping_in_progress()) {
    // First try to refill the free-list, concurrent sweeper threads
    // may have freed some objects in the meantime.
    if (heap()->sweeper()->ShouldRefillFreelistForSpace(
            allocator_->identity())) {
      {
        TRACE_GC_EPOCH_WITH_FLOW(
            heap()->tracer(), sweeping_scope_id, sweeping_scope_kind,
            heap()->sweeper()->GetTraceIdForFlowEvent(sweeping_scope_id),
            TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);
        space_->RefillFreeList();
      }

      // Retry the free list allocation.
      if (TryAllocationFromFreeListMain(static_cast<size_t>(size_in_bytes),
                                        origin))
        return true;
    }

    if (ContributeToSweepingMain(size_in_bytes, kMaxPagesToSweep, size_in_bytes,
                                 origin, sweeping_scope_id,
                                 sweeping_scope_kind))
      return true;
  }

  if (allocator_->in_gc()) {
    DCHECK_NE(NEW_SPACE, allocator_->identity());
    // The main thread may have acquired all swept pages. Try to steal from
    // it. This can only happen during young generation evacuation.
    PagedSpaceBase* main_space = heap()->paged_space(allocator_->identity());
    Page* page = main_space->RemovePageSafe(size_in_bytes);
    if (page != nullptr) {
      space_->AddPage(page);
      if (TryAllocationFromFreeListMain(static_cast<size_t>(size_in_bytes),
                                        origin))
        return true;
    }
  }

  if (allocator_->identity() != NEW_SPACE &&
      heap()->ShouldExpandOldGenerationOnSlowAllocation(
          allocator_->local_heap(), origin) &&
      heap()->CanExpandOldGeneration(space_->AreaSize())) {
    if (space_->TryExpand(size_in_bytes, origin) &&
        TryAllocationFromFreeListMain(static_cast<size_t>(size_in_bytes),
                                      origin)) {
      return true;
    }
  }

  // Try sweeping all pages.
  if (ContributeToSweepingMain(0, 0, size_in_bytes, origin, sweeping_scope_id,
                               sweeping_scope_kind))
    return true;

  if (allocator_->identity() != NEW_SPACE && allocator_->in_gc() &&
      !heap()->force_oom()) {
    // Avoid OOM crash in the GC in order to invoke NearHeapLimitCallback after
    // GC and give it a chance to increase the heap limit.
    if (space_->TryExpand(size_in_bytes, origin) &&
        TryAllocationFromFreeListMain(static_cast<size_t>(size_in_bytes),
                                      origin)) {
      return true;
    }
  }
  return false;
}

bool PagedSpaceAllocatorPolicy::ContributeToSweepingMain(
    int required_freed_bytes, int max_pages, int size_in_bytes,
    AllocationOrigin origin, GCTracer::Scope::ScopeId sweeping_scope_id,
    ThreadKind sweeping_scope_kind) {
  if (!heap()->sweeping_in_progress_for_space(allocator_->identity()))
    return false;
  if (!(allocator_->identity() == NEW_SPACE
            ? heap()->sweeper()->AreMinorSweeperTasksRunning()
            : heap()->sweeper()->AreMajorSweeperTasksRunning()) &&
      heap()->sweeper()->IsSweepingDoneForSpace(allocator_->identity()))
    return false;

  TRACE_GC_EPOCH_WITH_FLOW(
      heap()->tracer(), sweeping_scope_id, sweeping_scope_kind,
      heap()->sweeper()->GetTraceIdForFlowEvent(sweeping_scope_id),
      TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);
  // Cleanup invalidated old-to-new refs for compaction space in the
  // final atomic pause.
  Sweeper::SweepingMode sweeping_mode =
      allocator_->in_gc() ? Sweeper::SweepingMode::kEagerDuringGC
                          : Sweeper::SweepingMode::kLazyOrConcurrent;

  heap()->sweeper()->ParallelSweepSpace(allocator_->identity(), sweeping_mode,
                                        required_freed_bytes, max_pages);
  space_->RefillFreeList();
  return TryAllocationFromFreeListMain(size_in_bytes, origin);
}

void PagedSpaceAllocatorPolicy::SetLinearAllocationArea(Address top,
                                                        Address limit,
                                                        Address end) {
  allocator_->ResetLab(top, limit, end);
  if (top != kNullAddress && top != limit) {
    Page* page = Page::FromAllocationAreaAddress(top);
    if (allocator_->IsBlackAllocationEnabled()) {
      page->CreateBlackArea(top, limit);
    }
  }
}

void PagedSpaceAllocatorPolicy::DecreaseLimit(Address new_limit) {
  Address old_limit = allocator_->limit();
  DCHECK_LE(allocator_->top(), new_limit);
  DCHECK_GE(old_limit, new_limit);
  if (new_limit != old_limit) {
    base::Optional<CodePageHeaderModificationScope> optional_scope;
    if (allocator_->identity() == CODE_SPACE) {
      optional_scope.emplace("DecreaseLimit writes to the page header.");
    }

    PagedSpace::ConcurrentAllocationMutex guard(space_);
    Address old_max_limit = allocator_->original_limit_relaxed();
    if (!allocator_->supports_extending_lab()) {
      DCHECK_EQ(old_max_limit, old_limit);
      allocator_->ResetLab(allocator_->top(), new_limit, new_limit);
      space_->Free(new_limit, old_max_limit - new_limit,
                   SpaceAccountingMode::kSpaceAccounted);
    } else {
      allocator_->ExtendLAB(new_limit);
      heap()->CreateFillerObjectAt(new_limit,
                                   static_cast<int>(old_max_limit - new_limit));
    }
    if (allocator_->IsBlackAllocationEnabled()) {
      Page::FromAllocationAreaAddress(new_limit)->DestroyBlackArea(new_limit,
                                                                   old_limit);
    }
  }
}

bool PagedSpaceAllocatorPolicy::TryAllocationFromFreeListMain(
    size_t size_in_bytes, AllocationOrigin origin) {
  PagedSpace::ConcurrentAllocationMutex guard(space_);
  DCHECK(IsAligned(size_in_bytes, kTaggedSize));
  DCHECK_LE(allocator_->top(), allocator_->limit());
#ifdef DEBUG
  if (allocator_->top() != allocator_->limit()) {
    DCHECK_EQ(Page::FromAddress(allocator_->top()),
              Page::FromAddress(allocator_->limit() - 1));
  }
#endif
  // Don't free list allocate if there is linear space available.
  DCHECK_LT(static_cast<size_t>(allocator_->limit() - allocator_->top()),
            size_in_bytes);

  // Mark the old linear allocation area with a free space map so it can be
  // skipped when scanning the heap.  This also puts it back in the free list
  // if it is big enough.
  FreeLinearAllocationAreaUnsynchronized();

  size_t new_node_size = 0;
  Tagged<FreeSpace> new_node =
      space_->free_list_->Allocate(size_in_bytes, &new_node_size, origin);
  if (new_node.is_null()) return false;
  DCHECK_GE(new_node_size, size_in_bytes);

  // The old-space-step might have finished sweeping and restarted marking.
  // Verify that it did not turn the page of the new node into an evacuation
  // candidate.
  DCHECK(!MarkCompactCollector::IsOnEvacuationCandidate(new_node));

  // Memory in the linear allocation area is counted as allocated.  We may free
  // a little of this again immediately - see below.
  Page* page = Page::FromHeapObject(new_node);
  space_->IncreaseAllocatedBytes(new_node_size, page);

  DCHECK_EQ(allocator_->allocation_info().start(),
            allocator_->allocation_info().top());
  Address start = new_node.address();
  Address end = new_node.address() + new_node_size;
  Address limit = allocator_->ComputeLimit(start, end, size_in_bytes);
  DCHECK_LE(limit, end);
  DCHECK_LE(size_in_bytes, limit - start);
  if (limit != end) {
    if (!allocator_->supports_extending_lab()) {
      space_->Free(limit, end - limit, SpaceAccountingMode::kSpaceAccounted);
      end = limit;
    } else {
      DCHECK(heap()->IsMainThread());
      heap()->CreateFillerObjectAt(limit, static_cast<int>(end - limit));
    }
  }
  SetLinearAllocationArea(start, limit, end);
  space_->AddRangeToActiveSystemPages(page, start, limit);

  return true;
}

bool PagedSpaceAllocatorPolicy::TryExtendLAB(int size_in_bytes) {
  if (!allocator_->supports_extending_lab()) return false;
  Address current_top = allocator_->top();
  if (current_top == kNullAddress) return false;
  Address current_limit = allocator_->limit();
  Address max_limit = allocator_->original_limit_relaxed();
  if (current_top + size_in_bytes > max_limit) {
    return false;
  }
  allocator_->AdvanceAllocationObservers();
  Address new_limit =
      allocator_->ComputeLimit(current_top, max_limit, size_in_bytes);
  allocator_->ExtendLAB(new_limit);
  DCHECK(heap()->IsMainThread());
  heap()->CreateFillerObjectAt(new_limit,
                               static_cast<int>(max_limit - new_limit));
  Page* page = Page::FromAddress(current_top);
  // No need to create a black allocation area since new space doesn't use
  // black allocation.
  DCHECK_EQ(NEW_SPACE, allocator_->identity());
  space_->AddRangeToActiveSystemPages(page, current_limit, new_limit);
  return true;
}

void PagedSpaceAllocatorPolicy::FreeLinearAllocationArea() {
  if (!allocator_->IsLabValid()) return;

  base::MutexGuard guard(space_->mutex());
  FreeLinearAllocationAreaUnsynchronized();
}

void PagedSpaceAllocatorPolicy::FreeLinearAllocationAreaUnsynchronized() {
  if (!allocator_->IsLabValid()) return;

#if DEBUG
  allocator_->Verify();
#endif  // DEBUG

  Address current_top = allocator_->top();
  Address current_limit = allocator_->limit();

  Address current_max_limit = allocator_->supports_extending_lab()
                                  ? allocator_->original_limit_relaxed()
                                  : current_limit;
  DCHECK_IMPLIES(!allocator_->supports_extending_lab(),
                 current_max_limit == current_limit);

  allocator_->AdvanceAllocationObservers();

  base::Optional<CodePageHeaderModificationScope> optional_scope;
  if (allocator_->identity() == CODE_SPACE) {
    optional_scope.emplace(
        "FreeLinearAllocationArea writes to the page header.");
  }

  if (current_top != current_limit && allocator_->IsBlackAllocationEnabled()) {
    Page::FromAddress(current_top)
        ->DestroyBlackArea(current_top, current_limit);
  }

  allocator_->ResetLab(kNullAddress, kNullAddress, kNullAddress);
  DCHECK_GE(current_limit, current_top);

  DCHECK_IMPLIES(current_limit - current_top >= 2 * kTaggedSize,
                 heap()->marking_state()->IsUnmarked(
                     HeapObject::FromAddress(current_top)));
  space_->Free(current_top, current_max_limit - current_top,
               SpaceAccountingMode::kSpaceAccounted);
}

}  // namespace internal
}  // namespace v8
