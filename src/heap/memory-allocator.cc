// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-allocator.h"

#include <cinttypes>

#include "src/execution/isolate.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/logging/log.h"

namespace v8 {
namespace internal {

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

}  // namespace internal
}  // namespace v8
