// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap-base.h"

#include "src/base/bounded-page-allocator.h"
#include "src/base/platform/platform.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-object-header-inl.h"
#include "src/heap/cppgc/heap-page-inl.h"
#include "src/heap/cppgc/heap-visitor.h"
#include "src/heap/cppgc/marker.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/prefinalizer-handler.h"
#include "src/heap/cppgc/stack.h"
#include "src/heap/cppgc/stats-collector.h"

namespace cppgc {
namespace internal {

namespace {

class ObjectSizeCounter : private HeapVisitor<ObjectSizeCounter> {
  friend class HeapVisitor<ObjectSizeCounter>;

 public:
  size_t GetSize(RawHeap* heap) {
    Traverse(heap);
    return accumulated_size_;
  }

 private:
  static size_t ObjectSize(const HeapObjectHeader* header) {
    const size_t size =
        header->IsLargeObject()
            ? static_cast<const LargePage*>(BasePage::FromPayload(header))
                  ->PayloadSize()
            : header->GetSize();
    DCHECK_GE(size, sizeof(HeapObjectHeader));
    return size - sizeof(HeapObjectHeader);
  }

  bool VisitHeapObjectHeader(HeapObjectHeader* header) {
    if (header->IsFree()) return true;
    accumulated_size_ += ObjectSize(header);
    return true;
  }

  size_t accumulated_size_ = 0;
};

#if defined(CPPGC_CAGED_HEAP)
VirtualMemory ReserveCagedHeap(v8::PageAllocator* platform_allocator) {
  DCHECK_EQ(0u,
            kCagedHeapReservationSize % platform_allocator->AllocatePageSize());

  static constexpr size_t kAllocationTries = 4;
  for (size_t i = 0; i < kAllocationTries; ++i) {
    void* hint = reinterpret_cast<void*>(RoundDown(
        reinterpret_cast<uintptr_t>(platform_allocator->GetRandomMmapAddr()),
        kCagedHeapReservationAlignment));

    VirtualMemory memory(platform_allocator, kCagedHeapReservationSize,
                         kCagedHeapReservationAlignment, hint);
    if (memory.IsReserved()) return memory;
  }

  FATAL("Fatal process out of memory: Failed to reserve memory for caged heap");
  UNREACHABLE();
}

std::unique_ptr<v8::base::BoundedPageAllocator> CreateBoundedAllocator(
    v8::PageAllocator* platform_allocator, void* caged_heap_start) {
  DCHECK(caged_heap_start);

  auto start = reinterpret_cast<v8::base::BoundedPageAllocator::Address>(
      caged_heap_start);

  return std::make_unique<v8::base::BoundedPageAllocator>(
      platform_allocator, start, kCagedHeapReservationSize, kPageSize);
}
#endif

}  // namespace

HeapBase::HeapBase(std::shared_ptr<cppgc::Platform> platform,
                   size_t custom_spaces)
    : raw_heap_(this, custom_spaces),
      platform_(std::move(platform)),
#if defined(CPPGC_CAGED_HEAP)
      reserved_area_(ReserveCagedHeap(platform_->GetPageAllocator())),
      bounded_allocator_(CreateBoundedAllocator(platform_->GetPageAllocator(),
                                                reserved_area_.address())),
      page_backend_(std::make_unique<PageBackend>(bounded_allocator_.get())),
#else
      page_backend_(
          std::make_unique<PageBackend>(platform_->GetPageAllocator())),
#endif
      stats_collector_(std::make_unique<StatsCollector>()),
      stack_(std::make_unique<Stack>(v8::base::Stack::GetStackStart())),
      prefinalizer_handler_(std::make_unique<PreFinalizerHandler>()),
      object_allocator_(&raw_heap_, page_backend_.get(),
                        stats_collector_.get()),
      sweeper_(&raw_heap_, platform_.get(), stats_collector_.get()) {
}

HeapBase::~HeapBase() = default;

size_t HeapBase::ObjectPayloadSize() const {
  return ObjectSizeCounter().GetSize(const_cast<RawHeap*>(&raw_heap()));
}

HeapBase::NoGCScope::NoGCScope(HeapBase& heap) : heap_(heap) {
  heap_.no_gc_scope_++;
}

HeapBase::NoGCScope::~NoGCScope() { heap_.no_gc_scope_--; }

}  // namespace internal
}  // namespace cppgc
