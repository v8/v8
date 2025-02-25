// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_INLINING_H_
#define V8_MAGLEV_MAGLEV_INLINING_H_

#include "src/base/logging.h"
#include "src/compiler/js-heap-broker.h"
#include "src/execution/local-isolate.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-deopt-frame-visitor.h"
#include "src/maglev/maglev-graph-builder.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-ir.h"

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
      if (result) {
        if (auto alloc = result->TryCast<InlinedAllocation>()) {
          // TODO(victorgomes): Support eliding VOs.
          alloc->ForceEscaping();
#ifdef DEBUG
          alloc->set_is_returned_value_from_inline_call();
#endif  // DEBUG
        }
        GraphProcessor<UpdateInputsProcessor> substitute_use(
            details->generic_call_node, result);
        substitute_use.ProcessGraph(graph_);
      }
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
      // Restore the rest of the graph.
      // TODO(victorgomes): Some of these basic blocks might be unreachable now.
      // Remove them in a different pass.
      for (auto bb : saved_bb) {
        graph_->Add(bb);
      }
      return nullptr;
    }

    DCHECK(result.IsDoneWithValue());
    ValueNode* returned_value =
        EnsureTagged(inner_graph_builder, result.value());

    // Resume execution using the final block of the inner builder.
    BasicBlock* final_block = inner_graph_builder.current_block();
    DCHECK_NOT_NULL(final_block);

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

    // Update the predecessor of the successors of the {final_block}, that were
    // previously pointing to {call_block}.
    final_block->ForEachSuccessor(
        [call_block, final_block](BasicBlock* successor) {
          UpdatePredecessorsOf(successor, call_block, final_block);
        });

    // Restore the rest of the graph.
    for (auto bb : saved_bb) {
      graph_->Add(bb);
    }

    return returned_value;
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

  template <class Node, typename... Args>
  ValueNode* AddNodeAtBlockEnd(MaglevGraphBuilder& builder,
                               std::initializer_list<ValueNode*> inputs,
                               Args&&... args) {
    ValueNode* node = NodeBase::New<Node>(builder.zone(), inputs,
                                          std::forward<Args>(args)...);
    DCHECK(!node->properties().can_eager_deopt());
    DCHECK(!node->properties().can_lazy_deopt());
    DCHECK_NOT_NULL(builder.current_block());
    builder.current_block()->nodes().Add(node);
    RegisterNode(builder, node);
    return node;
  }

  void RegisterNode(MaglevGraphBuilder& builder, Node* node) {
    if (builder.has_graph_labeller()) {
      builder.graph_labeller()->RegisterNode(node);
    }
  }

  ValueNode* EnsureTagged(MaglevGraphBuilder& builder, ValueNode* node) {
    // TODO(victorgomes): Use KNA to create better conversion nodes?
    switch (node->value_representation()) {
      case ValueRepresentation::kInt32:
        return AddNodeAtBlockEnd<Int32ToNumber>(builder, {node});
      case ValueRepresentation::kUint32:
        return AddNodeAtBlockEnd<Uint32ToNumber>(builder, {node});
      case ValueRepresentation::kFloat64:
        return AddNodeAtBlockEnd<Float64ToTagged>(
            builder, {node}, Float64ToTagged::ConversionMode::kForceHeapNumber);
      case ValueRepresentation::kHoleyFloat64:
        return AddNodeAtBlockEnd<HoleyFloat64ToTagged>(
            builder, {node},
            HoleyFloat64ToTagged::ConversionMode::kForceHeapNumber);
      case ValueRepresentation::kIntPtr:
        return AddNodeAtBlockEnd<IntPtrToNumber>(builder, {node});
      case ValueRepresentation::kTagged:
        return node;
    }
  }

  static void UpdatePredecessorsOf(BasicBlock* block, BasicBlock* prev_pred,
                                   BasicBlock* new_pred) {
    if (!block->has_state()) {
      DCHECK_EQ(block->predecessor(), prev_pred);
      block->set_predecessor(new_pred);
      return;
    }
    for (int i = 0; i < block->predecessor_count(); i++) {
      if (block->predecessor_at(i) == prev_pred) {
        block->state()->set_predecessor_at(i, new_pred);
        break;
      }
    }
  }
};

}  // namespace v8::internal::maglev

#endif  // V8_MAGLEV_MAGLEV_INLINING_H_
