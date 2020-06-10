// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/object-allocator.h"

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/heap/cppgc/free-list.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-object-header-inl.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap-space.h"
#include "src/heap/cppgc/heap-visitor.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/object-allocator-inl.h"
#include "src/heap/cppgc/object-start-bitmap.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/stats-collector.h"
#include "src/heap/cppgc/sweeper.h"

namespace cppgc {
namespace internal {
namespace {

void AddToFreeList(NormalPageSpace* space, Address start, size_t size) {
  auto& free_list = space->free_list();
  free_list.Add({start, size});
  NormalPage::From(BasePage::FromPayload(start))
      ->object_start_bitmap()
      .SetBit(start);
}

void ReplaceLinearAllocationBuffer(NormalPageSpace* space,
                                   StatsCollector* stats_collector,
                                   Address new_buffer, size_t new_size) {
  DCHECK_NOT_NULL(space);
  DCHECK_NOT_NULL(stats_collector);

  auto& lab = space->linear_allocation_buffer();
  if (lab.size()) {
    AddToFreeList(space, lab.start(), lab.size());
    stats_collector->NotifyExplicitFree(lab.size());
  }

  lab.Set(new_buffer, new_size);
  if (new_size) {
    DCHECK_NOT_NULL(new_buffer);
    stats_collector->NotifyAllocation(new_size);
    NormalPage::From(BasePage::FromPayload(new_buffer))
        ->object_start_bitmap()
        .ClearBit(new_buffer);
  }
}

void* AllocateLargeObject(PageBackend* page_backend, LargePageSpace* space,
                          StatsCollector* stats_collector, size_t size,
                          GCInfoIndex gcinfo) {
  LargePage* page = LargePage::Create(page_backend, space, size);
  space->AddPage(page);

  auto* header = new (page->ObjectHeader())
      HeapObjectHeader(HeapObjectHeader::kLargeObjectSizeInHeader, gcinfo);

  stats_collector->NotifyAllocation(size);
  return header->Payload();
}

}  // namespace

ObjectAllocator::ObjectAllocator(RawHeap* heap, PageBackend* page_backend,
                                 StatsCollector* stats_collector)
    : raw_heap_(heap),
      page_backend_(page_backend),
      stats_collector_(stats_collector) {}

void* ObjectAllocator::OutOfLineAllocate(NormalPageSpace* space, size_t size,
                                         GCInfoIndex gcinfo) {
  void* memory = OutOfLineAllocateImpl(space, size, gcinfo);
  stats_collector_->NotifySafePointForConservativeCollection();
  return memory;
}

void* ObjectAllocator::OutOfLineAllocateImpl(NormalPageSpace* space,
                                             size_t size, GCInfoIndex gcinfo) {
  DCHECK_EQ(0, size & kAllocationMask);
  DCHECK_LE(kFreeListEntrySize, size);

  // 1. If this allocation is big enough, allocate a large object.
  if (size >= kLargeObjectSizeThreshold) {
    auto* large_space = LargePageSpace::From(
        raw_heap_->Space(RawHeap::RegularSpaceType::kLarge));
    return AllocateLargeObject(page_backend_, large_space, stats_collector_,
                               size, gcinfo);
  }

  // 2. Try to allocate from the freelist.
  if (void* result = AllocateFromFreeList(space, size, gcinfo)) {
    return result;
  }

  // 3. Lazily sweep pages of this heap until we find a freed area for
  // this allocation or we finish sweeping all pages of this heap.
  // TODO(chromium:1056170): Add lazy sweep.

  // 4. Complete sweeping.
  raw_heap_->heap()->sweeper().Finish();

  // 5. Add a new page to this heap.
  auto* new_page = NormalPage::Create(page_backend_, space);
  space->AddPage(new_page);
  AddToFreeList(space, new_page->PayloadStart(), new_page->PayloadSize());

  // 6. Try to allocate from the freelist. This allocation must succeed.
  void* result = AllocateFromFreeList(space, size, gcinfo);
  CPPGC_CHECK(result);

  return result;
}

void* ObjectAllocator::AllocateFromFreeList(NormalPageSpace* space, size_t size,
                                            GCInfoIndex gcinfo) {
  const FreeList::Block entry = space->free_list().Allocate(size);
  if (!entry.address) return nullptr;

  ReplaceLinearAllocationBuffer(
      space, stats_collector_, static_cast<Address>(entry.address), entry.size);

  return AllocateObjectOnSpace(space, size, gcinfo);
}

void ObjectAllocator::ResetLinearAllocationBuffers() {
  class Resetter : public HeapVisitor<Resetter> {
   public:
    explicit Resetter(StatsCollector* stats) : stats_collector_(stats) {}

    bool VisitLargePageSpace(LargePageSpace*) { return true; }

    bool VisitNormalPageSpace(NormalPageSpace* space) {
      ReplaceLinearAllocationBuffer(space, stats_collector_, nullptr, 0);
      return true;
    }

   private:
    StatsCollector* stats_collector_;
  } visitor(stats_collector_);

  visitor.Traverse(raw_heap_);
}

ObjectAllocator::NoAllocationScope::NoAllocationScope(
    ObjectAllocator& allocator)
    : allocator_(allocator) {
  allocator.no_allocation_scope_++;
}

ObjectAllocator::NoAllocationScope::~NoAllocationScope() {
  allocator_.no_allocation_scope_--;
}

}  // namespace internal
}  // namespace cppgc
