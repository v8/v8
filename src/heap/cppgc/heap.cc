// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap.h"

#include <memory>

#include "src/base/bounded-page-allocator.h"
#include "src/base/page-allocator.h"
#include "src/base/platform/platform.h"
#include "src/heap/cppgc/heap-object-header-inl.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap-visitor.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/stack.h"
#include "src/heap/cppgc/sweeper.h"
#include "src/heap/cppgc/virtual-memory.h"

namespace cppgc {

namespace {

void VerifyCustomSpaces(
    const std::vector<std::unique_ptr<CustomSpaceBase>>& custom_spaces) {
  // Ensures that user-provided custom spaces have indices that form a sequence
  // starting at 0.
#ifdef DEBUG
  for (size_t i = 0; i < custom_spaces.size(); ++i) {
    DCHECK_EQ(i, custom_spaces[i]->GetCustomSpaceIndex().value);
  }
#endif  // DEBUG
}

}  // namespace

std::unique_ptr<Heap> Heap::Create(std::shared_ptr<cppgc::Platform> platform,
                                   cppgc::Heap::HeapOptions options) {
  DCHECK(platform.get());
  VerifyCustomSpaces(options.custom_spaces);
  return std::make_unique<internal::Heap>(std::move(platform),
                                          options.custom_spaces.size());
}

void Heap::ForceGarbageCollectionSlow(const char* source, const char* reason,
                                      Heap::StackState stack_state) {
  internal::Heap::From(this)->CollectGarbage({stack_state});
}

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

// static
cppgc::LivenessBroker LivenessBrokerFactory::Create() {
  return cppgc::LivenessBroker();
}

Heap::Heap(std::shared_ptr<cppgc::Platform> platform, size_t custom_spaces)
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
      object_allocator_(&raw_heap_),
      sweeper_(&raw_heap_, platform_.get()),
      stack_(std::make_unique<Stack>(v8::base::Stack::GetStackStart())),
      prefinalizer_handler_(std::make_unique<PreFinalizerHandler>()) {
}

Heap::~Heap() {
  NoGCScope no_gc(this);
  // Finish already running GC if any, but don't finalize live objects.
  sweeper_.Finish();
}

void Heap::CollectGarbage(GCConfig config) {
  if (in_no_gc_scope()) return;

  epoch_++;

  // TODO(chromium:1056170): Replace with proper mark-sweep algorithm.
  // "Marking".
  marker_ = std::make_unique<Marker>(this);
  Marker::MarkingConfig marking_config(config.stack_state);
  marker_->StartMarking(marking_config);
  marker_->FinishMarking(marking_config);
  // "Sweeping and finalization".
  {
    // Pre finalizers are forbidden from allocating objects
    NoAllocationScope no_allocation_scope_(this);
    marker_->ProcessWeakness();
    prefinalizer_handler_->InvokePreFinalizers();
  }
  marker_.reset();
  {
    NoGCScope no_gc(this);
    sweeper_.Start(config.sweep_type);
  }
}

size_t Heap::ObjectPayloadSize() const {
  return ObjectSizeCounter().GetSize(const_cast<RawHeap*>(&raw_heap()));
}

Heap::NoGCScope::NoGCScope(Heap* heap) : heap_(heap) { heap_->no_gc_scope_++; }

Heap::NoGCScope::~NoGCScope() { heap_->no_gc_scope_--; }

Heap::NoAllocationScope::NoAllocationScope(Heap* heap) : heap_(heap) {
  heap_->no_allocation_scope_++;
}
Heap::NoAllocationScope::~NoAllocationScope() { heap_->no_allocation_scope_--; }

}  // namespace internal
}  // namespace cppgc
