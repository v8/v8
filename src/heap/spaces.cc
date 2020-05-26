// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/spaces.h"

#include <algorithm>
#include <cinttypes>
#include <utility>

#include "src/base/bits.h"
#include "src/base/bounded-page-allocator.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/heap/array-buffer-tracker-inl.h"
#include "src/heap/combined-heap.h"
#include "src/heap/concurrent-marking.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-controller.h"
#include "src/heap/heap.h"
#include "src/heap/incremental-marking-inl.h"
#include "src/heap/invalidated-slots-inl.h"
#include "src/heap/large-spaces.h"
#include "src/heap/mark-compact.h"
#include "src/heap/read-only-heap.h"
#include "src/heap/remembered-set-inl.h"
#include "src/heap/slot-set.h"
#include "src/init/v8.h"
#include "src/logging/counters.h"
#include "src/objects/free-space-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/objects-inl.h"
#include "src/sanitizer/msan.h"
#include "src/snapshot/snapshot.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

// These checks are here to ensure that the lower 32 bits of any real heap
// object can't overlap with the lower 32 bits of cleared weak reference value
// and therefore it's enough to compare only the lower 32 bits of a MaybeObject
// in order to figure out if it's a cleared weak reference or not.
STATIC_ASSERT(kClearedWeakHeapObjectLower32 > 0);
STATIC_ASSERT(kClearedWeakHeapObjectLower32 < Page::kHeaderSize);

PauseAllocationObserversScope::PauseAllocationObserversScope(Heap* heap)
    : heap_(heap) {
  DCHECK_EQ(heap->gc_state(), Heap::NOT_IN_GC);

  for (SpaceIterator it(heap_); it.HasNext();) {
    it.Next()->PauseAllocationObservers();
  }
}

PauseAllocationObserversScope::~PauseAllocationObserversScope() {
  for (SpaceIterator it(heap_); it.HasNext();) {
    it.Next()->ResumeAllocationObservers();
  }
}

void Page::AllocateFreeListCategories() {
  DCHECK_NULL(categories_);
  categories_ =
      new FreeListCategory*[owner()->free_list()->number_of_categories()]();
  for (int i = kFirstCategory; i <= owner()->free_list()->last_category();
       i++) {
    DCHECK_NULL(categories_[i]);
    categories_[i] = new FreeListCategory();
  }
}

void Page::InitializeFreeListCategories() {
  for (int i = kFirstCategory; i <= owner()->free_list()->last_category();
       i++) {
    categories_[i]->Initialize(static_cast<FreeListCategoryType>(i));
  }
}

void Page::ReleaseFreeListCategories() {
  if (categories_ != nullptr) {
    for (int i = kFirstCategory; i <= owner()->free_list()->last_category();
         i++) {
      if (categories_[i] != nullptr) {
        delete categories_[i];
        categories_[i] = nullptr;
      }
    }
    delete[] categories_;
    categories_ = nullptr;
  }
}

Page* Page::ConvertNewToOld(Page* old_page) {
  DCHECK(old_page);
  DCHECK(old_page->InNewSpace());
  OldSpace* old_space = old_page->heap()->old_space();
  old_page->set_owner(old_space);
  old_page->SetFlags(0, static_cast<uintptr_t>(~0));
  Page* new_page = old_space->InitializePage(old_page);
  old_space->AddPage(new_page);
  return new_page;
}

void Page::MoveOldToNewRememberedSetForSweeping() {
  CHECK_NULL(sweeping_slot_set_);
  sweeping_slot_set_ = slot_set_[OLD_TO_NEW];
  slot_set_[OLD_TO_NEW] = nullptr;
}

void Page::MergeOldToNewRememberedSets() {
  if (sweeping_slot_set_ == nullptr) return;

  if (slot_set_[OLD_TO_NEW]) {
    RememberedSet<OLD_TO_NEW>::Iterate(
        this,
        [this](MaybeObjectSlot slot) {
          Address address = slot.address();
          RememberedSetSweeping::Insert<AccessMode::NON_ATOMIC>(this, address);
          return KEEP_SLOT;
        },
        SlotSet::KEEP_EMPTY_BUCKETS);

    ReleaseSlotSet<OLD_TO_NEW>();
  }

  CHECK_NULL(slot_set_[OLD_TO_NEW]);
  slot_set_[OLD_TO_NEW] = sweeping_slot_set_;
  sweeping_slot_set_ = nullptr;
}

void Page::ResetAllocationStatistics() {
  allocated_bytes_ = area_size();
  wasted_memory_ = 0;
}

void Page::AllocateLocalTracker() {
  DCHECK_NULL(local_tracker_);
  local_tracker_ = new LocalArrayBufferTracker(this);
}

bool Page::contains_array_buffers() {
  return local_tracker_ != nullptr && !local_tracker_->IsEmpty();
}

size_t Page::AvailableInFreeList() {
  size_t sum = 0;
  ForAllFreeListCategories([&sum](FreeListCategory* category) {
    sum += category->available();
  });
  return sum;
}

#ifdef DEBUG
namespace {
// Skips filler starting from the given filler until the end address.
// Returns the first address after the skipped fillers.
Address SkipFillers(HeapObject filler, Address end) {
  Address addr = filler.address();
  while (addr < end) {
    filler = HeapObject::FromAddress(addr);
    CHECK(filler.IsFreeSpaceOrFiller());
    addr = filler.address() + filler.Size();
  }
  return addr;
}
}  // anonymous namespace
#endif  // DEBUG

size_t Page::ShrinkToHighWaterMark() {
  // Shrinking only makes sense outside of the CodeRange, where we don't care
  // about address space fragmentation.
  VirtualMemory* reservation = reserved_memory();
  if (!reservation->IsReserved()) return 0;

  // Shrink pages to high water mark. The water mark points either to a filler
  // or the area_end.
  HeapObject filler = HeapObject::FromAddress(HighWaterMark());
  if (filler.address() == area_end()) return 0;
  CHECK(filler.IsFreeSpaceOrFiller());
  // Ensure that no objects were allocated in [filler, area_end) region.
  DCHECK_EQ(area_end(), SkipFillers(filler, area_end()));
  // Ensure that no objects will be allocated on this page.
  DCHECK_EQ(0u, AvailableInFreeList());

  // Ensure that slot sets are empty. Otherwise the buckets for the shrinked
  // area would not be freed when deallocating this page.
  DCHECK_NULL(slot_set<OLD_TO_NEW>());
  DCHECK_NULL(slot_set<OLD_TO_OLD>());
  DCHECK_NULL(sweeping_slot_set());

  size_t unused = RoundDown(static_cast<size_t>(area_end() - filler.address()),
                            MemoryAllocator::GetCommitPageSize());
  if (unused > 0) {
    DCHECK_EQ(0u, unused % MemoryAllocator::GetCommitPageSize());
    if (FLAG_trace_gc_verbose) {
      PrintIsolate(heap()->isolate(), "Shrinking page %p: end %p -> %p\n",
                   reinterpret_cast<void*>(this),
                   reinterpret_cast<void*>(area_end()),
                   reinterpret_cast<void*>(area_end() - unused));
    }
    heap()->CreateFillerObjectAt(
        filler.address(),
        static_cast<int>(area_end() - filler.address() - unused),
        ClearRecordedSlots::kNo);
    heap()->memory_allocator()->PartialFreeMemory(
        this, address() + size() - unused, unused, area_end() - unused);
    if (filler.address() != area_end()) {
      CHECK(filler.IsFreeSpaceOrFiller());
      CHECK_EQ(filler.address() + filler.Size(), area_end());
    }
  }
  return unused;
}

void Page::CreateBlackArea(Address start, Address end) {
  DCHECK(heap()->incremental_marking()->black_allocation());
  DCHECK_EQ(Page::FromAddress(start), this);
  DCHECK_LT(start, end);
  DCHECK_EQ(Page::FromAddress(end - 1), this);
  IncrementalMarking::MarkingState* marking_state =
      heap()->incremental_marking()->marking_state();
  marking_state->bitmap(this)->SetRange(AddressToMarkbitIndex(start),
                                        AddressToMarkbitIndex(end));
  marking_state->IncrementLiveBytes(this, static_cast<intptr_t>(end - start));
}

void Page::CreateBlackAreaBackground(Address start, Address end) {
  DCHECK(heap()->incremental_marking()->black_allocation());
  DCHECK_EQ(Page::FromAddress(start), this);
  DCHECK_LT(start, end);
  DCHECK_EQ(Page::FromAddress(end - 1), this);
  IncrementalMarking::AtomicMarkingState* marking_state =
      heap()->incremental_marking()->atomic_marking_state();
  marking_state->bitmap(this)->SetRange(AddressToMarkbitIndex(start),
                                        AddressToMarkbitIndex(end));
  heap()->incremental_marking()->IncrementLiveBytesBackground(
      this, static_cast<intptr_t>(end - start));
}

void Page::DestroyBlackArea(Address start, Address end) {
  DCHECK(heap()->incremental_marking()->black_allocation());
  DCHECK_EQ(Page::FromAddress(start), this);
  DCHECK_LT(start, end);
  DCHECK_EQ(Page::FromAddress(end - 1), this);
  IncrementalMarking::MarkingState* marking_state =
      heap()->incremental_marking()->marking_state();
  marking_state->bitmap(this)->ClearRange(AddressToMarkbitIndex(start),
                                          AddressToMarkbitIndex(end));
  marking_state->IncrementLiveBytes(this, -static_cast<intptr_t>(end - start));
}

void Page::DestroyBlackAreaBackground(Address start, Address end) {
  DCHECK(heap()->incremental_marking()->black_allocation());
  DCHECK_EQ(Page::FromAddress(start), this);
  DCHECK_LT(start, end);
  DCHECK_EQ(Page::FromAddress(end - 1), this);
  IncrementalMarking::AtomicMarkingState* marking_state =
      heap()->incremental_marking()->atomic_marking_state();
  marking_state->bitmap(this)->ClearRange(AddressToMarkbitIndex(start),
                                          AddressToMarkbitIndex(end));
  heap()->incremental_marking()->IncrementLiveBytesBackground(
      this, -static_cast<intptr_t>(end - start));
}

// -----------------------------------------------------------------------------
// PagedSpace implementation

void Space::AddAllocationObserver(AllocationObserver* observer) {
  allocation_observers_.push_back(observer);
  StartNextInlineAllocationStep();
}

void Space::RemoveAllocationObserver(AllocationObserver* observer) {
  auto it = std::find(allocation_observers_.begin(),
                      allocation_observers_.end(), observer);
  DCHECK(allocation_observers_.end() != it);
  allocation_observers_.erase(it);
  StartNextInlineAllocationStep();
}

void Space::PauseAllocationObservers() { allocation_observers_paused_ = true; }

void Space::ResumeAllocationObservers() {
  allocation_observers_paused_ = false;
}

void Space::AllocationStep(int bytes_since_last, Address soon_object,
                           int size) {
  if (!AllocationObserversActive()) {
    return;
  }

  DCHECK(!heap()->allocation_step_in_progress());
  heap()->set_allocation_step_in_progress(true);
  heap()->CreateFillerObjectAt(soon_object, size, ClearRecordedSlots::kNo);
  for (AllocationObserver* observer : allocation_observers_) {
    observer->AllocationStep(bytes_since_last, soon_object, size);
  }
  heap()->set_allocation_step_in_progress(false);
}

void Space::AllocationStepAfterMerge(Address first_object_in_chunk, int size) {
  if (!AllocationObserversActive()) {
    return;
  }

  DCHECK(!heap()->allocation_step_in_progress());
  heap()->set_allocation_step_in_progress(true);
  for (AllocationObserver* observer : allocation_observers_) {
    observer->AllocationStep(size, first_object_in_chunk, size);
  }
  heap()->set_allocation_step_in_progress(false);
}

intptr_t Space::GetNextInlineAllocationStepSize() {
  intptr_t next_step = 0;
  for (AllocationObserver* observer : allocation_observers_) {
    next_step = next_step ? Min(next_step, observer->bytes_to_next_step())
                          : observer->bytes_to_next_step();
  }
  DCHECK(allocation_observers_.size() == 0 || next_step > 0);
  return next_step;
}

Address SpaceWithLinearArea::ComputeLimit(Address start, Address end,
                                          size_t min_size) {
  DCHECK_GE(end - start, min_size);

  if (heap()->inline_allocation_disabled()) {
    // Fit the requested area exactly.
    return start + min_size;
  } else if (SupportsInlineAllocation() && AllocationObserversActive()) {
    // Generated code may allocate inline from the linear allocation area for.
    // To make sure we can observe these allocations, we use a lower limit.
    size_t step = GetNextInlineAllocationStepSize();
    size_t rounded_step =
        RoundSizeDownToObjectAlignment(static_cast<int>(step - 1));
    return Min(static_cast<Address>(start + min_size + rounded_step), end);
  } else {
    // The entire node can be used as the linear allocation area.
    return end;
  }
}

void SpaceWithLinearArea::UpdateAllocationOrigins(AllocationOrigin origin) {
  DCHECK(!((origin != AllocationOrigin::kGC) &&
           (heap()->isolate()->current_vm_state() == GC)));
  allocations_origins_[static_cast<int>(origin)]++;
}

void SpaceWithLinearArea::PrintAllocationsOrigins() {
  PrintIsolate(
      heap()->isolate(),
      "Allocations Origins for %s: GeneratedCode:%zu - Runtime:%zu - GC:%zu\n",
      name(), allocations_origins_[0], allocations_origins_[1],
      allocations_origins_[2]);
}

LinearAllocationArea LocalAllocationBuffer::CloseAndMakeIterable() {
  if (IsValid()) {
    MakeIterable();
    const LinearAllocationArea old_info = allocation_info_;
    allocation_info_ = LinearAllocationArea(kNullAddress, kNullAddress);
    return old_info;
  }
  return LinearAllocationArea(kNullAddress, kNullAddress);
}

void LocalAllocationBuffer::MakeIterable() {
  if (IsValid()) {
    heap_->CreateFillerObjectAt(
        allocation_info_.top(),
        static_cast<int>(allocation_info_.limit() - allocation_info_.top()),
        ClearRecordedSlots::kNo);
  }
}

LocalAllocationBuffer::LocalAllocationBuffer(
    Heap* heap, LinearAllocationArea allocation_info) V8_NOEXCEPT
    : heap_(heap),
      allocation_info_(allocation_info) {
  if (IsValid()) {
    heap_->CreateFillerObjectAt(
        allocation_info_.top(),
        static_cast<int>(allocation_info_.limit() - allocation_info_.top()),
        ClearRecordedSlots::kNo);
  }
}

LocalAllocationBuffer::LocalAllocationBuffer(LocalAllocationBuffer&& other)
    V8_NOEXCEPT {
  *this = std::move(other);
}

LocalAllocationBuffer& LocalAllocationBuffer::operator=(
    LocalAllocationBuffer&& other) V8_NOEXCEPT {
  heap_ = other.heap_;
  allocation_info_ = other.allocation_info_;

  other.allocation_info_.Reset(kNullAddress, kNullAddress);
  return *this;
}
void SpaceWithLinearArea::StartNextInlineAllocationStep() {
  if (heap()->allocation_step_in_progress()) {
    // If we are mid-way through an existing step, don't start a new one.
    return;
  }

  if (AllocationObserversActive()) {
    top_on_previous_step_ = top();
    UpdateInlineAllocationLimit(0);
  } else {
    DCHECK_EQ(kNullAddress, top_on_previous_step_);
  }
}

void SpaceWithLinearArea::AddAllocationObserver(AllocationObserver* observer) {
  InlineAllocationStep(top(), top(), kNullAddress, 0);
  Space::AddAllocationObserver(observer);
  DCHECK_IMPLIES(top_on_previous_step_, AllocationObserversActive());
}

void SpaceWithLinearArea::RemoveAllocationObserver(
    AllocationObserver* observer) {
  Address top_for_next_step =
      allocation_observers_.size() == 1 ? kNullAddress : top();
  InlineAllocationStep(top(), top_for_next_step, kNullAddress, 0);
  Space::RemoveAllocationObserver(observer);
  DCHECK_IMPLIES(top_on_previous_step_, AllocationObserversActive());
}

void SpaceWithLinearArea::PauseAllocationObservers() {
  // Do a step to account for memory allocated so far.
  InlineAllocationStep(top(), kNullAddress, kNullAddress, 0);
  Space::PauseAllocationObservers();
  DCHECK_EQ(kNullAddress, top_on_previous_step_);
  UpdateInlineAllocationLimit(0);
}

void SpaceWithLinearArea::ResumeAllocationObservers() {
  DCHECK_EQ(kNullAddress, top_on_previous_step_);
  Space::ResumeAllocationObservers();
  StartNextInlineAllocationStep();
}

void SpaceWithLinearArea::InlineAllocationStep(Address top,
                                               Address top_for_next_step,
                                               Address soon_object,
                                               size_t size) {
  if (heap()->allocation_step_in_progress()) {
    // Avoid starting a new step if we are mid-way through an existing one.
    return;
  }

  if (top_on_previous_step_) {
    if (top < top_on_previous_step_) {
      // Generated code decreased the top pointer to do folded allocations.
      DCHECK_NE(top, kNullAddress);
      DCHECK_EQ(Page::FromAllocationAreaAddress(top),
                Page::FromAllocationAreaAddress(top_on_previous_step_));
      top_on_previous_step_ = top;
    }
    int bytes_allocated = static_cast<int>(top - top_on_previous_step_);
    AllocationStep(bytes_allocated, soon_object, static_cast<int>(size));
    top_on_previous_step_ = top_for_next_step;
  }
}

// -----------------------------------------------------------------------------
// Free lists for old object spaces implementation

void FreeListCategory::Reset(FreeList* owner) {
  if (is_linked(owner) && !top().is_null()) {
    owner->DecreaseAvailableBytes(available_);
  }
  set_top(FreeSpace());
  set_prev(nullptr);
  set_next(nullptr);
  available_ = 0;
}

FreeSpace FreeListCategory::PickNodeFromList(size_t minimum_size,
                                             size_t* node_size) {
  FreeSpace node = top();
  DCHECK(!node.is_null());
  DCHECK(Page::FromHeapObject(node)->CanAllocate());
  if (static_cast<size_t>(node.Size()) < minimum_size) {
    *node_size = 0;
    return FreeSpace();
  }
  set_top(node.next());
  *node_size = node.Size();
  UpdateCountersAfterAllocation(*node_size);
  return node;
}

FreeSpace FreeListCategory::SearchForNodeInList(size_t minimum_size,
                                                size_t* node_size) {
  FreeSpace prev_non_evac_node;
  for (FreeSpace cur_node = top(); !cur_node.is_null();
       cur_node = cur_node.next()) {
    DCHECK(Page::FromHeapObject(cur_node)->CanAllocate());
    size_t size = cur_node.size();
    if (size >= minimum_size) {
      DCHECK_GE(available_, size);
      UpdateCountersAfterAllocation(size);
      if (cur_node == top()) {
        set_top(cur_node.next());
      }
      if (!prev_non_evac_node.is_null()) {
        MemoryChunk* chunk = MemoryChunk::FromHeapObject(prev_non_evac_node);
        if (chunk->owner_identity() == CODE_SPACE) {
          chunk->heap()->UnprotectAndRegisterMemoryChunk(chunk);
        }
        prev_non_evac_node.set_next(cur_node.next());
      }
      *node_size = size;
      return cur_node;
    }

    prev_non_evac_node = cur_node;
  }
  return FreeSpace();
}

void FreeListCategory::Free(Address start, size_t size_in_bytes, FreeMode mode,
                            FreeList* owner) {
  FreeSpace free_space = FreeSpace::cast(HeapObject::FromAddress(start));
  free_space.set_next(top());
  set_top(free_space);
  available_ += size_in_bytes;
  if (mode == kLinkCategory) {
    if (is_linked(owner)) {
      owner->IncreaseAvailableBytes(size_in_bytes);
    } else {
      owner->AddCategory(this);
    }
  }
}

void FreeListCategory::RepairFreeList(Heap* heap) {
  Map free_space_map = ReadOnlyRoots(heap).free_space_map();
  FreeSpace n = top();
  while (!n.is_null()) {
    ObjectSlot map_slot = n.map_slot();
    if (map_slot.contains_value(kNullAddress)) {
      map_slot.store(free_space_map);
    } else {
      DCHECK(map_slot.contains_value(free_space_map.ptr()));
    }
    n = n.next();
  }
}

void FreeListCategory::Relink(FreeList* owner) {
  DCHECK(!is_linked(owner));
  owner->AddCategory(this);
}

// ------------------------------------------------
// Generic FreeList methods (alloc/free related)

FreeList* FreeList::CreateFreeList() {
  switch (FLAG_gc_freelist_strategy) {
    case 0:
      return new FreeListLegacy();
    case 1:
      return new FreeListFastAlloc();
    case 2:
      return new FreeListMany();
    case 3:
      return new FreeListManyCached();
    case 4:
      return new FreeListManyCachedFastPath();
    case 5:
      return new FreeListManyCachedOrigin();
    default:
      FATAL("Invalid FreeList strategy");
  }
}

FreeSpace FreeList::TryFindNodeIn(FreeListCategoryType type,
                                  size_t minimum_size, size_t* node_size) {
  FreeListCategory* category = categories_[type];
  if (category == nullptr) return FreeSpace();
  FreeSpace node = category->PickNodeFromList(minimum_size, node_size);
  if (!node.is_null()) {
    DecreaseAvailableBytes(*node_size);
    DCHECK(IsVeryLong() || Available() == SumFreeLists());
  }
  if (category->is_empty()) {
    RemoveCategory(category);
  }
  return node;
}

FreeSpace FreeList::SearchForNodeInList(FreeListCategoryType type,
                                        size_t minimum_size,
                                        size_t* node_size) {
  FreeListCategoryIterator it(this, type);
  FreeSpace node;
  while (it.HasNext()) {
    FreeListCategory* current = it.Next();
    node = current->SearchForNodeInList(minimum_size, node_size);
    if (!node.is_null()) {
      DecreaseAvailableBytes(*node_size);
      DCHECK(IsVeryLong() || Available() == SumFreeLists());
      if (current->is_empty()) {
        RemoveCategory(current);
      }
      return node;
    }
  }
  return node;
}

size_t FreeList::Free(Address start, size_t size_in_bytes, FreeMode mode) {
  Page* page = Page::FromAddress(start);
  page->DecreaseAllocatedBytes(size_in_bytes);

  // Blocks have to be a minimum size to hold free list items.
  if (size_in_bytes < min_block_size_) {
    page->add_wasted_memory(size_in_bytes);
    wasted_bytes_ += size_in_bytes;
    return size_in_bytes;
  }

  // Insert other blocks at the head of a free list of the appropriate
  // magnitude.
  FreeListCategoryType type = SelectFreeListCategoryType(size_in_bytes);
  page->free_list_category(type)->Free(start, size_in_bytes, mode, this);
  DCHECK_EQ(page->AvailableInFreeList(),
            page->AvailableInFreeListFromAllocatedBytes());
  return 0;
}

// ------------------------------------------------
// FreeListLegacy implementation

FreeListLegacy::FreeListLegacy() {
  // Initializing base (FreeList) fields
  number_of_categories_ = kHuge + 1;
  last_category_ = kHuge;
  min_block_size_ = kMinBlockSize;
  categories_ = new FreeListCategory*[number_of_categories_]();

  Reset();
}

FreeListLegacy::~FreeListLegacy() { delete[] categories_; }

FreeSpace FreeListLegacy::Allocate(size_t size_in_bytes, size_t* node_size,
                                   AllocationOrigin origin) {
  DCHECK_GE(kMaxBlockSize, size_in_bytes);
  FreeSpace node;
  // First try the allocation fast path: try to allocate the minimum element
  // size of a free list category. This operation is constant time.
  FreeListCategoryType type =
      SelectFastAllocationFreeListCategoryType(size_in_bytes);
  for (int i = type; i < kHuge && node.is_null(); i++) {
    node = TryFindNodeIn(static_cast<FreeListCategoryType>(i), size_in_bytes,
                         node_size);
  }

  if (node.is_null()) {
    // Next search the huge list for free list nodes. This takes linear time in
    // the number of huge elements.
    node = SearchForNodeInList(kHuge, size_in_bytes, node_size);
  }

  if (node.is_null() && type != kHuge) {
    // We didn't find anything in the huge list.
    type = SelectFreeListCategoryType(size_in_bytes);

    if (type == kTiniest) {
      // For this tiniest object, the tiny list hasn't been searched yet.
      // Now searching the tiny list.
      node = TryFindNodeIn(kTiny, size_in_bytes, node_size);
    }

    if (node.is_null()) {
      // Now search the best fitting free list for a node that has at least the
      // requested size.
      node = TryFindNodeIn(type, size_in_bytes, node_size);
    }
  }

  if (!node.is_null()) {
    Page::FromHeapObject(node)->IncreaseAllocatedBytes(*node_size);
  }

  DCHECK(IsVeryLong() || Available() == SumFreeLists());
  return node;
}

// ------------------------------------------------
// FreeListFastAlloc implementation

FreeListFastAlloc::FreeListFastAlloc() {
  // Initializing base (FreeList) fields
  number_of_categories_ = kHuge + 1;
  last_category_ = kHuge;
  min_block_size_ = kMinBlockSize;
  categories_ = new FreeListCategory*[number_of_categories_]();

  Reset();
}

FreeListFastAlloc::~FreeListFastAlloc() { delete[] categories_; }

FreeSpace FreeListFastAlloc::Allocate(size_t size_in_bytes, size_t* node_size,
                                      AllocationOrigin origin) {
  DCHECK_GE(kMaxBlockSize, size_in_bytes);
  FreeSpace node;
  // Try to allocate the biggest element possible (to make the most of later
  // bump-pointer allocations).
  FreeListCategoryType type = SelectFreeListCategoryType(size_in_bytes);
  for (int i = kHuge; i >= type && node.is_null(); i--) {
    node = TryFindNodeIn(static_cast<FreeListCategoryType>(i), size_in_bytes,
                         node_size);
  }

  if (!node.is_null()) {
    Page::FromHeapObject(node)->IncreaseAllocatedBytes(*node_size);
  }

  DCHECK(IsVeryLong() || Available() == SumFreeLists());
  return node;
}

// ------------------------------------------------
// FreeListMany implementation

constexpr unsigned int FreeListMany::categories_min[kNumberOfCategories];

FreeListMany::FreeListMany() {
  // Initializing base (FreeList) fields
  number_of_categories_ = kNumberOfCategories;
  last_category_ = number_of_categories_ - 1;
  min_block_size_ = kMinBlockSize;
  categories_ = new FreeListCategory*[number_of_categories_]();

  Reset();
}

FreeListMany::~FreeListMany() { delete[] categories_; }

size_t FreeListMany::GuaranteedAllocatable(size_t maximum_freed) {
  if (maximum_freed < categories_min[0]) {
    return 0;
  }
  for (int cat = kFirstCategory + 1; cat <= last_category_; cat++) {
    if (maximum_freed < categories_min[cat]) {
      return categories_min[cat - 1];
    }
  }
  return maximum_freed;
}

Page* FreeListMany::GetPageForSize(size_t size_in_bytes) {
  FreeListCategoryType minimum_category =
      SelectFreeListCategoryType(size_in_bytes);
  Page* page = nullptr;
  for (int cat = minimum_category + 1; !page && cat <= last_category_; cat++) {
    page = GetPageForCategoryType(cat);
  }
  if (!page) {
    // Might return a page in which |size_in_bytes| will not fit.
    page = GetPageForCategoryType(minimum_category);
  }
  return page;
}

FreeSpace FreeListMany::Allocate(size_t size_in_bytes, size_t* node_size,
                                 AllocationOrigin origin) {
  DCHECK_GE(kMaxBlockSize, size_in_bytes);
  FreeSpace node;
  FreeListCategoryType type = SelectFreeListCategoryType(size_in_bytes);
  for (int i = type; i < last_category_ && node.is_null(); i++) {
    node = TryFindNodeIn(static_cast<FreeListCategoryType>(i), size_in_bytes,
                         node_size);
  }

  if (node.is_null()) {
    // Searching each element of the last category.
    node = SearchForNodeInList(last_category_, size_in_bytes, node_size);
  }

  if (!node.is_null()) {
    Page::FromHeapObject(node)->IncreaseAllocatedBytes(*node_size);
  }

  DCHECK(IsVeryLong() || Available() == SumFreeLists());
  return node;
}

// ------------------------------------------------
// FreeListManyCached implementation

FreeListManyCached::FreeListManyCached() { ResetCache(); }

void FreeListManyCached::Reset() {
  ResetCache();
  FreeListMany::Reset();
}

bool FreeListManyCached::AddCategory(FreeListCategory* category) {
  bool was_added = FreeList::AddCategory(category);

  // Updating cache
  if (was_added) {
    UpdateCacheAfterAddition(category->type_);
  }

#ifdef DEBUG
  CheckCacheIntegrity();
#endif

  return was_added;
}

void FreeListManyCached::RemoveCategory(FreeListCategory* category) {
  FreeList::RemoveCategory(category);

  // Updating cache
  int type = category->type_;
  if (categories_[type] == nullptr) {
    UpdateCacheAfterRemoval(type);
  }

#ifdef DEBUG
  CheckCacheIntegrity();
#endif
}

size_t FreeListManyCached::Free(Address start, size_t size_in_bytes,
                                FreeMode mode) {
  Page* page = Page::FromAddress(start);
  page->DecreaseAllocatedBytes(size_in_bytes);

  // Blocks have to be a minimum size to hold free list items.
  if (size_in_bytes < min_block_size_) {
    page->add_wasted_memory(size_in_bytes);
    wasted_bytes_ += size_in_bytes;
    return size_in_bytes;
  }

  // Insert other blocks at the head of a free list of the appropriate
  // magnitude.
  FreeListCategoryType type = SelectFreeListCategoryType(size_in_bytes);
  page->free_list_category(type)->Free(start, size_in_bytes, mode, this);

  // Updating cache
  if (mode == kLinkCategory) {
    UpdateCacheAfterAddition(type);

#ifdef DEBUG
    CheckCacheIntegrity();
#endif
  }

  DCHECK_EQ(page->AvailableInFreeList(),
            page->AvailableInFreeListFromAllocatedBytes());
  return 0;
}

FreeSpace FreeListManyCached::Allocate(size_t size_in_bytes, size_t* node_size,
                                       AllocationOrigin origin) {
  USE(origin);
  DCHECK_GE(kMaxBlockSize, size_in_bytes);

  FreeSpace node;
  FreeListCategoryType type = SelectFreeListCategoryType(size_in_bytes);
  type = next_nonempty_category[type];
  for (; type < last_category_; type = next_nonempty_category[type + 1]) {
    node = TryFindNodeIn(type, size_in_bytes, node_size);
    if (!node.is_null()) break;
  }

  if (node.is_null()) {
    // Searching each element of the last category.
    type = last_category_;
    node = SearchForNodeInList(type, size_in_bytes, node_size);
  }

  // Updating cache
  if (!node.is_null() && categories_[type] == nullptr) {
    UpdateCacheAfterRemoval(type);
  }

#ifdef DEBUG
  CheckCacheIntegrity();
#endif

  if (!node.is_null()) {
    Page::FromHeapObject(node)->IncreaseAllocatedBytes(*node_size);
  }

  DCHECK(IsVeryLong() || Available() == SumFreeLists());
  return node;
}

// ------------------------------------------------
// FreeListManyCachedFastPath implementation

FreeSpace FreeListManyCachedFastPath::Allocate(size_t size_in_bytes,
                                               size_t* node_size,
                                               AllocationOrigin origin) {
  USE(origin);
  DCHECK_GE(kMaxBlockSize, size_in_bytes);
  FreeSpace node;

  // Fast path part 1: searching the last categories
  FreeListCategoryType first_category =
      SelectFastAllocationFreeListCategoryType(size_in_bytes);
  FreeListCategoryType type = first_category;
  for (type = next_nonempty_category[type]; type <= last_category_;
       type = next_nonempty_category[type + 1]) {
    node = TryFindNodeIn(type, size_in_bytes, node_size);
    if (!node.is_null()) break;
  }

  // Fast path part 2: searching the medium categories for tiny objects
  if (node.is_null()) {
    if (size_in_bytes <= kTinyObjectMaxSize) {
      for (type = next_nonempty_category[kFastPathFallBackTiny];
           type < kFastPathFirstCategory;
           type = next_nonempty_category[type + 1]) {
        node = TryFindNodeIn(type, size_in_bytes, node_size);
        if (!node.is_null()) break;
      }
    }
  }

  // Searching the last category
  if (node.is_null()) {
    // Searching each element of the last category.
    type = last_category_;
    node = SearchForNodeInList(type, size_in_bytes, node_size);
  }

  // Finally, search the most precise category
  if (node.is_null()) {
    type = SelectFreeListCategoryType(size_in_bytes);
    for (type = next_nonempty_category[type]; type < first_category;
         type = next_nonempty_category[type + 1]) {
      node = TryFindNodeIn(type, size_in_bytes, node_size);
      if (!node.is_null()) break;
    }
  }

  // Updating cache
  if (!node.is_null() && categories_[type] == nullptr) {
    UpdateCacheAfterRemoval(type);
  }

#ifdef DEBUG
  CheckCacheIntegrity();
#endif

  if (!node.is_null()) {
    Page::FromHeapObject(node)->IncreaseAllocatedBytes(*node_size);
  }

  DCHECK(IsVeryLong() || Available() == SumFreeLists());
  return node;
}

// ------------------------------------------------
// FreeListManyCachedOrigin implementation

FreeSpace FreeListManyCachedOrigin::Allocate(size_t size_in_bytes,
                                             size_t* node_size,
                                             AllocationOrigin origin) {
  if (origin == AllocationOrigin::kGC) {
    return FreeListManyCached::Allocate(size_in_bytes, node_size, origin);
  } else {
    return FreeListManyCachedFastPath::Allocate(size_in_bytes, node_size,
                                                origin);
  }
}

// ------------------------------------------------
// FreeListMap implementation

FreeListMap::FreeListMap() {
  // Initializing base (FreeList) fields
  number_of_categories_ = 1;
  last_category_ = kOnlyCategory;
  min_block_size_ = kMinBlockSize;
  categories_ = new FreeListCategory*[number_of_categories_]();

  Reset();
}

size_t FreeListMap::GuaranteedAllocatable(size_t maximum_freed) {
  return maximum_freed;
}

Page* FreeListMap::GetPageForSize(size_t size_in_bytes) {
  return GetPageForCategoryType(kOnlyCategory);
}

FreeListMap::~FreeListMap() { delete[] categories_; }

FreeSpace FreeListMap::Allocate(size_t size_in_bytes, size_t* node_size,
                                AllocationOrigin origin) {
  DCHECK_GE(kMaxBlockSize, size_in_bytes);

  // The following DCHECK ensures that maps are allocated one by one (ie,
  // without folding). This assumption currently holds. However, if it were to
  // become untrue in the future, you'll get an error here. To fix it, I would
  // suggest removing the DCHECK, and replacing TryFindNodeIn by
  // SearchForNodeInList below.
  DCHECK_EQ(size_in_bytes, Map::kSize);

  FreeSpace node = TryFindNodeIn(kOnlyCategory, size_in_bytes, node_size);

  if (!node.is_null()) {
    Page::FromHeapObject(node)->IncreaseAllocatedBytes(*node_size);
  }

  DCHECK_IMPLIES(node.is_null(), IsEmpty());
  return node;
}

// ------------------------------------------------
// Generic FreeList methods (non alloc/free related)

void FreeList::Reset() {
  ForAllFreeListCategories(
      [this](FreeListCategory* category) { category->Reset(this); });
  for (int i = kFirstCategory; i < number_of_categories_; i++) {
    categories_[i] = nullptr;
  }
  wasted_bytes_ = 0;
  available_ = 0;
}

size_t FreeList::EvictFreeListItems(Page* page) {
  size_t sum = 0;
  page->ForAllFreeListCategories([this, &sum](FreeListCategory* category) {
    sum += category->available();
    RemoveCategory(category);
    category->Reset(this);
  });
  return sum;
}

void FreeList::RepairLists(Heap* heap) {
  ForAllFreeListCategories(
      [heap](FreeListCategory* category) { category->RepairFreeList(heap); });
}

bool FreeList::AddCategory(FreeListCategory* category) {
  FreeListCategoryType type = category->type_;
  DCHECK_LT(type, number_of_categories_);
  FreeListCategory* top = categories_[type];

  if (category->is_empty()) return false;
  DCHECK_NE(top, category);

  // Common double-linked list insertion.
  if (top != nullptr) {
    top->set_prev(category);
  }
  category->set_next(top);
  categories_[type] = category;

  IncreaseAvailableBytes(category->available());
  return true;
}

void FreeList::RemoveCategory(FreeListCategory* category) {
  FreeListCategoryType type = category->type_;
  DCHECK_LT(type, number_of_categories_);
  FreeListCategory* top = categories_[type];

  if (category->is_linked(this)) {
    DecreaseAvailableBytes(category->available());
  }

  // Common double-linked list removal.
  if (top == category) {
    categories_[type] = category->next();
  }
  if (category->prev() != nullptr) {
    category->prev()->set_next(category->next());
  }
  if (category->next() != nullptr) {
    category->next()->set_prev(category->prev());
  }
  category->set_next(nullptr);
  category->set_prev(nullptr);
}

void FreeList::PrintCategories(FreeListCategoryType type) {
  FreeListCategoryIterator it(this, type);
  PrintF("FreeList[%p, top=%p, %d] ", static_cast<void*>(this),
         static_cast<void*>(categories_[type]), type);
  while (it.HasNext()) {
    FreeListCategory* current = it.Next();
    PrintF("%p -> ", static_cast<void*>(current));
  }
  PrintF("null\n");
}

int MemoryChunk::FreeListsLength() {
  int length = 0;
  for (int cat = kFirstCategory; cat <= owner()->free_list()->last_category();
       cat++) {
    if (categories_[cat] != nullptr) {
      length += categories_[cat]->FreeListLength();
    }
  }
  return length;
}

size_t FreeListCategory::SumFreeList() {
  size_t sum = 0;
  FreeSpace cur = top();
  while (!cur.is_null()) {
    // We can't use "cur->map()" here because both cur's map and the
    // root can be null during bootstrapping.
    DCHECK(cur.map_slot().contains_value(Page::FromHeapObject(cur)
                                             ->heap()
                                             ->isolate()
                                             ->root(RootIndex::kFreeSpaceMap)
                                             .ptr()));
    sum += cur.relaxed_read_size();
    cur = cur.next();
  }
  return sum;
}
int FreeListCategory::FreeListLength() {
  int length = 0;
  FreeSpace cur = top();
  while (!cur.is_null()) {
    length++;
    cur = cur.next();
  }
  return length;
}

#ifdef DEBUG
bool FreeList::IsVeryLong() {
  int len = 0;
  for (int i = kFirstCategory; i < number_of_categories_; i++) {
    FreeListCategoryIterator it(this, static_cast<FreeListCategoryType>(i));
    while (it.HasNext()) {
      len += it.Next()->FreeListLength();
      if (len >= FreeListCategory::kVeryLongFreeList) return true;
    }
  }
  return false;
}


// This can take a very long time because it is linear in the number of entries
// on the free list, so it should not be called if FreeListLength returns
// kVeryLongFreeList.
size_t FreeList::SumFreeLists() {
  size_t sum = 0;
  ForAllFreeListCategories(
      [&sum](FreeListCategory* category) { sum += category->SumFreeList(); });
  return sum;
}
#endif

}  // namespace internal
}  // namespace v8
