// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap.h"

#include <memory>

#include "src/base/platform/platform.h"
#include "src/heap/cppgc/heap-object-header-inl.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap-visitor.h"
#include "src/heap/cppgc/stack.h"

namespace cppgc {

std::unique_ptr<Heap> Heap::Create() {
  return std::make_unique<internal::Heap>();
}

namespace internal {

namespace {

constexpr bool NeedsConservativeStackScan(Heap::GCConfig config) {
  return config.stack_state == Heap::GCConfig::StackState::kNonEmpty;
}

class ObjectSizeCounter : public HeapVisitor<ObjectSizeCounter> {
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

}  // namespace

// static
cppgc::LivenessBroker LivenessBrokerFactory::Create() {
  return cppgc::LivenessBroker();
}

// TODO(chromium:1056170): Replace with fast stack scanning once
// object are allocated actual arenas/spaces.
class StackMarker final : public StackVisitor {
 public:
  explicit StackMarker(const std::vector<HeapObjectHeader*>& objects)
      : objects_(objects) {}

  void VisitPointer(const void* address) final {
    for (auto* header : objects_) {
      if (address >= header->Payload() &&
          address < (header + header->GetSize())) {
        header->TryMarkAtomic();
      }
    }
  }

 private:
  const std::vector<HeapObjectHeader*>& objects_;
};

Heap::Heap()
    : raw_heap_(this),
      page_backend_(std::make_unique<PageBackend>(&system_allocator_)),
      object_allocator_(&raw_heap_),
      stack_(std::make_unique<Stack>(v8::base::Stack::GetStackStart())),
      prefinalizer_handler_(std::make_unique<PreFinalizerHandler>()) {}

Heap::~Heap() {
  for (HeapObjectHeader* header : objects_) {
    header->Finalize();
  }
}

void Heap::CollectGarbage(GCConfig config) {
  // No GC calls when in NoGCScope.
  CHECK(!in_no_gc_scope());

  // TODO(chromium:1056170): Replace with proper mark-sweep algorithm.
  // "Marking".
  if (NeedsConservativeStackScan(config)) {
    StackMarker marker(objects_);
    stack_->IteratePointers(&marker);
  }
  // "Sweeping and finalization".
  {
    // Pre finalizers are forbidden from allocating objects
    NoAllocationScope no_allocation_scope_(this);
    prefinalizer_handler_->InvokePreFinalizers();
  }
  for (auto it = objects_.begin(); it != objects_.end();) {
    HeapObjectHeader* header = *it;
    if (header->IsMarked()) {
      header->Unmark();
      ++it;
    } else {
      header->Finalize();
      it = objects_.erase(it);
    }
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
