// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_INLINING_TREE_H_
#define V8_WASM_INLINING_TREE_H_

#include <cstdint>
#include <queue>
#include <vector>

#include "src/wasm/compilation-environment.h"
#include "src/wasm/wasm-module.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::wasm {

// Represents a tree of inlining decisions.
// A node in the tree represents a function frame, and `function_calls_`
// represent all function calls in this frame. If an element of
// `function_calls_` has its `is_inlined_` field set, it should be inlined into
// the caller. Note that since each element corresponds to a single call, we can
// only represent one speculative call per call_ref.
class InliningTree : public ZoneObject {
 public:
  InliningTree(Zone* zone, const WasmModule* module, uint32_t function_index,
               int call_count, int wire_byte_size)
      : zone_(zone),
        module_(module),
        function_index_(function_index),
        call_count_(call_count),
        wire_byte_size_(wire_byte_size) {}

  int64_t score() const {
    // Note that the zero-point is arbitrary. Functions with negative score
    // can still get inlined.
    constexpr int count_factor = 2;
    constexpr int size_factor = 3;
    return int64_t{call_count_} * count_factor -
           int64_t{wire_byte_size_} * size_factor;
  }

  static constexpr int kMaxInlinedCount = 60;

  // Recursively expand the tree by expanding this node and children nodes etc.
  // Nodes are prioritized by their `score`. Expansion continues until
  // `kMaxInlinedCount` nodes are expanded or `budget` (in wire-bytes size) is
  // depleted.
  void FullyExpand(const size_t initial_graph_size);
  ZoneVector<InliningTree*>* function_calls() { return function_calls_; }
  bool feedback_found() { return function_calls_ != nullptr; }
  bool is_inlined() { return is_inlined_; }
  uint32_t function_index() { return function_index_; }

 private:
  // Mark this function call as inline and initialize `function_calls_` based
  // on the `module_->type_feedback`.
  void Inline();
  bool SmallEnoughToInline(size_t initial_graph_size,
                           size_t inlined_wire_byte_count);

  // TODO(14108): Do not store these in every tree node.
  Zone* zone_;
  const WasmModule* module_;

  uint32_t function_index_;
  int call_count_;
  int wire_byte_size_;
  bool is_inlined_ = false;

  // TODO(14108): These vectors never grow; use pointers instead.
  ZoneVector<InliningTree*>* function_calls_ = nullptr;
};

void InliningTree::Inline() {
  is_inlined_ = true;
  auto feedback =
      module_->type_feedback.feedback_for_function.find(function_index_);
  if (feedback != module_->type_feedback.feedback_for_function.end() &&
      feedback->second.feedback_vector.size() ==
          feedback->second.call_targets.size()) {
    std::vector<CallSiteFeedback>& type_feedback =
        feedback->second.feedback_vector;
    function_calls_ =
        zone_->New<ZoneVector<InliningTree*>>(type_feedback.size(), zone_);
    for (size_t i = 0; i < type_feedback.size(); i++) {
      if (type_feedback[i].num_cases() > 0) {
        uint32_t callee_index = type_feedback[i].function_index(0);
        (*function_calls_)[i] = zone_->New<InliningTree>(
            zone_, module_, callee_index, type_feedback[i].call_count(0),
            module_->functions[callee_index].code.length());
      }
    }
  }
}

struct TreeNodeOrdering {
  bool operator()(InliningTree* t1, InliningTree* t2) {
    return t1->score() < t2->score();
  }
};

void InliningTree::FullyExpand(const size_t initial_graph_size) {
  size_t inlined_wire_byte_count = 0;
  std::priority_queue<InliningTree*, std::vector<InliningTree*>,
                      TreeNodeOrdering>
      queue;
  queue.push(this);
  int inlined_count = 0;
  base::SharedMutexGuard<base::kShared> mutex_guard(
      &module_->type_feedback.mutex);
  while (!queue.empty() && inlined_count < kMaxInlinedCount) {
    InliningTree* top = queue.top();
    queue.pop();
    if (!top->SmallEnoughToInline(initial_graph_size,
                                  inlined_wire_byte_count)) {
      continue;
    }
    top->Inline();
    inlined_count++;
    inlined_wire_byte_count += top->wire_byte_size_;
    if (top->feedback_found()) {
      for (InliningTree* call : *top->function_calls_) {
        if (call != nullptr) queue.push(call);
      }
    }
  }
}

bool InliningTree::SmallEnoughToInline(size_t initial_graph_size,
                                       size_t inlined_wire_byte_count) {
  if (wire_byte_size_ > static_cast<int>(v8_flags.wasm_inlining_max_size)) {
    return false;
  }
  // For tiny functions, let's be a bit more generous.
  if (wire_byte_size_ < 12) {
    if (inlined_wire_byte_count > 100) {
      inlined_wire_byte_count -= 100;
    } else {
      inlined_wire_byte_count = 0;
    }
  }
  size_t budget =
      std::max<size_t>(v8_flags.wasm_inlining_min_budget,
                       v8_flags.wasm_inlining_factor * initial_graph_size);
  size_t full_budget =
      std::max<size_t>(v8_flags.wasm_inlining_budget, initial_graph_size * 1.1);
  return inlined_wire_byte_count + static_cast<size_t>(wire_byte_size_) <
         std::min<size_t>(budget, full_budget);
}

}  // namespace v8::internal::wasm

#endif  // V8_WASM_INLINING_TREE_H_
