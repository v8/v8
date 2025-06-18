// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_INLINING_H_
#define V8_MAGLEV_MAGLEV_INLINING_H_

#include <algorithm>
#include <utility>

#include "src/base/logging.h"
#include "src/compiler/heap-refs.h"
#include "src/compiler/js-heap-broker.h"
#include "src/execution/local-isolate.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-graph-builder.h"
#include "src/maglev/maglev-ir.h"

namespace v8::internal::maglev {

class MaglevInliner {
 public:
  explicit MaglevInliner(Graph* graph) : graph_(graph) {}

  void Run(bool is_tracing_maglev_graphs_enabled) {
    if (graph_->inlineable_calls().empty()) return;

    while (true) {
      if (graph_->total_inlined_bytecode_size() >
          v8_flags.max_maglev_inlined_bytecode_size_cumulative) {
        // No more inlining.
        break;
      }
      MaglevCallSiteInfo* call_site = ChooseNextCallSite();
      if (!call_site) break;

      MaybeReduceResult result = BuildInlineFunction(call_site);
      if (result.IsFail()) continue;
      // If --trace-maglev-inlining-verbose, we print the graph after each
      // inlining step/call.
      if (is_tracing_maglev_graphs_enabled && v8_flags.print_maglev_graphs &&
          v8_flags.trace_maglev_inlining_verbose) {
        std::cout << "\nAfter inlining "
                  << call_site->generic_call_node->shared_function_info()
                  << std::endl;
        PrintGraph(std::cout, graph_);
      }
    }

    // Otherwise we print just once at the end.
    if (is_tracing_maglev_graphs_enabled && v8_flags.print_maglev_graphs &&
        !v8_flags.trace_maglev_inlining_verbose) {
      std::cout << "\nAfter inlining" << std::endl;
      PrintGraph(std::cout, graph_);
    }
  }

 private:
  Graph* graph_;

  compiler::JSHeapBroker* broker() const { return graph_->broker(); }
  Zone* zone() const { return graph_->zone(); }

  MaglevCallSiteInfo* ChooseNextCallSite() {
    auto it =
        v8_flags.maglev_inlining_following_eager_order
            ? std::ranges::find_if(graph_->inlineable_calls(),
                                   [](auto* site) { return site != nullptr; })
            : std::ranges::max_element(
                  graph_->inlineable_calls(),
                  [](const MaglevCallSiteInfo* info1,
                     const MaglevCallSiteInfo* info2) {
                    if (info1 == nullptr || info2 == nullptr) {
                      return info2 != nullptr;
                    }
                    return info1->score < info2->score;
                  });
    if (it == graph_->inlineable_calls().end()) return nullptr;
    MaglevCallSiteInfo* call_site = *it;
    *it = nullptr;  // Erase call site.
    return call_site;
  }

  MaybeReduceResult BuildInlineFunction(MaglevCallSiteInfo* call_site) {
    CallKnownJSFunction* call_node = call_site->generic_call_node;
    BasicBlock* call_block = call_node->owner();
    MaglevCallerDetails* caller_details = &call_site->caller_details;
    DeoptFrame* caller_deopt_frame = caller_details->deopt_frame;
    const MaglevCompilationUnit* caller_unit =
        &caller_deopt_frame->GetCompilationUnit();
    compiler::SharedFunctionInfoRef shared = call_node->shared_function_info();

    if (!call_block || call_block->is_dead()) {
      // The block containing the call is unreachable, and it was previously
      // removed. Do not try to inline the call.
      return ReduceResult::Fail();
    }

    if (v8_flags.trace_maglev_inlining) {
      std::cout << "  non-eager inlining " << shared << std::endl;
    }

    // Check if the catch block might become unreachable, ie, the call is the
    // only instance of a throwable node in this block to the same catch block.
    ExceptionHandlerInfo* call_exception_handler_info =
        call_node->exception_handler_info();
    bool catch_block_might_be_unreachable = false;
    if (call_exception_handler_info->HasExceptionHandler() &&
        !call_exception_handler_info->ShouldLazyDeopt()) {
      BasicBlock* catch_block = call_exception_handler_info->catch_block();
      catch_block_might_be_unreachable = true;
      for (ExceptionHandlerInfo* info : call_block->exception_handlers()) {
        if (info != call_exception_handler_info &&
            info->HasExceptionHandler() && !info->ShouldLazyDeopt() &&
            info->catch_block() == catch_block) {
          catch_block_might_be_unreachable = false;
          break;
        }
      }
    }

    // Remove exception handler info from call block.
    ExceptionHandlerInfo::List rem_handlers_in_call_block;
    call_block->exception_handlers().TruncateAt(&rem_handlers_in_call_block,
                                                call_exception_handler_info);
    rem_handlers_in_call_block.DropHead();

    // Truncate the basic block and remove the generic call node.
    ZoneVector<Node*> rem_nodes_in_call_block =
        call_block->Split(call_node, zone());

    // Create a new compilation unit.
    MaglevCompilationUnit* inner_unit = MaglevCompilationUnit::NewInner(
        zone(), caller_unit, shared, call_site->feedback_cell);

    compiler::BytecodeArrayRef bytecode = shared.GetBytecodeArray(broker());
    graph_->add_inlined_bytecode_size(bytecode.length());

    // Create a new graph builder for the inlined function.
    LocalIsolate* local_isolate = broker()->local_isolate_or_isolate();
    MaglevGraphBuilder inner_graph_builder(local_isolate, inner_unit, graph_,
                                           &call_site->caller_details);

    // Update caller deopt frame with inlined arguments.
    caller_details->deopt_frame =
        inner_graph_builder.AddInlinedArgumentsToDeoptFrame(
            caller_deopt_frame, inner_unit, call_node->closure().node(),
            call_site->caller_details.arguments);

    // We truncate the graph to build the function in-place, preserving the
    // invariant that all jumps move forward (except JumpLoop).
    std::vector<BasicBlock*> saved_bb = TruncateGraphAt(call_block);
    ControlNode* control_node = call_block->reset_control_node();

    // Set the inner graph builder to build in the truncated call block.
    inner_graph_builder.set_current_block(call_block);

    ReduceResult result = inner_graph_builder.BuildInlineFunction(
        caller_deopt_frame->GetSourcePosition(), call_node->context().node(),
        call_node->closure().node(), call_node->new_target().node());

    if (result.IsDoneWithAbort()) {
      // Since the rest of the block is dead, these nodes don't belong
      // to any basic block anymore.
      for (auto node : rem_nodes_in_call_block) {
        node->set_owner(nullptr);
      }
      // Restore the rest of the graph.
      for (auto bb : saved_bb) {
        graph_->Add(bb);
      }
      RemovePredecessorFollowing(control_node, call_block);
      // TODO(victorgomes): We probably don't need to iterate all the graph to
      // remove unreachable blocks, but only the successors of control_node in
      // saved_bbs.
      RemoveUnreachableBlocks();
      return result;
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
    final_block->exception_handlers().Append(
        std::move(rem_handlers_in_call_block));

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

    if (auto alloc = returned_value->TryCast<InlinedAllocation>()) {
      // TODO(victorgomes): Support eliding VOs.
      alloc->ForceEscaping();
#ifdef DEBUG
      alloc->set_is_returned_value_from_inline_call();
#endif  // DEBUG
    }
    call_node->OverwriteWithIdentityTo(returned_value);

    // Remove unreachable catch block if no throwable nodes were added during
    // inlining.
    // TODO(victorgomes): Improve this: track if we didnt indeed add a throwable
    // node.
    if (catch_block_might_be_unreachable) {
      RemoveUnreachableBlocks();
    }

    return ReduceResult::Done();
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

  void RegisterNode(MaglevGraphBuilder& builder, Node* node) {
    if (builder.has_graph_labeller()) {
      builder.graph_labeller()->RegisterNode(node);
    }
  }

  ValueNode* EnsureTagged(MaglevGraphBuilder& builder, ValueNode* node) {
    // TODO(victorgomes): Use KNA to create better conversion nodes?
    switch (node->value_representation()) {
      case ValueRepresentation::kInt32:
        return builder.reducer().AddNewNode<Int32ToNumber>({node});
      case ValueRepresentation::kUint32:
        return builder.reducer().AddNewNode<Uint32ToNumber>({node});
      case ValueRepresentation::kFloat64:
        return builder.reducer().AddNewNode<Float64ToTagged>(
            {node}, Float64ToTagged::ConversionMode::kForceHeapNumber);
      case ValueRepresentation::kHoleyFloat64:
        return builder.reducer().AddNewNode<HoleyFloat64ToTagged>(
            {node}, HoleyFloat64ToTagged::ConversionMode::kForceHeapNumber);
      case ValueRepresentation::kIntPtr:
        return builder.reducer().AddNewNode<IntPtrToNumber>({node});
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
      if (!succ->has_state()) {
        succ->set_predecessor(nullptr);
        return;
      }
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
    graph_->set_may_have_unreachable_blocks();
    graph_->RemoveUnreachableBlocks();
  }
};

}  // namespace v8::internal::maglev

#endif  // V8_MAGLEV_MAGLEV_INLINING_H_
