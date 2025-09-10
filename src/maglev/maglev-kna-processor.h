// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_KNA_PROCESSOR_H_
#define V8_MAGLEV_MAGLEV_KNA_PROCESSOR_H_

#include <type_traits>

#include "src/base/logging.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

template <typename T>
concept IsNodeT = std::is_base_of_v<Node, T>;

// Recomputes the Known Node Aspects (KNA) for the entire graph. KNA tracks
// information about nodes that can be used for optimizations, such as
// eliminating redundant checks or loads.
//
// It performs a forward data-flow analysis over the graph. Starting with
// empty KNA, it iterates through nodes in each basic block. When it
// encounters a node with potential side effects (e.g., writing to an array or
// field), it updates the KNA to reflect that some previously known information
// may no longer be valid. This updated information is then merged into
// successor basic blocks.
class RecomputeKnownNodeAspectsProcessor {
 public:
  explicit RecomputeKnownNodeAspectsProcessor(Graph* graph)
      : graph_(graph), known_node_aspects_(nullptr) {}

  void PreProcessGraph(Graph* graph) {
    known_node_aspects_ = zone()->New<KnownNodeAspects>(zone());
    for (BasicBlock* block : graph->blocks()) {
      if (block->has_state()) {
        block->state()->ClearKnownNodeAspects();
      }
      if (block->is_exception_handler_block()) {
        // TODO(victorgomes): Figure it out the first block to throw to this
        // node and set KNA.
        block->state()->MergeNodeAspects(zone(), *known_node_aspects_);
      }
    }
  }
  void PostProcessGraph(Graph* graph) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    if (block->has_state()) {
      known_node_aspects_ = block->state()->TakeKnownNodeAspects();
    }
    DCHECK_IMPLIES(known_node_aspects_ == nullptr,
                   block->is_edge_split_block());
    return BlockProcessResult::kContinue;
  }
  void PostProcessBasicBlock(BasicBlock* block) {}
  void PostPhiProcessing() {}

  template <IsNodeT NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    MarkPossibleSideEffect(node);
    return ProcessResult::kContinue;
  }

  ProcessResult Process(Switch* node, const ProcessingState& state) {
    for (int i = 0; i < node->size(); i++) {
      Merge(node->targets()[i].block_ptr());
    }
    if (node->has_fallthrough()) {
      Merge(node->fallthrough());
    }
    return ProcessResult::kContinue;
  }

  ProcessResult Process(BranchControlNode* node, const ProcessingState& state) {
    Merge(node->if_true());
    Merge(node->if_false());
    return ProcessResult::kContinue;
  }

  ProcessResult Process(Jump* node, const ProcessingState& state) {
    if (!node->owner()->is_edge_split_block()) {
      Merge(node->target());
      return ProcessResult::kContinue;
    }
    return ProcessResult::kContinue;
  }

  ProcessResult Process(CheckpointedJump* node, const ProcessingState& state) {
    Merge(node->target());
    return ProcessResult::kContinue;
  }

  ProcessResult Process(JumpLoop* node, const ProcessingState& state) {
#ifdef DEBUG
    known_node_aspects_ = nullptr;
#endif  // DEBUG
    return ProcessResult::kContinue;
  }

  ProcessResult Process(TerminalControlNode* node,
                        const ProcessingState& state) {
#ifdef DEBUG
    known_node_aspects_ = nullptr;
#endif  // DEBUG
    return ProcessResult::kContinue;
  }

  ProcessResult Process(ControlNode* node, const ProcessingState& state) {
    UNREACHABLE();
  }

  KnownNodeAspects& known_node_aspects() {
    DCHECK_NOT_NULL(known_node_aspects_);
    return *known_node_aspects_;
  }

 private:
  Graph* graph_;
  KnownNodeAspects* known_node_aspects_;

  Zone* zone() { return graph_->zone(); }

  void Merge(BasicBlock* block) {
    while (block->is_edge_split_block()) {
      block = block->control_node()->Cast<Jump>()->target();
    }
    // If we don't have state, this must be a fallthrough basic block.
    if (!block->has_state()) return;
    block->state()->MergeNodeAspects(zone(), *known_node_aspects_);
  }

  template <typename NodeT>
  void MarkPossibleSideEffect(NodeT* node) {
    // Don't do anything for nodes without side effects.
    if constexpr (!NodeT::kProperties.can_write()) return;

    if constexpr (IsElementsArrayWrite(Node::opcode_of<NodeT>)) {
      node->ClearElementsProperties(graph_->is_tracing_enabled(),
                                    known_node_aspects());
    } else if constexpr (!IsSimpleFieldStore(Node::opcode_of<NodeT>) &&
                         !IsTypedArrayStore(Node::opcode_of<NodeT>)) {
      // Don't change known node aspects for simple field stores. The only
      // relevant side effect on these is writes to objects which invalidate
      // loaded properties and context slots, and we invalidate these already as
      // part of emitting the store.
      node->ClearUnstableNodeAspects(graph_->is_tracing_enabled(),
                                     known_node_aspects());
    }
  }
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_KNA_PROCESSOR_H_
