// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/spaces.h"

#include <algorithm>
#include <cinttypes>
#include <utility>

#include "src/base/bits.h"
#include "src/base/macros.h"
#include "src/base/optional.h"
#include "src/base/platform/semaphore.h"
#include "src/common/globals.h"
#include "src/execution/vm-state-inl.h"
#include "src/heap/array-buffer-sweeper.h"
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
#include "src/heap/sweeper.h"
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

static base::LazyInstance<CodeRangeAddressHint>::type code_range_address_hint =
    LAZY_INSTANCE_INITIALIZER;

Address CodeRangeAddressHint::GetAddressHint(size_t code_range_size) {
  base::MutexGuard guard(&mutex_);
  auto it = recently_freed_.find(code_range_size);
  if (it == recently_freed_.end() || it->second.empty()) {
    return reinterpret_cast<Address>(GetRandomMmapAddr());
  }
  Address result = it->second.back();
  it->second.pop_back();
  return result;
}

void CodeRangeAddressHint::NotifyFreedCodeRange(Address code_range_start,
                                                size_t code_range_size) {
  base::MutexGuard guard(&mutex_);
  recently_freed_[code_range_size].push_back(code_range_start);
}

// -----------------------------------------------------------------------------
// MemoryAllocator
//

MemoryAllocator::MemoryAllocator(Isolate* isolate, size_t capacity,
                                 size_t code_range_size)
    : isolate_(isolate),
      data_page_allocator_(isolate->page_allocator()),
      code_page_allocator_(nullptr),
      capacity_(RoundUp(capacity, Page::kPageSize)),
      size_(0),
      size_executable_(0),
      lowest_ever_allocated_(static_cast<Address>(-1ll)),
      highest_ever_allocated_(kNullAddress),
      unmapper_(isolate->heap(), this) {
  InitializeCodePageAllocator(data_page_allocator_, code_range_size);
}

void MemoryAllocator::InitializeCodePageAllocator(
    v8::PageAllocator* page_allocator, size_t requested) {
  DCHECK_NULL(code_page_allocator_instance_.get());

  code_page_allocator_ = page_allocator;

  if (requested == 0) {
    if (!isolate_->RequiresCodeRange()) return;
    // When a target requires the code range feature, we put all code objects
    // in a kMaximalCodeRangeSize range of virtual address space, so that
    // they can call each other with near calls.
    requested = kMaximalCodeRangeSize;
  } else if (requested <= kMinimumCodeRangeSize) {
    requested = kMinimumCodeRangeSize;
  }

  const size_t reserved_area =
      kReservedCodeRangePages * MemoryAllocator::GetCommitPageSize();
  if (requested < (kMaximalCodeRangeSize - reserved_area)) {
    requested += RoundUp(reserved_area, MemoryChunk::kPageSize);
    // Fullfilling both reserved pages requirement and huge code area
    // alignments is not supported (requires re-implementation).
    DCHECK_LE(kMinExpectedOSPageSize, page_allocator->AllocatePageSize());
  }
  DCHECK(!isolate_->RequiresCodeRange() || requested <= kMaximalCodeRangeSize);

  Address hint =
      RoundDown(code_range_address_hint.Pointer()->GetAddressHint(requested),
                page_allocator->AllocatePageSize());
  VirtualMemory reservation(
      page_allocator, requested, reinterpret_cast<void*>(hint),
      Max(kMinExpectedOSPageSize, page_allocator->AllocatePageSize()));
  if (!reservation.IsReserved()) {
    V8::FatalProcessOutOfMemory(isolate_,
                                "CodeRange setup: allocate virtual memory");
  }
  code_range_ = reservation.region();
  isolate_->AddCodeRange(code_range_.begin(), code_range_.size());

  // We are sure that we have mapped a block of requested addresses.
  DCHECK_GE(reservation.size(), requested);
  Address base = reservation.address();

  // On some platforms, specifically Win64, we need to reserve some pages at
  // the beginning of an executable space. See
  //   https://cs.chromium.org/chromium/src/components/crash/content/
  //     app/crashpad_win.cc?rcl=fd680447881449fba2edcf0589320e7253719212&l=204
  // for details.
  if (reserved_area > 0) {
    if (!reservation.SetPermissions(base, reserved_area,
                                    PageAllocator::kReadWrite))
      V8::FatalProcessOutOfMemory(isolate_, "CodeRange setup: set permissions");

    base += reserved_area;
  }
  Address aligned_base = RoundUp(base, MemoryChunk::kAlignment);
  size_t size =
      RoundDown(reservation.size() - (aligned_base - base) - reserved_area,
                MemoryChunk::kPageSize);
  DCHECK(IsAligned(aligned_base, kMinExpectedOSPageSize));

  LOG(isolate_,
      NewEvent("CodeRange", reinterpret_cast<void*>(reservation.address()),
               requested));

  code_reservation_ = std::move(reservation);
  code_page_allocator_instance_ = std::make_unique<base::BoundedPageAllocator>(
      page_allocator, aligned_base, size,
      static_cast<size_t>(MemoryChunk::kAlignment));
  code_page_allocator_ = code_page_allocator_instance_.get();
}

void MemoryAllocator::TearDown() {
  unmapper()->TearDown();

  // Check that spaces were torn down before MemoryAllocator.
  DCHECK_EQ(size_, 0u);
  // TODO(gc) this will be true again when we fix FreeMemory.
  // DCHECK_EQ(0, size_executable_);
  capacity_ = 0;

  if (last_chunk_.IsReserved()) {
    last_chunk_.Free();
  }

  if (code_page_allocator_instance_.get()) {
    DCHECK(!code_range_.is_empty());
    code_range_address_hint.Pointer()->NotifyFreedCodeRange(code_range_.begin(),
                                                            code_range_.size());
    code_range_ = base::AddressRegion();
    code_page_allocator_instance_.reset();
  }
  code_page_allocator_ = nullptr;
  data_page_allocator_ = nullptr;
}

class MemoryAllocator::Unmapper::UnmapFreeMemoryTask : public CancelableTask {
 public:
  explicit UnmapFreeMemoryTask(Isolate* isolate, Unmapper* unmapper)
      : CancelableTask(isolate),
        unmapper_(unmapper),
        tracer_(isolate->heap()->tracer()) {}

 private:
  void RunInternal() override {
    TRACE_BACKGROUND_GC(tracer_,
                        GCTracer::BackgroundScope::BACKGROUND_UNMAPPER);
    unmapper_->PerformFreeMemoryOnQueuedChunks<FreeMode::kUncommitPooled>();
    unmapper_->active_unmapping_tasks_--;
    unmapper_->pending_unmapping_tasks_semaphore_.Signal();
    if (FLAG_trace_unmapper) {
      PrintIsolate(unmapper_->heap_->isolate(),
                   "UnmapFreeMemoryTask Done: id=%" PRIu64 "\n", id());
    }
  }

  Unmapper* const unmapper_;
  GCTracer* const tracer_;
  DISALLOW_COPY_AND_ASSIGN(UnmapFreeMemoryTask);
};

void MemoryAllocator::Unmapper::FreeQueuedChunks() {
  if (!heap_->IsTearingDown() && FLAG_concurrent_sweeping) {
    if (!MakeRoomForNewTasks()) {
      // kMaxUnmapperTasks are already running. Avoid creating any more.
      if (FLAG_trace_unmapper) {
        PrintIsolate(heap_->isolate(),
                     "Unmapper::FreeQueuedChunks: reached task limit (%d)\n",
                     kMaxUnmapperTasks);
      }
      return;
    }
    auto task = std::make_unique<UnmapFreeMemoryTask>(heap_->isolate(), this);
    if (FLAG_trace_unmapper) {
      PrintIsolate(heap_->isolate(),
                   "Unmapper::FreeQueuedChunks: new task id=%" PRIu64 "\n",
                   task->id());
    }
    DCHECK_LT(pending_unmapping_tasks_, kMaxUnmapperTasks);
    DCHECK_LE(active_unmapping_tasks_, pending_unmapping_tasks_);
    DCHECK_GE(active_unmapping_tasks_, 0);
    active_unmapping_tasks_++;
    task_ids_[pending_unmapping_tasks_++] = task->id();
    V8::GetCurrentPlatform()->CallOnWorkerThread(std::move(task));
  } else {
    PerformFreeMemoryOnQueuedChunks<FreeMode::kUncommitPooled>();
  }
}

void MemoryAllocator::Unmapper::CancelAndWaitForPendingTasks() {
  for (int i = 0; i < pending_unmapping_tasks_; i++) {
    if (heap_->isolate()->cancelable_task_manager()->TryAbort(task_ids_[i]) !=
        TryAbortResult::kTaskAborted) {
      pending_unmapping_tasks_semaphore_.Wait();
    }
  }
  pending_unmapping_tasks_ = 0;
  active_unmapping_tasks_ = 0;

  if (FLAG_trace_unmapper) {
    PrintIsolate(
        heap_->isolate(),
        "Unmapper::CancelAndWaitForPendingTasks: no tasks remaining\n");
  }
}

void MemoryAllocator::Unmapper::PrepareForGC() {
  // Free non-regular chunks because they cannot be re-used.
  PerformFreeMemoryOnQueuedNonRegularChunks();
}

void MemoryAllocator::Unmapper::EnsureUnmappingCompleted() {
  CancelAndWaitForPendingTasks();
  PerformFreeMemoryOnQueuedChunks<FreeMode::kReleasePooled>();
}

bool MemoryAllocator::Unmapper::MakeRoomForNewTasks() {
  DCHECK_LE(pending_unmapping_tasks_, kMaxUnmapperTasks);

  if (active_unmapping_tasks_ == 0 && pending_unmapping_tasks_ > 0) {
    // All previous unmapping tasks have been run to completion.
    // Finalize those tasks to make room for new ones.
    CancelAndWaitForPendingTasks();
  }
  return pending_unmapping_tasks_ != kMaxUnmapperTasks;
}

void MemoryAllocator::Unmapper::PerformFreeMemoryOnQueuedNonRegularChunks() {
  MemoryChunk* chunk = nullptr;
  while ((chunk = GetMemoryChunkSafe<kNonRegular>()) != nullptr) {
    allocator_->PerformFreeMemory(chunk);
  }
}

template <MemoryAllocator::Unmapper::FreeMode mode>
void MemoryAllocator::Unmapper::PerformFreeMemoryOnQueuedChunks() {
  MemoryChunk* chunk = nullptr;
  if (FLAG_trace_unmapper) {
    PrintIsolate(
        heap_->isolate(),
        "Unmapper::PerformFreeMemoryOnQueuedChunks: %d queued chunks\n",
        NumberOfChunks());
  }
  // Regular chunks.
  while ((chunk = GetMemoryChunkSafe<kRegular>()) != nullptr) {
    bool pooled = chunk->IsFlagSet(MemoryChunk::POOLED);
    allocator_->PerformFreeMemory(chunk);
    if (pooled) AddMemoryChunkSafe<kPooled>(chunk);
  }
  if (mode == MemoryAllocator::Unmapper::FreeMode::kReleasePooled) {
    // The previous loop uncommitted any pages marked as pooled and added them
    // to the pooled list. In case of kReleasePooled we need to free them
    // though.
    while ((chunk = GetMemoryChunkSafe<kPooled>()) != nullptr) {
      allocator_->Free<MemoryAllocator::kAlreadyPooled>(chunk);
    }
  }
  PerformFreeMemoryOnQueuedNonRegularChunks();
}

void MemoryAllocator::Unmapper::TearDown() {
  CHECK_EQ(0, pending_unmapping_tasks_);
  PerformFreeMemoryOnQueuedChunks<FreeMode::kReleasePooled>();
  for (int i = 0; i < kNumberOfChunkQueues; i++) {
    DCHECK(chunks_[i].empty());
  }
}

size_t MemoryAllocator::Unmapper::NumberOfCommittedChunks() {
  base::MutexGuard guard(&mutex_);
  return chunks_[kRegular].size() + chunks_[kNonRegular].size();
}

int MemoryAllocator::Unmapper::NumberOfChunks() {
  base::MutexGuard guard(&mutex_);
  size_t result = 0;
  for (int i = 0; i < kNumberOfChunkQueues; i++) {
    result += chunks_[i].size();
  }
  return static_cast<int>(result);
}

size_t MemoryAllocator::Unmapper::CommittedBufferedMemory() {
  base::MutexGuard guard(&mutex_);

  size_t sum = 0;
  // kPooled chunks are already uncommited. We only have to account for
  // kRegular and kNonRegular chunks.
  for (auto& chunk : chunks_[kRegular]) {
    sum += chunk->size();
  }
  for (auto& chunk : chunks_[kNonRegular]) {
    sum += chunk->size();
  }
  return sum;
}

bool MemoryAllocator::CommitMemory(VirtualMemory* reservation) {
  Address base = reservation->address();
  size_t size = reservation->size();
  if (!reservation->SetPermissions(base, size, PageAllocator::kReadWrite)) {
    return false;
  }
  UpdateAllocatedSpaceLimits(base, base + size);
  return true;
}

bool MemoryAllocator::UncommitMemory(VirtualMemory* reservation) {
  size_t size = reservation->size();
  if (!reservation->SetPermissions(reservation->address(), size,
                                   PageAllocator::kNoAccess)) {
    return false;
  }
  return true;
}

void MemoryAllocator::FreeMemory(v8::PageAllocator* page_allocator,
                                 Address base, size_t size) {
  CHECK(FreePages(page_allocator, reinterpret_cast<void*>(base), size));
}

Address MemoryAllocator::AllocateAlignedMemory(
    size_t reserve_size, size_t commit_size, size_t alignment,
    Executability executable, void* hint, VirtualMemory* controller) {
  v8::PageAllocator* page_allocator = this->page_allocator(executable);
  DCHECK(commit_size <= reserve_size);
  VirtualMemory reservation(page_allocator, reserve_size, hint, alignment);
  if (!reservation.IsReserved()) return kNullAddress;
  Address base = reservation.address();
  size_ += reservation.size();

  if (executable == EXECUTABLE) {
    if (!CommitExecutableMemory(&reservation, base, commit_size,
                                reserve_size)) {
      base = kNullAddress;
    }
  } else {
    if (reservation.SetPermissions(base, commit_size,
                                   PageAllocator::kReadWrite)) {
      UpdateAllocatedSpaceLimits(base, base + commit_size);
    } else {
      base = kNullAddress;
    }
  }

  if (base == kNullAddress) {
    // Failed to commit the body. Free the mapping and any partially committed
    // regions inside it.
    reservation.Free();
    size_ -= reserve_size;
    return kNullAddress;
  }

  *controller = std::move(reservation);
  return base;
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

MemoryChunk* MemoryAllocator::AllocateChunk(size_t reserve_area_size,
                                            size_t commit_area_size,
                                            Executability executable,
                                            Space* owner) {
  DCHECK_LE(commit_area_size, reserve_area_size);

  size_t chunk_size;
  Heap* heap = isolate_->heap();
  Address base = kNullAddress;
  VirtualMemory reservation;
  Address area_start = kNullAddress;
  Address area_end = kNullAddress;
  void* address_hint =
      AlignedAddress(heap->GetRandomMmapAddr(), MemoryChunk::kAlignment);

  //
  // MemoryChunk layout:
  //
  //             Executable
  // +----------------------------+<- base aligned with MemoryChunk::kAlignment
  // |           Header           |
  // +----------------------------+<- base + CodePageGuardStartOffset
  // |           Guard            |
  // +----------------------------+<- area_start_
  // |           Area             |
  // +----------------------------+<- area_end_ (area_start + commit_area_size)
  // |   Committed but not used   |
  // +----------------------------+<- aligned at OS page boundary
  // | Reserved but not committed |
  // +----------------------------+<- aligned at OS page boundary
  // |           Guard            |
  // +----------------------------+<- base + chunk_size
  //
  //           Non-executable
  // +----------------------------+<- base aligned with MemoryChunk::kAlignment
  // |          Header            |
  // +----------------------------+<- area_start_ (base + area_start_)
  // |           Area             |
  // +----------------------------+<- area_end_ (area_start + commit_area_size)
  // |  Committed but not used    |
  // +----------------------------+<- aligned at OS page boundary
  // | Reserved but not committed |
  // +----------------------------+<- base + chunk_size
  //

  if (executable == EXECUTABLE) {
    chunk_size = ::RoundUp(MemoryChunkLayout::ObjectStartOffsetInCodePage() +
                               reserve_area_size +
                               MemoryChunkLayout::CodePageGuardSize(),
                           GetCommitPageSize());

    // Size of header (not executable) plus area (executable).
    size_t commit_size = ::RoundUp(
        MemoryChunkLayout::CodePageGuardStartOffset() + commit_area_size,
        GetCommitPageSize());
    base =
        AllocateAlignedMemory(chunk_size, commit_size, MemoryChunk::kAlignment,
                              executable, address_hint, &reservation);
    if (base == kNullAddress) return nullptr;
    // Update executable memory size.
    size_executable_ += reservation.size();

    if (Heap::ShouldZapGarbage()) {
      ZapBlock(base, MemoryChunkLayout::CodePageGuardStartOffset(), kZapValue);
      ZapBlock(base + MemoryChunkLayout::ObjectStartOffsetInCodePage(),
               commit_area_size, kZapValue);
    }

    area_start = base + MemoryChunkLayout::ObjectStartOffsetInCodePage();
    area_end = area_start + commit_area_size;
  } else {
    chunk_size = ::RoundUp(
        MemoryChunkLayout::ObjectStartOffsetInDataPage() + reserve_area_size,
        GetCommitPageSize());
    size_t commit_size = ::RoundUp(
        MemoryChunkLayout::ObjectStartOffsetInDataPage() + commit_area_size,
        GetCommitPageSize());
    base =
        AllocateAlignedMemory(chunk_size, commit_size, MemoryChunk::kAlignment,
                              executable, address_hint, &reservation);

    if (base == kNullAddress) return nullptr;

    if (Heap::ShouldZapGarbage()) {
      ZapBlock(
          base,
          MemoryChunkLayout::ObjectStartOffsetInDataPage() + commit_area_size,
          kZapValue);
    }

    area_start = base + MemoryChunkLayout::ObjectStartOffsetInDataPage();
    area_end = area_start + commit_area_size;
  }

  // Use chunk_size for statistics because we assume that  treat reserved but
  // not-yet committed memory regions of chunks as allocated.
  LOG(isolate_,
      NewEvent("MemoryChunk", reinterpret_cast<void*>(base), chunk_size));

  // We cannot use the last chunk in the address space because we would
  // overflow when comparing top and limit if this chunk is used for a
  // linear allocation area.
  if ((base + chunk_size) == 0u) {
    CHECK(!last_chunk_.IsReserved());
    last_chunk_ = std::move(reservation);
    UncommitMemory(&last_chunk_);
    size_ -= chunk_size;
    if (executable == EXECUTABLE) {
      size_executable_ -= chunk_size;
    }
    CHECK(last_chunk_.IsReserved());
    return AllocateChunk(reserve_area_size, commit_area_size, executable,
                         owner);
  }

  MemoryChunk* chunk =
      MemoryChunk::Initialize(heap, base, chunk_size, area_start, area_end,
                              executable, owner, std::move(reservation));

  if (chunk->executable()) RegisterExecutableMemoryChunk(chunk);
  return chunk;
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

void MemoryAllocator::PartialFreeMemory(MemoryChunk* chunk, Address start_free,
                                        size_t bytes_to_free,
                                        Address new_area_end) {
  VirtualMemory* reservation = chunk->reserved_memory();
  DCHECK(reservation->IsReserved());
  chunk->set_size(chunk->size() - bytes_to_free);
  chunk->set_area_end(new_area_end);
  if (chunk->IsFlagSet(MemoryChunk::IS_EXECUTABLE)) {
    // Add guard page at the end.
    size_t page_size = GetCommitPageSize();
    DCHECK_EQ(0, chunk->area_end() % static_cast<Address>(page_size));
    DCHECK_EQ(chunk->address() + chunk->size(),
              chunk->area_end() + MemoryChunkLayout::CodePageGuardSize());
    reservation->SetPermissions(chunk->area_end(), page_size,
                                PageAllocator::kNoAccess);
  }
  // On e.g. Windows, a reservation may be larger than a page and releasing
  // partially starting at |start_free| will also release the potentially
  // unused part behind the current page.
  const size_t released_bytes = reservation->Release(start_free);
  DCHECK_GE(size_, released_bytes);
  size_ -= released_bytes;
}

void MemoryAllocator::UnregisterMemory(MemoryChunk* chunk) {
  DCHECK(!chunk->IsFlagSet(MemoryChunk::UNREGISTERED));
  VirtualMemory* reservation = chunk->reserved_memory();
  const size_t size =
      reservation->IsReserved() ? reservation->size() : chunk->size();
  DCHECK_GE(size_, static_cast<size_t>(size));
  size_ -= size;
  if (chunk->executable() == EXECUTABLE) {
    DCHECK_GE(size_executable_, size);
    size_executable_ -= size;
  }

  if (chunk->executable()) UnregisterExecutableMemoryChunk(chunk);
  chunk->SetFlag(MemoryChunk::UNREGISTERED);
}

void MemoryAllocator::PreFreeMemory(MemoryChunk* chunk) {
  DCHECK(!chunk->IsFlagSet(MemoryChunk::PRE_FREED));
  LOG(isolate_, DeleteEvent("MemoryChunk", chunk));
  UnregisterMemory(chunk);
  isolate_->heap()->RememberUnmappedPage(reinterpret_cast<Address>(chunk),
                                         chunk->IsEvacuationCandidate());
  chunk->SetFlag(MemoryChunk::PRE_FREED);
}

void MemoryAllocator::PerformFreeMemory(MemoryChunk* chunk) {
  DCHECK(chunk->IsFlagSet(MemoryChunk::UNREGISTERED));
  DCHECK(chunk->IsFlagSet(MemoryChunk::PRE_FREED));
  chunk->ReleaseAllAllocatedMemory();

  VirtualMemory* reservation = chunk->reserved_memory();
  if (chunk->IsFlagSet(MemoryChunk::POOLED)) {
    UncommitMemory(reservation);
  } else {
    if (reservation->IsReserved()) {
      reservation->Free();
    } else {
      // Only read-only pages can have non-initialized reservation object.
      DCHECK_EQ(RO_SPACE, chunk->owner_identity());
      FreeMemory(page_allocator(chunk->executable()), chunk->address(),
                 chunk->size());
    }
  }
}

template <MemoryAllocator::FreeMode mode>
void MemoryAllocator::Free(MemoryChunk* chunk) {
  switch (mode) {
    case kFull:
      PreFreeMemory(chunk);
      PerformFreeMemory(chunk);
      break;
    case kAlreadyPooled:
      // Pooled pages cannot be touched anymore as their memory is uncommitted.
      // Pooled pages are not-executable.
      FreeMemory(data_page_allocator(), chunk->address(),
                 static_cast<size_t>(MemoryChunk::kPageSize));
      break;
    case kPooledAndQueue:
      DCHECK_EQ(chunk->size(), static_cast<size_t>(MemoryChunk::kPageSize));
      DCHECK_EQ(chunk->executable(), NOT_EXECUTABLE);
      chunk->SetFlag(MemoryChunk::POOLED);
      V8_FALLTHROUGH;
    case kPreFreeAndQueue:
      PreFreeMemory(chunk);
      // The chunks added to this queue will be freed by a concurrent thread.
      unmapper()->AddMemoryChunkSafe(chunk);
      break;
  }
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void MemoryAllocator::Free<
    MemoryAllocator::kFull>(MemoryChunk* chunk);

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void MemoryAllocator::Free<
    MemoryAllocator::kAlreadyPooled>(MemoryChunk* chunk);

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void MemoryAllocator::Free<
    MemoryAllocator::kPreFreeAndQueue>(MemoryChunk* chunk);

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void MemoryAllocator::Free<
    MemoryAllocator::kPooledAndQueue>(MemoryChunk* chunk);

template <MemoryAllocator::AllocationMode alloc_mode, typename SpaceType>
Page* MemoryAllocator::AllocatePage(size_t size, SpaceType* owner,
                                    Executability executable) {
  MemoryChunk* chunk = nullptr;
  if (alloc_mode == kPooled) {
    DCHECK_EQ(size, static_cast<size_t>(
                        MemoryChunkLayout::AllocatableMemoryInMemoryChunk(
                            owner->identity())));
    DCHECK_EQ(executable, NOT_EXECUTABLE);
    chunk = AllocatePagePooled(owner);
  }
  if (chunk == nullptr) {
    chunk = AllocateChunk(size, size, executable, owner);
  }
  if (chunk == nullptr) return nullptr;
  return owner->InitializePage(chunk);
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Page* MemoryAllocator::AllocatePage<MemoryAllocator::kRegular, PagedSpace>(
        size_t size, PagedSpace* owner, Executability executable);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Page* MemoryAllocator::AllocatePage<MemoryAllocator::kRegular, SemiSpace>(
        size_t size, SemiSpace* owner, Executability executable);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Page* MemoryAllocator::AllocatePage<MemoryAllocator::kPooled, SemiSpace>(
        size_t size, SemiSpace* owner, Executability executable);

LargePage* MemoryAllocator::AllocateLargePage(size_t size,
                                              LargeObjectSpace* owner,
                                              Executability executable) {
  MemoryChunk* chunk = AllocateChunk(size, size, executable, owner);
  if (chunk == nullptr) return nullptr;
  return LargePage::Initialize(isolate_->heap(), chunk, executable);
}

template <typename SpaceType>
MemoryChunk* MemoryAllocator::AllocatePagePooled(SpaceType* owner) {
  MemoryChunk* chunk = unmapper()->TryGetPooledMemoryChunkSafe();
  if (chunk == nullptr) return nullptr;
  const int size = MemoryChunk::kPageSize;
  const Address start = reinterpret_cast<Address>(chunk);
  const Address area_start =
      start +
      MemoryChunkLayout::ObjectStartOffsetInMemoryChunk(owner->identity());
  const Address area_end = start + size;
  // Pooled pages are always regular data pages.
  DCHECK_NE(CODE_SPACE, owner->identity());
  VirtualMemory reservation(data_page_allocator(), start, size);
  if (!CommitMemory(&reservation)) return nullptr;
  if (Heap::ShouldZapGarbage()) {
    ZapBlock(start, size, kZapValue);
  }
  MemoryChunk::Initialize(isolate_->heap(), start, size, area_start, area_end,
                          NOT_EXECUTABLE, owner, std::move(reservation));
  size_ += size;
  return chunk;
}

void MemoryAllocator::ZapBlock(Address start, size_t size,
                               uintptr_t zap_value) {
  DCHECK(IsAligned(start, kTaggedSize));
  DCHECK(IsAligned(size, kTaggedSize));
  MemsetTagged(ObjectSlot(start), Object(static_cast<Address>(zap_value)),
               size >> kTaggedSizeLog2);
}

intptr_t MemoryAllocator::GetCommitPageSize() {
  if (FLAG_v8_os_page_size != 0) {
    DCHECK(base::bits::IsPowerOfTwo(FLAG_v8_os_page_size));
    return FLAG_v8_os_page_size * KB;
  } else {
    return CommitPageSize();
  }
}

base::AddressRegion MemoryAllocator::ComputeDiscardMemoryArea(Address addr,
                                                              size_t size) {
  size_t page_size = MemoryAllocator::GetCommitPageSize();
  if (size < page_size + FreeSpace::kSize) {
    return base::AddressRegion(0, 0);
  }
  Address discardable_start = RoundUp(addr + FreeSpace::kSize, page_size);
  Address discardable_end = RoundDown(addr + size, page_size);
  if (discardable_start >= discardable_end) return base::AddressRegion(0, 0);
  return base::AddressRegion(discardable_start,
                             discardable_end - discardable_start);
}

bool MemoryAllocator::CommitExecutableMemory(VirtualMemory* vm, Address start,
                                             size_t commit_size,
                                             size_t reserved_size) {
  const size_t page_size = GetCommitPageSize();
  // All addresses and sizes must be aligned to the commit page size.
  DCHECK(IsAligned(start, page_size));
  DCHECK_EQ(0, commit_size % page_size);
  DCHECK_EQ(0, reserved_size % page_size);
  const size_t guard_size = MemoryChunkLayout::CodePageGuardSize();
  const size_t pre_guard_offset = MemoryChunkLayout::CodePageGuardStartOffset();
  const size_t code_area_offset =
      MemoryChunkLayout::ObjectStartOffsetInCodePage();
  // reserved_size includes two guard regions, commit_size does not.
  DCHECK_LE(commit_size, reserved_size - 2 * guard_size);
  const Address pre_guard_page = start + pre_guard_offset;
  const Address code_area = start + code_area_offset;
  const Address post_guard_page = start + reserved_size - guard_size;
  // Commit the non-executable header, from start to pre-code guard page.
  if (vm->SetPermissions(start, pre_guard_offset, PageAllocator::kReadWrite)) {
    // Create the pre-code guard page, following the header.
    if (vm->SetPermissions(pre_guard_page, page_size,
                           PageAllocator::kNoAccess)) {
      // Commit the executable code body.
      if (vm->SetPermissions(code_area, commit_size - pre_guard_offset,
                             PageAllocator::kReadWrite)) {
        // Create the post-code guard page.
        if (vm->SetPermissions(post_guard_page, page_size,
                               PageAllocator::kNoAccess)) {
          UpdateAllocatedSpaceLimits(start, code_area + commit_size);
          return true;
        }
        vm->SetPermissions(code_area, commit_size, PageAllocator::kNoAccess);
      }
    }
    vm->SetPermissions(start, pre_guard_offset, PageAllocator::kNoAccess);
  }
  return false;
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
