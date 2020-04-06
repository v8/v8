// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_HEAP_H_
#define V8_HEAP_CPPGC_HEAP_H_

#include <memory>
#include <vector>

#include "include/cppgc/gc-info.h"
#include "include/cppgc/heap.h"
#include "src/heap/cppgc/heap-object-header.h"

namespace cppgc {
namespace internal {

class Stack;

class V8_EXPORT_PRIVATE Heap final : public cppgc::Heap {
 public:
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
  ~Heap() final = default;

  inline void* Allocate(size_t size, GCInfoIndex index);

  void CollectGarbage(GCConfig config = GCConfig::Default());

 private:
  GCConfig current_config_;

  std::unique_ptr<Stack> stack_;
  std::vector<HeapObjectHeader*> objects_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_HEAP_H_
