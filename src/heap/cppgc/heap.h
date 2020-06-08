// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_HEAP_H_
#define V8_HEAP_CPPGC_HEAP_H_

#include <memory>
#include <vector>

#include "include/cppgc/heap.h"
#include "include/cppgc/internal/gc-info.h"
#include "include/cppgc/internal/persistent-node.h"
#include "include/cppgc/liveness-broker.h"
#include "include/cppgc/macros.h"
#include "src/base/page-allocator.h"
#include "src/heap/cppgc/garbage-collector.h"
#include "src/heap/cppgc/gc-invoker.h"
#include "src/heap/cppgc/heap-growing.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/marker.h"
#include "src/heap/cppgc/object-allocator.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/prefinalizer-handler.h"
#include "src/heap/cppgc/raw-heap.h"
#include "src/heap/cppgc/sweeper.h"
#include "src/heap/cppgc/virtual-memory.h"

#if defined(CPPGC_CAGED_HEAP)
#include "src/base/bounded-page-allocator.h"
#endif

namespace cppgc {
namespace internal {

namespace testing {
class TestWithHeap;
}

class StatsCollector;
class Stack;

class V8_EXPORT_PRIVATE LivenessBrokerFactory {
 public:
  static LivenessBroker Create();
};

class V8_EXPORT_PRIVATE Heap final : public cppgc::Heap,
                                     public GarbageCollector {
 public:
  // NoGCScope allows going over limits and avoids triggering garbage
  // collection triggered through allocations or even explicitly.
  class V8_EXPORT_PRIVATE NoGCScope final {
    CPPGC_STACK_ALLOCATED();

   public:
    explicit NoGCScope(Heap* heap);
    ~NoGCScope();

    NoGCScope(const NoGCScope&) = delete;
    NoGCScope& operator=(const NoGCScope&) = delete;

   private:
    Heap* const heap_;
  };

  // NoAllocationScope is used in debug mode to catch unwanted allocations. E.g.
  // allocations during GC.
  class V8_EXPORT_PRIVATE NoAllocationScope final {
    CPPGC_STACK_ALLOCATED();

   public:
    explicit NoAllocationScope(Heap* heap);
    ~NoAllocationScope();

    NoAllocationScope(const NoAllocationScope&) = delete;
    NoAllocationScope& operator=(const NoAllocationScope&) = delete;

   private:
    Heap* const heap_;
  };

  static Heap* From(cppgc::Heap* heap) { return static_cast<Heap*>(heap); }

  Heap(std::shared_ptr<cppgc::Platform> platform,
       cppgc::Heap::HeapOptions options);
  ~Heap() final;

  inline void* Allocate(size_t size, GCInfoIndex index);
  inline void* Allocate(size_t size, GCInfoIndex index,
                        CustomSpaceIndex space_index);

  void CollectGarbage(Config config) final;

  PreFinalizerHandler* prefinalizer_handler() {
    return prefinalizer_handler_.get();
  }

  PersistentRegion& GetStrongPersistentRegion() {
    return strong_persistent_region_;
  }
  const PersistentRegion& GetStrongPersistentRegion() const {
    return strong_persistent_region_;
  }
  PersistentRegion& GetWeakPersistentRegion() {
    return weak_persistent_region_;
  }
  const PersistentRegion& GetWeakPersistentRegion() const {
    return weak_persistent_region_;
  }

  RawHeap& raw_heap() { return raw_heap_; }
  const RawHeap& raw_heap() const { return raw_heap_; }

  StatsCollector* stats_collector() { return stats_collector_.get(); }
  const StatsCollector* stats_collector() const {
    return stats_collector_.get();
  }

  Stack* stack() { return stack_.get(); }

  PageBackend* page_backend() { return page_backend_.get(); }
  const PageBackend* page_backend() const { return page_backend_.get(); }

  cppgc::Platform* platform() { return platform_.get(); }
  const cppgc::Platform* platform() const { return platform_.get(); }

  Sweeper& sweeper() { return sweeper_; }

  size_t epoch() const final { return epoch_; }

  size_t ObjectPayloadSize() const;

 private:
  bool in_no_gc_scope() const { return no_gc_scope_ > 0; }
  bool is_allocation_allowed() const { return no_allocation_scope_ == 0; }

  RawHeap raw_heap_;

  std::shared_ptr<cppgc::Platform> platform_;
#if defined(CPPGC_CAGED_HEAP)
  // The order is important: page_backend_ must be destroyed before
  // reserved_area_ is freed.
  VirtualMemory reserved_area_;
  std::unique_ptr<v8::base::BoundedPageAllocator> bounded_allocator_;
#endif
  std::unique_ptr<PageBackend> page_backend_;
  std::unique_ptr<StatsCollector> stats_collector_;
  ObjectAllocator object_allocator_;
  Sweeper sweeper_;
  GCInvoker gc_invoker_;
  HeapGrowing growing_;

  std::unique_ptr<Stack> stack_;
  std::unique_ptr<PreFinalizerHandler> prefinalizer_handler_;
  std::unique_ptr<Marker> marker_;

  PersistentRegion strong_persistent_region_;
  PersistentRegion weak_persistent_region_;

  size_t epoch_ = 0;

  size_t no_gc_scope_ = 0;
  size_t no_allocation_scope_ = 0;

  friend class WriteBarrier;
  friend class testing::TestWithHeap;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_HEAP_H_
