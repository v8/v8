// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_INLINING_H_
#define V8_MAGLEV_MAGLEV_INLINING_H_

#include "src/base/logging.h"
#include "src/compiler/js-heap-broker.h"
#include "src/execution/local-isolate.h"
#include "src/maglev/maglev-basic-block.h"
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
  void PostProcessBasicBlock(BasicBlock* block) {}
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
  MaglevInliner(MaglevCompilationInfo* compilation_info, Graph* graph)
      : compilation_info_(compilation_info), graph_(graph) {}

  void Run(bool is_tracing_maglev_graphs_enabled) {
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
      // If --trace-maglev-inlining-verbose, we print the graph after each
      // inlining step/call.
      if (is_tracing_maglev_graphs_enabled && v8_flags.print_maglev_graphs &&
          v8_flags.trace_maglev_inlining_verbose) {
        std::cout << "\nAfter inlining "
                  << details->callee->shared_function_info() << std::endl;
        PrintGraph(std::cout, compilation_info_, graph_);
      }
    }
    // Otherwise we print just once at the end.
    if (is_tracing_maglev_graphs_enabled && v8_flags.print_maglev_graphs &&
        !v8_flags.trace_maglev_inlining_verbose) {
      std::cout << "\nAfter inlining" << std::endl;
      PrintGraph(std::cout, compilation_info_, graph_);
    }
  }

 private:
  MaglevCompilationInfo* compilation_info_;
  Graph* graph_;

  compiler::JSHeapBroker* broker() const { return compilation_info_->broker(); }
  Zone* zone() const { return compilation_info_->zone(); }

  ValueNode* BuildInlineFunction(MaglevCallerDetails* details) {
    if (v8_flags.trace_maglev_inlining) {
      std::cout << "  non-eager inlining "
                << details->callee->shared_function_info() << std::endl;
    }

    compiler::BytecodeArrayRef bytecode =
        details->callee->shared_function_info().GetBytecodeArray(broker());
    graph_->add_inlined_bytecode_size(bytecode.length());

    // Create a new graph builder for the inlined function.
    LocalIsolate* local_isolate = broker()->local_isolate_or_isolate();
    MaglevGraphBuilder inner_graph_builder(local_isolate, details->callee,
                                           graph_, details);

    CallKnownJSFunction* generic_node = details->generic_call_node;
    BasicBlock* call_block = generic_node->owner();

    // We truncate the graph to build the function in-place, preserving the
    // invariant that all jumps move forward (except JumpLoop).
    std::vector<BasicBlock*> saved_bb = TruncateGraphAt(call_block);

    // Truncate the basic block and remove the generic call node.
    ControlNode* control_node = call_block->reset_control_node();
    ZoneVector<Node*> rem_nodes_in_call_block =
        call_block->Split(generic_node, zone());

    // Set the inner graph builder to build in the truncated call block.
    inner_graph_builder.set_current_block(call_block);

    ReduceResult result = inner_graph_builder.BuildInlineFunction(
        generic_node->context().node(), generic_node->closure().node(),
        generic_node->new_target().node());

    if (result.IsDoneWithAbort()) {
      // Restore the rest of the graph.
      for (auto bb : saved_bb) {
        graph_->Add(bb);
      }
      RemovePredecessorFollowing(control_node, call_block);
      // TODO(victorgomes): We probably don't need to iterate all the graph to
      // remove unreachable blocks, but only the successors of control_node in
      // saved_bbs.
      RemoveUnreachableBlocks();
      return nullptr;
    }

    DCHECK(result.IsDoneWithValue());
    ValueNode* returned_value =
        EnsureTagged(inner_graph_builder, result.value());

    // Resume execution using the final block of the inner builder.

    // Add remaining nodes to the final block and use the control flow of the
    // old call block.
    BasicBlock* final_block = inner_graph_builder.FinishInlinedBlockForCaller(
        control_node, rem_nodes_in_call_block);
    DCHECK_NOT_NULL(final_block);

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
    ValueNode* node =
        NodeBase::New<Node>(zone(), inputs, std::forward<Args>(args)...);
    DCHECK(!node->properties().can_eager_deopt());
    DCHECK(!node->properties().can_lazy_deopt());
    builder.node_buffer().push_back(node);
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

  void RemovePredecessorFollowing(ControlNode* control,
                                  BasicBlock* call_block) {
    BasicBlock::ForEachSuccessorFollowing(control, [&](BasicBlock* succ) {
      if (!succ->has_state()) return;
      if (succ->is_loop() && succ->backedge_predecessor() == call_block) {
        succ->state()->TurnLoopIntoRegularBlock();
        return;
      }
      for (int i = succ->predecessor_count() - 1; i >= 0; i--) {
        if (succ->predecessor_at(i) == call_block) {
          succ->state()->RemovePredecessorAt(i);
        }
      }
    });
  }

  void RemoveUnreachableBlocks() {
    absl::flat_hash_set<BasicBlock*> reachable_blocks;
    absl::flat_hash_set<BasicBlock*> loop_headers_unreachable_by_backegde;
    std::vector<BasicBlock*> worklist;

    DCHECK(!graph_->blocks().empty());
    BasicBlock* initial_bb = graph_->blocks().front();
    worklist.push_back(initial_bb);
    reachable_blocks.insert(initial_bb);
    DCHECK(!initial_bb->is_loop());

    // Add all exception handler blocks to the worklist.
    // TODO(victorgomes): A catch block could still be unreachable, if no
    // bbs in its try-block are unreachables, or its nodes cannot throw.
    for (BasicBlock* bb : graph_->blocks()) {
      if (bb->is_exception_handler_block()) {
        worklist.push_back(bb);
        reachable_blocks.insert(bb);
      }
    }

    while (!worklist.empty()) {
      BasicBlock* current = worklist.back();
      worklist.pop_back();
      if (current->is_loop()) {
        loop_headers_unreachable_by_backegde.insert(current);
      }
      current->ForEachSuccessor([&](BasicBlock* succ) {
        if (reachable_blocks.contains(succ)) {
          // We have already added this block to the worklist, check only if
          // that's a reachable loop header.
          if (succ->is_loop()) {
            // This must be the loop back edge.
            DCHECK(succ->is_loop());
            DCHECK_EQ(succ->backedge_predecessor(), current);
            DCHECK(loop_headers_unreachable_by_backegde.contains(succ));
            loop_headers_unreachable_by_backegde.erase(succ);
          }
        } else {
          reachable_blocks.insert(succ);
          worklist.push_back(succ);
        }
      });
    }

    for (BasicBlock* bb : loop_headers_unreachable_by_backegde) {
      DCHECK(bb->has_state());
      bb->state()->TurnLoopIntoRegularBlock();
    }

    ZoneVector<BasicBlock*> new_blocks(zone());
    for (BasicBlock* bb : graph_->blocks()) {
      if (reachable_blocks.contains(bb)) {
        new_blocks.push_back(bb);
        // Remove unreachable predecessors.
        // If block doesn't have a merge state, it has only one predecessor, so
        // it must be a reachable one.
        if (!bb->has_state()) continue;
        for (int i = bb->predecessor_count() - 1; i >= 0; i--) {
          if (!reachable_blocks.contains(bb->predecessor_at(i))) {
            bb->state()->RemovePredecessorAt(i);
          }
        }
      }
    }
    graph_->set_blocks(new_blocks);
  }
};

}  // namespace v8::internal::maglev

#endif  // V8_MAGLEV_MAGLEV_INLINING_H_
