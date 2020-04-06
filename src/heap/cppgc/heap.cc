// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap.h"

#include <memory>

#include "src/base/platform/platform.h"
#include "src/heap/cppgc/heap-object-header-inl.h"
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

}  // namespace

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
    : stack_(std::make_unique<Stack>(v8::base::Stack::GetStackStart())) {}

void Heap::CollectGarbage(GCConfig config) {
  current_config_ = config;
  // TODO(chromium:1056170): Replace with proper mark-sweep algorithm.
  // "Marking".
  if (NeedsConservativeStackScan(current_config_)) {
    StackMarker marker(objects_);
    stack_->IteratePointers(&marker);
  }
  // "Sweeping and finalization".
  for (HeapObjectHeader* header : objects_) {
    if (header->IsMarked()) {
      header->Unmark();
    } else {
      header->Finalize();
      free(header);
    }
  }
}

}  // namespace internal
}  // namespace cppgc
