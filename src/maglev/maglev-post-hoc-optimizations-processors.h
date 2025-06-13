// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_POST_HOC_OPTIMIZATIONS_PROCESSORS_H_
#define V8_MAGLEV_MAGLEV_POST_HOC_OPTIMIZATIONS_PROCESSORS_H_

#include "absl/container/flat_hash_set.h"
#include "src/compiler/heap-refs.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-graph-builder.h"
#include "src/maglev/maglev-graph-printer.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir.h"
#include "src/objects/js-function.h"

namespace v8::internal::maglev {

class SweepIdentityNodes {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  void PostProcessBasicBlock(BasicBlock* block) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}
  ProcessResult Process(NodeBase* node, const ProcessingState& state) {
    for (int i = 0; i < node->input_count(); i++) {
      Input& input = node->input(i);
      while (input.node() && input.node()->Is<Identity>()) {
        node->change_input(i, input.node()->input(0).node());
      }
    }
    // While visiting the deopt info, the iterator will clear the identity nodes
    // automatically.
    if (node->properties().can_lazy_deopt()) {
      node->lazy_deopt_info()->ForEachInput([&](ValueNode* node) {});
    }
    if (node->properties().can_eager_deopt()) {
      node->eager_deopt_info()->ForEachInput([&](ValueNode* node) {});
    }
    return ProcessResult::kContinue;
  }
};

// Optimizations involving loops which cannot be done at graph building time.
// Currently mainly loop invariant code motion.
class LoopOptimizationProcessor {
 public:
  explicit LoopOptimizationProcessor(MaglevCompilationInfo* info)
      : zone(info->zone()) {
    was_deoptimized =
        info->toplevel_compilation_unit()->feedback().was_once_deoptimized();
  }

  void PreProcessGraph(Graph* graph) {}
  void PostPhiProcessing() {}

  void PostProcessBasicBlock(BasicBlock* block) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    current_block = block;
    if (current_block->is_loop()) {
      loop_effects = current_block->state()->loop_effects();
      if (loop_effects) return BlockProcessResult::kContinue;
    } else {
      // TODO(olivf): Some dominance analysis would allow us to keep loop
      // effects longer than just the first block of the loop.
      loop_effects = nullptr;
    }
    return BlockProcessResult::kSkip;
  }

  bool IsLoopPhi(Node* input) {
    DCHECK(current_block->is_loop());
    if (auto phi = input->TryCast<Phi>()) {
      if (phi->is_loop_phi() && phi->merge_state() == current_block->state()) {
        return true;
      }
    }
    return false;
  }

  bool CanHoist(Node* candidate) {
    DCHECK_EQ(candidate->input_count(), 1);
    DCHECK(current_block->is_loop());
    ValueNode* input = candidate->input(0).node();
    DCHECK(!IsLoopPhi(input));
    // For hoisting an instruction we need:
    // * A unique loop entry block.
    // * Inputs live before the loop (i.e., not defined inside the loop).
    // * No hoisting over checks (done eagerly by clearing loop_effects).
    // TODO(olivf): We should enforce loops having a unique entry block at graph
    // building time.
    if (current_block->predecessor_count() != 2) return false;
    BasicBlock* loop_entry = current_block->predecessor_at(0);
    if (loop_entry->successors().size() != 1) {
      return false;
    }
    if (IsConstantNode(input->opcode())) return true;
    return input->owner() != current_block;
  }

  ProcessResult Process(LoadTaggedFieldForContextSlotNoCells* ltf,
                        const ProcessingState& state) {
    DCHECK(loop_effects);
    ValueNode* object = ltf->object_input().node();
    if (IsLoopPhi(object)) {
      return ProcessResult::kContinue;
    }
    auto key = std::tuple{object, ltf->offset()};
    if (!loop_effects->may_have_aliasing_contexts &&
        !loop_effects->unstable_aspects_cleared &&
        !loop_effects->context_slot_written.count(key) && CanHoist(ltf)) {
      return ProcessResult::kHoist;
    }
    return ProcessResult::kContinue;
  }

  ProcessResult Process(LoadTaggedFieldForProperty* ltf,
                        const ProcessingState& state) {
    return ProcessNamedLoad(ltf, ltf->object_input().node(), ltf->name());
  }

  ProcessResult Process(StringLength* len, const ProcessingState& state) {
    return ProcessNamedLoad(
        len, len->object_input().node(),
        KnownNodeAspects::LoadedPropertyMapKey::StringLength());
  }

  ProcessResult Process(LoadTypedArrayLength* len,
                        const ProcessingState& state) {
    return ProcessNamedLoad(
        len, len->receiver_input().node(),
        KnownNodeAspects::LoadedPropertyMapKey::TypedArrayLength());
  }

  ProcessResult ProcessNamedLoad(Node* load, ValueNode* object,
                                 KnownNodeAspects::LoadedPropertyMapKey name) {
    DCHECK(!load->properties().can_deopt());
    if (!loop_effects) return ProcessResult::kContinue;
    if (IsLoopPhi(object)) {
      return ProcessResult::kContinue;
    }
    if (!loop_effects->unstable_aspects_cleared &&
        !loop_effects->keys_cleared.count(name) &&
        !loop_effects->objects_written.count(object) && CanHoist(load)) {
      return ProcessResult::kHoist;
    }
    return ProcessResult::kContinue;
  }

  ProcessResult Process(CheckMaps* maps, const ProcessingState& state) {
    DCHECK(loop_effects);
    // Hoisting a check out of a loop can cause it to trigger more than actually
    // needed (i.e., if the loop is executed 0 times). This could lead to
    // deoptimization loops as there is no feedback to learn here. Thus, we
    // abort this optimization if the function deoptimized previously. Also, if
    // hoisting of this check fails we need to abort (and not continue) to
    // ensure we are not hoisting other instructions over it.
    if (was_deoptimized) return ProcessResult::kSkipBlock;
    ValueNode* object = maps->receiver_input().node();
    if (IsLoopPhi(object)) {
      return ProcessResult::kSkipBlock;
    }
    if (!loop_effects->unstable_aspects_cleared && CanHoist(maps)) {
      if (auto j = current_block->predecessor_at(0)
                       ->control_node()
                       ->TryCast<CheckpointedJump>()) {
        maps->SetEagerDeoptInfo(zone, j->eager_deopt_info()->top_frame(),
                                maps->eager_deopt_info()->feedback_to_update());
        return ProcessResult::kHoist;
      }
    }
    return ProcessResult::kSkipBlock;
  }

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    // Ensure we are not hoisting over checks.
    if (node->properties().can_eager_deopt()) {
      loop_effects = nullptr;
      return ProcessResult::kSkipBlock;
    }
    return ProcessResult::kContinue;
  }

  void PostProcessGraph(Graph* graph) {}

  Zone* zone;
  BasicBlock* current_block;
  const LoopEffects* loop_effects;
  bool was_deoptimized;
};

template <typename NodeT>
constexpr bool CanBeStoreToNonEscapedObject() {
  return CanBeStoreToNonEscapedObject(NodeBase::opcode_of<NodeT>);
}

// The TruncationProcessor is an optimization pass that replaces
// floating-point operations with more efficient integer-based equivalents.
// It inspects the inputs of bitwise operations, which implicitly truncate
// their operands to 32-bit integers. If a floating-point input node (e.g.,
// Float64Add) can be proven to produce an integer-representable value, this
// pass replaces it with its integer counterpart (e.g., Int32Add), thus
// avoiding expensive floating-point arithmetic and conversions.
class TruncationProcessor {
 public:
  explicit TruncationProcessor(Graph* graph) : graph_(graph) {}

  constexpr static int kMaxInteger64Log2 = 64;
  constexpr static int kMaxSafeIntegerLog2 = 53;

#define TRACE(...)                                               \
  do {                                                           \
    if (V8_UNLIKELY(v8_flags.trace_maglev_truncation)) {         \
      std::cout << "[truncation]: " << __VA_ARGS__ << std::endl; \
    }                                                            \
  } while (false);
#define PRINT(node) \
  PrintNodeLabel(graph_->graph_labeller(), node) << ": " << node->opcode()

  void PreProcessGraph(Graph* graph) {}
  void PostProcessBasicBlock(BasicBlock* block) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    return ProcessResult::kContinue;
  }

#define BITWISE_BINOP_PROCESS(Name)                                 \
  ProcessResult Process(Name* node, const ProcessingState& state) { \
    TruncateInput(node, 0);                                         \
    TruncateInput(node, 1);                                         \
    return ProcessResult::kContinue;                                \
  }
  BITWISE_BINOP_PROCESS(Int32BitwiseAnd)
  BITWISE_BINOP_PROCESS(Int32BitwiseOr)
  BITWISE_BINOP_PROCESS(Int32BitwiseXor)
  BITWISE_BINOP_PROCESS(Int32ShiftLeft)
  BITWISE_BINOP_PROCESS(Int32ShiftRight)
  BITWISE_BINOP_PROCESS(Int32ShiftRightLogical)

  ProcessResult Process(Int32BitwiseNot* node, const ProcessingState& state) {
    TruncateInput(node, 0);
    return ProcessResult::kContinue;
  }

  void PostProcessGraph(Graph* graph) {}

 private:
  Graph* graph_;

  void TruncateInput(ValueNode* node, int index) {
    ValueNode* input = node->input(index).node();
    if (CanTruncate(input)) {
      node->change_input(index, Truncate(input));
    }
  }

  void UnsafeTruncateInput(ValueNode* node, int index) {
    ValueNode* input = node->input(index).node();
    node->change_input(index, Truncate(input));
  }

  // TODO(victorgomes): CanTruncate could be calculated during graph building.
  bool CanTruncate(ValueNode* node) {
    switch (node->opcode()) {
      // Constants
      case Opcode::kConstant:
        return node->Cast<Constant>()->object().IsHeapNumber();
      case Opcode::kRootConstant:
        return IsOddball(GetRootConstant(node));
      case Opcode::kFloat64Constant:
        return true;
      // Conversion nodes
      case Opcode::kChangeInt32ToFloat64:
        return true;
      case Opcode::kCheckedTruncateFloat64ToInt32:
      case Opcode::kTruncateFloat64ToInt32:
      case Opcode::kCheckedTruncateNumberOrOddballToInt32:
      case Opcode::kTruncateNumberOrOddballToInt32:
      case Opcode::kCheckedNumberToInt32:
        if (node->use_count() != 1) {
          TRACE("conversion node " << PRINT(node) << " has many uses");
          return false;
        }
        return CanTruncate(node->input(0).node());
      // Arithmetic pure operations
      case Opcode::kFloat64Add:
      case Opcode::kFloat64Subtract:
        return IsIntN(node, kMaxSafeIntegerLog2);
      // TODO(victorgomes): We need to guarantee that the multiplication
      // produces a safe integer. case Opcode::kFloat64Multiply:
      case Opcode::kFloat64Divide:
        if (node->use_count() != 1) {
          TRACE(PRINT(node) << " has many uses");
          return false;
        }
        // The operation can be truncated if the numerator is a safe integer.
        // The denominator can be any integer.
        return IsIntN(node->input(0).node(), kMaxSafeIntegerLog2) &&
               IsIntN(node->input(1).node(), kMaxInteger64Log2);
      default:
        return false;
    }
  }

  bool IsIntN(ValueNode* node, int nbits) {
    if (nbits <= 0) return false;
    switch (node->opcode()) {
      // Constants
      case Opcode::kConstant:
        return node->Cast<Constant>()->object().IsHeapNumber() &&
               IsIntN(node->Cast<Constant>()->object().AsHeapNumber().value(),
                      nbits);
      case Opcode::kRootConstant:
        return IsOddball(GetRootConstant(node)) &&
               IsIntN(Cast<Oddball>(GetRootConstant(node))->to_number_raw(),
                      nbits);
      case Opcode::kFloat64Constant:
        return IsIntN(node->Cast<Float64Constant>()->value().get_scalar(),
                      nbits);
      // Conversion nodes
      case Opcode::kChangeInt32ToFloat64:
        return nbits >= 32;
      case Opcode::kCheckedTruncateFloat64ToInt32:
      case Opcode::kTruncateFloat64ToInt32:
      case Opcode::kCheckedTruncateNumberOrOddballToInt32:
      case Opcode::kTruncateNumberOrOddballToInt32:
      case Opcode::kCheckedNumberToInt32:
        if (node->use_count() != 1) {
          TRACE("conversion node " << PRINT(node) << " has many uses");
          return false;
        }
        return IsIntN(node->input(0).node(), nbits);
      // Arithmetic pure operations
      case Opcode::kFloat64Add:
      case Opcode::kFloat64Subtract:
        if (node->use_count() != 1) {
          TRACE(PRINT(node) << " has many uses");
          return false;
        }
        // Integer addition/subtraction can be represented with one more bit
        // than its inputs.
        return IsIntN(node->input(0).node(), nbits - 1) &&
               IsIntN(node->input(1).node(), nbits - 1);
      default:
        return false;
    }
  }

  bool IsIntN(double value, int nbits) {
    DCHECK_LE(nbits, 64);
    if (nbits == 64) return true;
    if (nbits <= 0) return false;
    double limit = 1LL << (nbits - 1);
    return -limit <= value && value < limit && std::trunc(value) == value;
  }

  ValueNode* Truncate(ValueNode* node) {
    switch (node->opcode()) {
      // Constants
      case Opcode::kConstant:
        DCHECK(node->Cast<Constant>()->object().IsHeapNumber());
        return GetTruncatedInt32Constant(
            node->Cast<Constant>()->object().AsHeapNumber().value());
      case Opcode::kRootConstant:
        DCHECK(IsOddball(GetRootConstant(node)));
        return GetTruncatedInt32Constant(
            Cast<Oddball>(GetRootConstant(node))->to_number_raw());
      case Opcode::kFloat64Constant:
        return GetTruncatedInt32Constant(
            node->Cast<Float64Constant>()->value().get_scalar());
      // Conversion nodes
      case Opcode::kChangeInt32ToFloat64:
        TRACE("bypassing conversion node " << PRINT(node));
        return node->input(0).node();
      case Opcode::kCheckedTruncateFloat64ToInt32:
      case Opcode::kTruncateFloat64ToInt32: {
        TRACE("bypassing conversion node " << PRINT(node));
        ValueNode* input = node->input(0).node();
#ifdef DEBUG
        // This conversion node is now dead, since we recursively truncate its
        // input and return that instead. The recursive truncation can change
        // the representation of the input node, which would cause a type
        // mismatch for this (dead) node's input and crash the graph verifier.
        // We set the input to a constant zero to ensure the dead node remains
        // valid for the verifier.
        node->change_input(0, graph_->GetFloat64Constant(0));
#endif  // DEBUG
        return Truncate(input);
      }
      case Opcode::kCheckedTruncateNumberOrOddballToInt32:
      case Opcode::kTruncateNumberOrOddballToInt32:
      case Opcode::kCheckedNumberToInt32: {
        TRACE("bypassing conversion node " << PRINT(node));
        ValueNode* input = node->input(0).node();
#ifdef DEBUG
        // This conversion node is now dead, since we recursively truncate its
        // input and return that instead. The recursive truncation can change
        // the representation of the input node, which would cause a type
        // mismatch for this (dead) node's input and crash the graph verifier.
        // We set the input to a constant zero to ensure the dead node remains
        // valid for the verifier.
        node->change_input(0, graph_->GetSmiConstant(0));
#endif  // DEBUG
        return Truncate(input);
      }
      // Arithmetic pure operations
      case Opcode::kFloat64Add:
        return OverwriteWith<Int32Add>(node);
      case Opcode::kFloat64Subtract:
        return OverwriteWith<Int32Subtract>(node);
      // case Opcode::kFloat64Multiply:
      //   return OverwriteWith<Int32Multiply>(node);
      case Opcode::kFloat64Divide:
        return OverwriteWith<Int32Divide>(node);
      default:
        UNREACHABLE();
    }
  }

  template <typename NodeT>
  ValueNode* OverwriteWith(ValueNode* node) {
    TRACE("overwriting " << PRINT(node));
    UnsafeTruncateInput(node, 0);
    UnsafeTruncateInput(node, 1);
    node->OverwriteWith<NodeT>();
    // TODO(victorgomes): I don't think we should initialize register data in
    // the value node constructor, maybe choose a less prone place for it,
    // before register allocation.
    node->InitializeRegisterData();
    TRACE("   with " << PRINT(node));
    return node;
  }

  ValueNode* GetTruncatedInt32Constant(double constant) {
    return graph_->GetInt32Constant(DoubleToInt32(constant));
  }

  Tagged<Object> GetRootConstant(ValueNode* node) {
    return graph_->broker()->local_isolate()->root(
        node->Cast<RootConstant>()->index());
  }

#undef PRINT
#undef TRACE
};

class AnyUseMarkingProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessBasicBlock(BasicBlock* block) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    if constexpr (IsValueNode(Node::opcode_of<NodeT>) &&
                  (!NodeT::kProperties.is_required_when_unused() ||
                   std::is_same_v<ArgumentsElements, NodeT>)) {
      if (!node->is_used()) {
        if (!node->unused_inputs_were_visited()) {
          DropInputUses(node);
        }
        return ProcessResult::kRemove;
      }
    }

    if constexpr (CanBeStoreToNonEscapedObject<NodeT>()) {
      if (node->input(0).node()->template Is<InlinedAllocation>()) {
        stores_to_allocations_.push_back(node);
      }
    }

    return ProcessResult::kContinue;
  }

#ifdef DEBUG
  ProcessResult Process(Dead* node, const ProcessingState& state) {
    if (!v8_flags.maglev_untagged_phis) {
      // These nodes are removed in the phi representation selector, if we are
      // running without it. Just remove it here.
      return ProcessResult::kRemove;
    }
    UNREACHABLE();
  }
#endif  // DEBUG

  void PostProcessGraph(Graph* graph) {
    RunEscapeAnalysis(graph);
    DropUseOfValueInStoresToCapturedAllocations();
  }

 private:
  std::vector<Node*> stores_to_allocations_;

  void EscapeAllocation(Graph* graph, InlinedAllocation* alloc,
                        Graph::SmallAllocationVector& deps) {
    if (alloc->HasBeenAnalysed() && alloc->HasEscaped()) return;
    alloc->SetEscaped();
    for (auto dep : deps) {
      EscapeAllocation(graph, dep,
                       graph->allocations_escape_map().find(dep)->second);
    }
  }

  void VerifyEscapeAnalysis(Graph* graph) {
#ifdef DEBUG
    for (const auto& it : graph->allocations_escape_map()) {
      auto* alloc = it.first;
      DCHECK(alloc->HasBeenAnalysed());
      if (alloc->HasEscaped()) {
        for (auto* dep : it.second) {
          DCHECK(dep->HasEscaped());
        }
      }
    }
#endif  // DEBUG
  }

  void RunEscapeAnalysis(Graph* graph) {
    for (auto& it : graph->allocations_escape_map()) {
      auto* alloc = it.first;
      if (alloc->HasBeenAnalysed()) continue;
      // Check if all its uses are non escaping.
      if (alloc->IsEscaping()) {
        // Escape this allocation and all its dependencies.
        EscapeAllocation(graph, alloc, it.second);
      } else {
        // Try to capture the allocation. This can still change if a escaped
        // allocation has this value as one of its dependencies.
        alloc->SetElided();
      }
    }
    // Check that we've reached a fixpoint.
    VerifyEscapeAnalysis(graph);
  }

  void DropUseOfValueInStoresToCapturedAllocations() {
    for (Node* node : stores_to_allocations_) {
      InlinedAllocation* alloc =
          node->input(0).node()->Cast<InlinedAllocation>();
      // Since we don't analyze if allocations will escape until a fixpoint,
      // this could drop an use of an allocation and turn it non-escaping.
      if (alloc->HasBeenElided()) {
        // Skip first input.
        for (int i = 1; i < node->input_count(); i++) {
          DropInputUses(node->input(i));
        }
      }
    }
  }

  void DropInputUses(Input& input) {
    ValueNode* input_node = input.node();
    if (input_node->properties().is_required_when_unused() &&
        !input_node->Is<ArgumentsElements>())
      return;
    input_node->remove_use();
    if (!input_node->is_used() && !input_node->unused_inputs_were_visited()) {
      DropInputUses(input_node);
    }
  }

  void DropInputUses(ValueNode* node) {
    for (Input& input : *node) {
      DropInputUses(input);
    }
    DCHECK(!node->properties().can_eager_deopt());
    DCHECK(!node->properties().can_lazy_deopt());
    node->mark_unused_inputs_visited();
  }
};

class DeadNodeSweepingProcessor {
 public:
  void PreProcessGraph(Graph* graph) {
    if (graph->has_graph_labeller()) {
      labeller_ = graph->graph_labeller();
    }
  }
  void PostProcessGraph(Graph* graph) {}
  void PostProcessBasicBlock(BasicBlock* block) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}

  ProcessResult Process(AllocationBlock* node, const ProcessingState& state) {
    // Note: this need to be done before ValueLocationConstraintProcessor, since
    // it access the allocation offsets.
    int size = 0;
    for (auto alloc : node->allocation_list()) {
      if (alloc->HasEscaped()) {
        alloc->set_offset(size);
        size += alloc->size();
      }
    }
    // ... and update its size.
    node->set_size(size);
    // If size is zero, then none of the inlined allocations have escaped, we
    // can remove the allocation block.
    if (size == 0) return ProcessResult::kRemove;
    return ProcessResult::kContinue;
  }

  ProcessResult Process(InlinedAllocation* node, const ProcessingState& state) {
    // Remove inlined allocation that became non-escaping.
    if (!node->HasEscaped()) {
      if (v8_flags.trace_maglev_escape_analysis) {
        std::cout << "* Removing allocation node "
                  << PrintNodeLabel(labeller_, node) << std::endl;
      }
      return ProcessResult::kRemove;
    }
    return ProcessResult::kContinue;
  }

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    if constexpr (IsValueNode(Node::opcode_of<NodeT>) &&
                  (!NodeT::kProperties.is_required_when_unused() ||
                   std::is_same_v<ArgumentsElements, NodeT>)) {
      if (!node->is_used()) {
        return ProcessResult::kRemove;
      }
      return ProcessResult::kContinue;
    }

    if constexpr (CanBeStoreToNonEscapedObject<NodeT>()) {
      if (InlinedAllocation* object =
              node->input(0).node()->template TryCast<InlinedAllocation>()) {
        if (!object->HasEscaped()) {
          if (v8_flags.trace_maglev_escape_analysis) {
            std::cout << "* Removing store node "
                      << PrintNodeLabel(labeller_, node) << " to allocation "
                      << PrintNodeLabel(labeller_, object) << std::endl;
          }
          return ProcessResult::kRemove;
        }
      }
    }
    return ProcessResult::kContinue;
  }

 private:
  MaglevGraphLabeller* labeller_ = nullptr;
};

}  // namespace v8::internal::maglev

#endif  // V8_MAGLEV_MAGLEV_POST_HOC_OPTIMIZATIONS_PROCESSORS_H_
