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
#include "src/base/page-allocator.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/object-allocator.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/prefinalizer-handler.h"
#include "src/heap/cppgc/raw-heap.h"

namespace cppgc {
namespace internal {

class Stack;

class V8_EXPORT_PRIVATE LivenessBrokerFactory {
 public:
  static LivenessBroker Create();
};

class V8_EXPORT_PRIVATE Heap final : public cppgc::Heap {
 public:
  class V8_EXPORT_PRIVATE NoGCScope final {
   public:
    explicit NoGCScope(Heap* heap);
    ~NoGCScope();

   private:
    Heap* heap_;
  };

  // The NoAllocationScope class is used in debug mode to catch unwanted
  // allocations. E.g. allocations during GC.
  class V8_EXPORT_PRIVATE NoAllocationScope final {
    CPPGC_STACK_ALLOCATED();

   public:
    explicit NoAllocationScope(Heap* heap);
    ~NoAllocationScope();

   private:
    Heap* const heap_;

    DISALLOW_COPY_AND_ASSIGN(NoAllocationScope);
  };

  struct GCConfig {
    enum class StackState : uint8_t {
      kEmpty,
      kNonEmpty,
    };

    static GCConfig Default() { return {StackState::kNonEmpty}; }

    StackState stack_state = StackState::kNonEmpty;
  };

  static Heap* From(cppgc::Heap* heap) { return static_cast<Heap*>(heap); }

  Heap();
  ~Heap() final;

  inline void* Allocate(size_t size, GCInfoIndex index);

  void CollectGarbage(GCConfig config = GCConfig::Default());

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

  PageBackend* page_backend() { return page_backend_.get(); }
  const PageBackend* page_backend() const { return page_backend_.get(); }

  size_t ObjectPayloadSize() const;

 private:
  bool in_no_gc_scope() const { return no_gc_scope_ > 0; }
  bool is_allocation_allowed() const { return no_allocation_scope_ == 0; }

  RawHeap raw_heap_;

  v8::base::PageAllocator system_allocator_;
  std::unique_ptr<PageBackend> page_backend_;
  ObjectAllocator object_allocator_;

  std::unique_ptr<Stack> stack_;
  std::unique_ptr<PreFinalizerHandler> prefinalizer_handler_;
  std::vector<HeapObjectHeader*> objects_;

  PersistentRegion strong_persistent_region_;
  PersistentRegion weak_persistent_region_;

  size_t no_gc_scope_ = 0;
  size_t no_allocation_scope_ = 0;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_HEAP_H_
