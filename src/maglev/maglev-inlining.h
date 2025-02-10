// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_INLINING_H_
#define V8_MAGLEV_MAGLEV_INLINING_H_

#include "src/compiler/js-heap-broker.h"
#include "src/execution/local-isolate.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-deopt-frame-visitor.h"
#include "src/maglev/maglev-graph-builder.h"
#include "src/maglev/maglev-graph-processor.h"

namespace v8::internal::maglev {

class UpdateInputsProcessor {
 public:
  explicit UpdateInputsProcessor(ValueNode* from, ValueNode* to)
      : from_(from), to_(to) {}

  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    for (Input& input : *node) {
      if (input.node() == from_) {
        input.set_node(to_);
        to_->add_use();
      }
    }
    if constexpr (NodeT::kProperties.can_eager_deopt()) {
      detail::DeepForEachInputForEager<detail::DeoptFrameVisitMode::kOther>(
          node->eager_deopt_info(),
          [&](ValueNode*& node, InputLocation* input) {
            if (node == from_) {
              node = to_;
              to_->add_use();
            }
          });
    }
    if constexpr (NodeT::kProperties.can_lazy_deopt()) {
      detail::DeepForEachInputForLazy<detail::DeoptFrameVisitMode::kOther>(
          node->lazy_deopt_info(), [&](ValueNode*& node, InputLocation* input) {
            if (node == from_) {
              node = to_;
              to_->add_use();
            }
          });
    }
    return ProcessResult::kContinue;
  }

 private:
  ValueNode* from_;
  ValueNode* to_;
};

class MaglevInliner {
 public:
  MaglevInliner(LocalIsolate* local_isolate, Graph* graph)
      : local_isolate_(local_isolate), graph_(graph) {}

  void Run() {
    // TODO(victorgomes): Add some heuristics to choose which function to
    // inline.
    while (!graph_->inlineable_calls().empty()) {
      if (graph_->total_inlined_bytecode_size() >
          v8_flags.max_maglev_inlined_bytecode_size_cumulative) {
        // No more inlining.
        break;
      }
      MaglevCallerDetails* details = graph_->inlineable_calls().back();
      graph_->inlineable_calls().pop_back();
      ValueNode* result = BuildInlineFunction(details);
      GraphProcessor<UpdateInputsProcessor> substitute_use(
          details->generic_call_node, result);
      substitute_use.ProcessGraph(graph_);
    }
  }

 private:
  LocalIsolate* local_isolate_;
  Graph* graph_;

  ValueNode* BuildInlineFunction(MaglevCallerDetails* details) {
    compiler::JSHeapBroker* broker = details->callee->broker();
    compiler::BytecodeArrayRef bytecode =
        details->callee->shared_function_info().GetBytecodeArray(broker);
    graph_->add_inlined_bytecode_size(bytecode.length());

    // Create a new graph builder for the inlined function.
    MaglevGraphBuilder inner_graph_builder(local_isolate_, details->callee,
                                           graph_, details);

    CallKnownJSFunction* generic_node = details->generic_call_node;
    BasicBlock* call_block = generic_node->owner();

    // We truncate the graph to build the function in-place, preserving the
    // invariant that all jumps move forward (except JumpLoop).
    std::vector<BasicBlock*> saved_bb = TruncateGraphAt(call_block);

    // Truncate the basic block and remove the generic call node.
    Node::List rem_nodes_in_call_block;
    ControlNode* control_node = call_block->control_node();
    call_block->TruncateAt(generic_node, &rem_nodes_in_call_block);
    DCHECK_EQ(*rem_nodes_in_call_block.begin(), generic_node);
    rem_nodes_in_call_block.DropHead();

    // Set the inner graph builder to build in the truncated call block.
    inner_graph_builder.set_current_block(call_block);

    ReduceResult result = inner_graph_builder.BuildInlineFunction(
        generic_node->context().node(), generic_node->closure().node(),
        generic_node->new_target().node());

    if (result.IsDoneWithAbort()) {
      // TODO(victorgomes): Not yet supported.
      UNREACHABLE();
    }

    DCHECK(result.IsDoneWithValue());

    // Resume execution using the final block of the inner builder.
    BasicBlock* final_block = inner_graph_builder.current_block();

    // Add remaining nodes to the final block and use the control flow of the
    // old call block.
    for (Node* n : rem_nodes_in_call_block) {
      n->set_owner(final_block);
    }
    final_block->nodes().Append(std::move(rem_nodes_in_call_block));
    CHECK_NULL(final_block->control_node());
    control_node->set_owner(final_block);
    final_block->set_control_node(control_node);

    // Add the final block to the graph.
    graph_->Add(final_block);
    if (details->callee->has_graph_labeller()) {
      // Only need to register the block, since the control node should already
      // be registered.
      details->callee->graph_labeller()->RegisterBasicBlock(final_block);
    }

    // TODO(victorgomes): Investigate whether we should update the predecessor_
    // of the blocks that final_block jumps to when control_node != JumpLoop.
    // Update the control node in case of loop.
    if (control_node->Is<JumpLoop>()) {
      // We need to update the predecessor of the LoopHeader.
      BasicBlock* loop_header = control_node->Cast<JumpLoop>()->target();
      CHECK(loop_header->is_loop());
      auto state = loop_header->state();
      CHECK_EQ(state->predecessor_count(), 2);
      state->set_predecessor_at(1, final_block);
    }

    // Restore the rest of the graph.
    for (auto bb : saved_bb) {
      graph_->Add(bb);
    }

    // TODO(victorgomes): This (currently) need to be tagged.
    return result.value();
  }

  // Truncates the graph at the given basic block `block`.  All blocks
  // following `block` (exclusive) are removed from the graph and returned.
  // `block` itself is removed from the graph and not returned.
  std::vector<BasicBlock*> TruncateGraphAt(BasicBlock* block) {
    // TODO(victorgomes): Consider using a linked list of basic blocks in Maglev
    // instead of a vector.
    auto it =
        std::find(graph_->blocks().begin(), graph_->blocks().end(), block);
    CHECK_NE(it, graph_->blocks().end());
    size_t index = std::distance(graph_->blocks().begin(), it);
    std::vector<BasicBlock*> saved_bb(graph_->blocks().begin() + index + 1,
                                      graph_->blocks().end());
    graph_->blocks().resize(index);
    return saved_bb;
  }
};

}  // namespace v8::internal::maglev

#endif  // V8_MAGLEV_MAGLEV_INLINING_H_
