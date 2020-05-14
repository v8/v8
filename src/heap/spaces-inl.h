// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SPACES_INL_H_
#define V8_HEAP_SPACES_INL_H_

#include "src/base/atomic-utils.h"
#include "src/base/bounded-page-allocator.h"
#include "src/base/v8-fallthrough.h"
#include "src/common/globals.h"
#include "src/heap/heap-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/memory-chunk-inl.h"
#include "src/heap/spaces.h"
#include "src/objects/code-inl.h"
#include "src/sanitizer/msan.h"

namespace v8 {
namespace internal {

template <class PAGE_TYPE>
PageIteratorImpl<PAGE_TYPE>& PageIteratorImpl<PAGE_TYPE>::operator++() {
  p_ = p_->next_page();
  return *this;
}

template <class PAGE_TYPE>
PageIteratorImpl<PAGE_TYPE> PageIteratorImpl<PAGE_TYPE>::operator++(int) {
  PageIteratorImpl<PAGE_TYPE> tmp(*this);
  operator++();
  return tmp;
}

PageRange::PageRange(Address start, Address limit)
    : begin_(Page::FromAddress(start)),
      end_(Page::FromAllocationAreaAddress(limit)->next_page()) {
#ifdef DEBUG
  if (begin_->InNewSpace()) {
    SemiSpace::AssertValidRange(start, limit);
  }
#endif  // DEBUG
}

// -----------------------------------------------------------------------------
// SemiSpaceObjectIterator

HeapObject SemiSpaceObjectIterator::Next() {
  while (current_ != limit_) {
    if (Page::IsAlignedToPageSize(current_)) {
      Page* page = Page::FromAllocationAreaAddress(current_);
      page = page->next_page();
      DCHECK(page);
      current_ = page->area_start();
      if (current_ == limit_) return HeapObject();
    }
    HeapObject object = HeapObject::FromAddress(current_);
    current_ += object.Size();
    if (!object.IsFreeSpaceOrFiller()) {
      return object;
    }
  }
  return HeapObject();
}

void Space::IncrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                               size_t amount) {
  base::CheckedIncrement(&external_backing_store_bytes_[type], amount);
  heap()->IncrementExternalBackingStoreBytes(type, amount);
}

void Space::DecrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                               size_t amount) {
  base::CheckedDecrement(&external_backing_store_bytes_[type], amount);
  heap()->DecrementExternalBackingStoreBytes(type, amount);
}

void Space::MoveExternalBackingStoreBytes(ExternalBackingStoreType type,
                                          Space* from, Space* to,
                                          size_t amount) {
  if (from == to) return;

  base::CheckedDecrement(&(from->external_backing_store_bytes_[type]), amount);
  base::CheckedIncrement(&(to->external_backing_store_bytes_[type]), amount);
}

// -----------------------------------------------------------------------------
// SemiSpace

bool SemiSpace::Contains(HeapObject o) const {
  MemoryChunk* memory_chunk = MemoryChunk::FromHeapObject(o);
  if (memory_chunk->IsLargePage()) return false;
  return id_ == kToSpace ? memory_chunk->IsToPage()
                         : memory_chunk->IsFromPage();
}

bool SemiSpace::Contains(Object o) const {
  return o.IsHeapObject() && Contains(HeapObject::cast(o));
}

bool SemiSpace::ContainsSlow(Address a) const {
  for (const Page* p : *this) {
    if (p == MemoryChunk::FromAddress(a)) return true;
  }
  return false;
}

// --------------------------------------------------------------------------
// NewSpace

bool NewSpace::Contains(Object o) const {
  return o.IsHeapObject() && Contains(HeapObject::cast(o));
}

bool NewSpace::Contains(HeapObject o) const {
  return MemoryChunk::FromHeapObject(o)->InNewSpace();
}

bool NewSpace::ContainsSlow(Address a) const {
  return from_space_.ContainsSlow(a) || to_space_.ContainsSlow(a);
}

bool NewSpace::ToSpaceContainsSlow(Address a) const {
  return to_space_.ContainsSlow(a);
}

bool NewSpace::ToSpaceContains(Object o) const { return to_space_.Contains(o); }
bool NewSpace::FromSpaceContains(Object o) const {
  return from_space_.Contains(o);
}

void Page::MarkNeverAllocateForTesting() {
  DCHECK(this->owner_identity() != NEW_SPACE);
  DCHECK(!IsFlagSet(NEVER_ALLOCATE_ON_PAGE));
  SetFlag(NEVER_ALLOCATE_ON_PAGE);
  SetFlag(NEVER_EVACUATE);
  reinterpret_cast<PagedSpace*>(owner())->free_list()->EvictFreeListItems(this);
}

void Page::MarkEvacuationCandidate() {
  DCHECK(!IsFlagSet(NEVER_EVACUATE));
  DCHECK_NULL(slot_set<OLD_TO_OLD>());
  DCHECK_NULL(typed_slot_set<OLD_TO_OLD>());
  SetFlag(EVACUATION_CANDIDATE);
  reinterpret_cast<PagedSpace*>(owner())->free_list()->EvictFreeListItems(this);
}

void Page::ClearEvacuationCandidate() {
  if (!IsFlagSet(COMPACTION_WAS_ABORTED)) {
    DCHECK_NULL(slot_set<OLD_TO_OLD>());
    DCHECK_NULL(typed_slot_set<OLD_TO_OLD>());
  }
  ClearFlag(EVACUATION_CANDIDATE);
  InitializeFreeListCategories();
}

OldGenerationMemoryChunkIterator::OldGenerationMemoryChunkIterator(Heap* heap)
    : heap_(heap),
      state_(kOldSpaceState),
      old_iterator_(heap->old_space()->begin()),
      code_iterator_(heap->code_space()->begin()),
      map_iterator_(heap->map_space()->begin()),
      lo_iterator_(heap->lo_space()->begin()),
      code_lo_iterator_(heap->code_lo_space()->begin()) {}

MemoryChunk* OldGenerationMemoryChunkIterator::next() {
  switch (state_) {
    case kOldSpaceState: {
      if (old_iterator_ != heap_->old_space()->end()) return *(old_iterator_++);
      state_ = kMapState;
      V8_FALLTHROUGH;
    }
    case kMapState: {
      if (map_iterator_ != heap_->map_space()->end()) return *(map_iterator_++);
      state_ = kCodeState;
      V8_FALLTHROUGH;
    }
    case kCodeState: {
      if (code_iterator_ != heap_->code_space()->end())
        return *(code_iterator_++);
      state_ = kLargeObjectState;
      V8_FALLTHROUGH;
    }
    case kLargeObjectState: {
      if (lo_iterator_ != heap_->lo_space()->end()) return *(lo_iterator_++);
      state_ = kCodeLargeObjectState;
      V8_FALLTHROUGH;
    }
    case kCodeLargeObjectState: {
      if (code_lo_iterator_ != heap_->code_lo_space()->end())
        return *(code_lo_iterator_++);
      state_ = kFinishedState;
      V8_FALLTHROUGH;
    }
    case kFinishedState:
      return nullptr;
    default:
      break;
  }
  UNREACHABLE();
}

bool FreeListCategory::is_linked(FreeList* owner) const {
  return prev_ != nullptr || next_ != nullptr ||
         owner->categories_[type_] == this;
}

void FreeListCategory::UpdateCountersAfterAllocation(size_t allocation_size) {
  available_ -= allocation_size;
}

Page* FreeList::GetPageForCategoryType(FreeListCategoryType type) {
  FreeListCategory* category_top = top(type);
  if (category_top != nullptr) {
    DCHECK(!category_top->top().is_null());
    return Page::FromHeapObject(category_top->top());
  } else {
    return nullptr;
  }
}

Page* FreeListLegacy::GetPageForSize(size_t size_in_bytes) {
  const int minimum_category =
      static_cast<int>(SelectFreeListCategoryType(size_in_bytes));
  Page* page = GetPageForCategoryType(kHuge);
  if (!page && static_cast<int>(kLarge) >= minimum_category)
    page = GetPageForCategoryType(kLarge);
  if (!page && static_cast<int>(kMedium) >= minimum_category)
    page = GetPageForCategoryType(kMedium);
  if (!page && static_cast<int>(kSmall) >= minimum_category)
    page = GetPageForCategoryType(kSmall);
  if (!page && static_cast<int>(kTiny) >= minimum_category)
    page = GetPageForCategoryType(kTiny);
  if (!page && static_cast<int>(kTiniest) >= minimum_category)
    page = GetPageForCategoryType(kTiniest);
  return page;
}

Page* FreeListFastAlloc::GetPageForSize(size_t size_in_bytes) {
  const int minimum_category =
      static_cast<int>(SelectFreeListCategoryType(size_in_bytes));
  Page* page = GetPageForCategoryType(kHuge);
  if (!page && static_cast<int>(kLarge) >= minimum_category)
    page = GetPageForCategoryType(kLarge);
  if (!page && static_cast<int>(kMedium) >= minimum_category)
    page = GetPageForCategoryType(kMedium);
  return page;
}

AllocationResult LocalAllocationBuffer::AllocateRawAligned(
    int size_in_bytes, AllocationAlignment alignment) {
  Address current_top = allocation_info_.top();
  int filler_size = Heap::GetFillToAlign(current_top, alignment);

  Address new_top = current_top + filler_size + size_in_bytes;
  if (new_top > allocation_info_.limit()) return AllocationResult::Retry();

  allocation_info_.set_top(new_top);
  if (filler_size > 0) {
    return Heap::PrecedeWithFiller(ReadOnlyRoots(heap_),
                                   HeapObject::FromAddress(current_top),
                                   filler_size);
  }

  return AllocationResult(HeapObject::FromAddress(current_top));
}

// -----------------------------------------------------------------------------
// NewSpace

AllocationResult NewSpace::AllocateRawAligned(int size_in_bytes,
                                              AllocationAlignment alignment,
                                              AllocationOrigin origin) {
  Address top = allocation_info_.top();
  int filler_size = Heap::GetFillToAlign(top, alignment);
  int aligned_size_in_bytes = size_in_bytes + filler_size;

  if (allocation_info_.limit() - top <
      static_cast<uintptr_t>(aligned_size_in_bytes)) {
    // See if we can create room.
    if (!EnsureAllocation(size_in_bytes, alignment)) {
      return AllocationResult::Retry();
    }

    top = allocation_info_.top();
    filler_size = Heap::GetFillToAlign(top, alignment);
    aligned_size_in_bytes = size_in_bytes + filler_size;
  }

  HeapObject obj = HeapObject::FromAddress(top);
  allocation_info_.set_top(top + aligned_size_in_bytes);
  DCHECK_SEMISPACE_ALLOCATION_INFO(allocation_info_, to_space_);

  if (filler_size > 0) {
    obj = Heap::PrecedeWithFiller(ReadOnlyRoots(heap()), obj, filler_size);
  }

  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(obj.address(), size_in_bytes);

  if (FLAG_trace_allocations_origins) {
    UpdateAllocationOrigins(origin);
  }

  return obj;
}

AllocationResult NewSpace::AllocateRawUnaligned(int size_in_bytes,
                                                AllocationOrigin origin) {
  Address top = allocation_info_.top();
  if (allocation_info_.limit() < top + size_in_bytes) {
    // See if we can create room.
    if (!EnsureAllocation(size_in_bytes, kWordAligned)) {
      return AllocationResult::Retry();
    }

    top = allocation_info_.top();
  }

  HeapObject obj = HeapObject::FromAddress(top);
  allocation_info_.set_top(top + size_in_bytes);
  DCHECK_SEMISPACE_ALLOCATION_INFO(allocation_info_, to_space_);

  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(obj.address(), size_in_bytes);

  if (FLAG_trace_allocations_origins) {
    UpdateAllocationOrigins(origin);
  }

  return obj;
}

AllocationResult NewSpace::AllocateRaw(int size_in_bytes,
                                       AllocationAlignment alignment,
                                       AllocationOrigin origin) {
  if (top() < top_on_previous_step_) {
    // Generated code decreased the top() pointer to do folded allocations
    DCHECK_EQ(Page::FromAllocationAreaAddress(top()),
              Page::FromAllocationAreaAddress(top_on_previous_step_));
    top_on_previous_step_ = top();
  }
#ifdef V8_HOST_ARCH_32_BIT
  return alignment != kWordAligned
             ? AllocateRawAligned(size_in_bytes, alignment, origin)
             : AllocateRawUnaligned(size_in_bytes, origin);
#else
#ifdef V8_COMPRESS_POINTERS
  // TODO(ishell, v8:8875): Consider using aligned allocations once the
  // allocation alignment inconsistency is fixed. For now we keep using
  // unaligned access since both x64 and arm64 architectures (where pointer
  // compression is supported) allow unaligned access to doubles and full words.
#endif  // V8_COMPRESS_POINTERS
  return AllocateRawUnaligned(size_in_bytes, origin);
#endif
}

V8_WARN_UNUSED_RESULT inline AllocationResult NewSpace::AllocateRawSynchronized(
    int size_in_bytes, AllocationAlignment alignment, AllocationOrigin origin) {
  base::MutexGuard guard(&mutex_);
  return AllocateRaw(size_in_bytes, alignment, origin);
}

LocalAllocationBuffer LocalAllocationBuffer::FromResult(Heap* heap,
                                                        AllocationResult result,
                                                        intptr_t size) {
  if (result.IsRetry()) return InvalidBuffer();
  HeapObject obj;
  bool ok = result.To(&obj);
  USE(ok);
  DCHECK(ok);
  Address top = HeapObject::cast(obj).address();
  return LocalAllocationBuffer(heap, LinearAllocationArea(top, top + size));
}


bool LocalAllocationBuffer::TryMerge(LocalAllocationBuffer* other) {
  if (allocation_info_.top() == other->allocation_info_.limit()) {
    allocation_info_.set_top(other->allocation_info_.top());
    other->allocation_info_.Reset(kNullAddress, kNullAddress);
    return true;
  }
  return false;
}

bool LocalAllocationBuffer::TryFreeLast(HeapObject object, int object_size) {
  if (IsValid()) {
    const Address object_address = object.address();
    if ((allocation_info_.top() - object_size) == object_address) {
      allocation_info_.set_top(object_address);
      return true;
    }
  }
  return false;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SPACES_INL_H_
