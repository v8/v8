// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/deoptimizer/deoptimize-reason.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/maglev/maglev-ir.h"
#include "src/maglev/maglev-tracer.h"

namespace v8 {
namespace internal {
namespace maglev {

#define TRACE_FOLD(...)                                       \
  if (V8_UNLIKELY(v8_flags.trace_maglev_fold_cold_branches && \
                  tracer.IsEnabled())) {                      \
    TraceLogger(tracer) << "[cold-branch] " << __VA_ARGS__;   \
  }

namespace {

enum class ColdSuccessor {
  kNotDeoptOnly,
  kFoldable,
  // The block ends in a Deopt but contains other nodes, which the fold does
  // not support.
  kDeoptWithNodes,
};

ColdSuccessor Classify(BasicBlock* block) {
  if (block->is_dead()) return ColdSuccessor::kNotDeoptOnly;
  if (!block->control_node()->Is<Deopt>()) return ColdSuccessor::kNotDeoptOnly;
  DCHECK(!block->is_loop());
  DCHECK(!block->is_exception_handler_block());
  // Split-edge form guarantees that a branch successor has exactly one
  // predecessor and thus no phis.
  DCHECK_EQ(block->predecessor_count(), 1);
  DCHECK(!block->has_phi());
  if (!block->nodes().empty()) return ColdSuccessor::kDeoptWithNodes;
  return ColdSuccessor::kFoldable;
}

std::string_view DeoptOnString(bool deopt_if_true) {
  return deopt_if_true ? "deopt_on=true" : "deopt_on=false";
}

template <typename NodeT, typename... Args>
NodeT* NewCheck(Zone* zone, std::initializer_list<ValueNode*> inputs,
                Args&&... args) {
  static_assert(NodeT::kProperties.can_eager_deopt());
  NodeT* node =
      NodeBase::New<NodeT>(zone, inputs.size(), std::forward<Args>(args)...);
  int i = 0;
  for (ValueNode* input : inputs) {
    node->set_input(i++, input);
  }
#ifdef DEBUG
  node->VerifyInputs();
#endif
  return node;
}

std::optional<AssertCondition> AssertConditionFor(Operation operation,
                                                  bool is_unsigned) {
  switch (operation) {
    case Operation::kEqual:
    case Operation::kStrictEqual:
      return AssertCondition::kEqual;
    case Operation::kLessThan:
      return is_unsigned ? AssertCondition::kUnsignedLessThan
                         : AssertCondition::kLessThan;
    case Operation::kLessThanOrEqual:
      return is_unsigned ? AssertCondition::kUnsignedLessThanEqual
                         : AssertCondition::kLessThanEqual;
    case Operation::kGreaterThan:
      return is_unsigned ? AssertCondition::kUnsignedGreaterThan
                         : AssertCondition::kGreaterThan;
    case Operation::kGreaterThanOrEqual:
      return is_unsigned ? AssertCondition::kUnsignedGreaterThanEqual
                         : AssertCondition::kGreaterThanEqual;
    default:
      return {};
  }
}

// Exact logical negation. This is only correct for integer comparisons; do
// not use for floating point conditions, where NaN breaks the duality.
AssertCondition NegateAssertCondition(AssertCondition condition) {
  switch (condition) {
    case AssertCondition::kEqual:
      return AssertCondition::kNotEqual;
    case AssertCondition::kNotEqual:
      return AssertCondition::kEqual;
    case AssertCondition::kLessThan:
      return AssertCondition::kGreaterThanEqual;
    case AssertCondition::kLessThanEqual:
      return AssertCondition::kGreaterThan;
    case AssertCondition::kGreaterThan:
      return AssertCondition::kLessThanEqual;
    case AssertCondition::kGreaterThanEqual:
      return AssertCondition::kLessThan;
    case AssertCondition::kUnsignedLessThan:
      return AssertCondition::kUnsignedGreaterThanEqual;
    case AssertCondition::kUnsignedLessThanEqual:
      return AssertCondition::kUnsignedGreaterThan;
    case AssertCondition::kUnsignedGreaterThan:
      return AssertCondition::kUnsignedLessThanEqual;
    case AssertCondition::kUnsignedGreaterThanEqual:
      return AssertCondition::kUnsignedLessThan;
  }
}

// Builds a check node that deopts exactly when the branch would have gone to
// its deopting successor: when {deopt_if_true} the check must deopt iff the
// branch condition holds, otherwise iff it does not hold. Returns nullptr for
// branch conditions that have no equivalent check node (yet).
Node* TryBuildCheckNode(Graph* graph, BranchControlNode* branch,
                        bool deopt_if_true, DeoptimizeReason reason) {
  Zone* zone = graph->zone();
  switch (branch->opcode()) {
    case Opcode::kBranchIfSmi: {
      ValueNode* value = branch->input(0).node();
      if (deopt_if_true) return NewCheck<CheckHeapObject>(zone, {value});
      return NewCheck<CheckSmi>(zone, {value});
    }
    case Opcode::kBranchIfToBooleanTrue: {
      CheckType check_type =
          branch->Cast<BranchIfToBooleanTrue>()->check_type();
      return NewCheck<CheckToBoolean>(zone, {branch->input(0).node()},
                                      check_type, !deopt_if_true, reason);
    }
    case Opcode::kBranchIfInt32ToBooleanTrue: {
      ValueNode* value = branch->input(0).node();
      if (deopt_if_true) {
        return NewCheck<CheckValueEqualsInt32>(zone, {value}, 0, reason);
      }
      return NewCheck<CheckInt32Condition>(zone,
                                           {value, graph->GetInt32Constant(0)},
                                           AssertCondition::kNotEqual, reason);
    }
    case Opcode::kBranchIfInt32Compare:
    case Opcode::kBranchIfUint32Compare: {
      bool is_unsigned = branch->opcode() == Opcode::kBranchIfUint32Compare;
      Operation operation =
          is_unsigned ? branch->Cast<BranchIfUint32Compare>()->operation()
                      : branch->Cast<BranchIfInt32Compare>()->operation();
      std::optional<AssertCondition> condition =
          AssertConditionFor(operation, is_unsigned);
      if (!condition.has_value()) return nullptr;
      if (deopt_if_true) condition = NegateAssertCondition(*condition);
      return NewCheck<CheckInt32Condition>(
          zone, {branch->input(0).node(), branch->input(1).node()}, *condition,
          reason);
    }
    case Opcode::kBranchIfFloat64Compare: {
      Operation operation = branch->Cast<BranchIfFloat64Compare>()->operation();
      switch (operation) {
        case Operation::kEqual:
        case Operation::kStrictEqual:
        case Operation::kLessThan:
        case Operation::kLessThanOrEqual:
        case Operation::kGreaterThan:
        case Operation::kGreaterThanOrEqual:
          break;
        default:
          UNREACHABLE();
      }
      return NewCheck<CheckFloat64Condition>(
          zone, {branch->input(0).node(), branch->input(1).node()}, operation,
          !deopt_if_true, reason);
    }
    case Opcode::kBranchIfReferenceEqual: {
      return NewCheck<CheckDynamicValue>(
          zone, {branch->input(0).node(), branch->input(1).node()},
          !deopt_if_true, reason);
    }
    case Opcode::kBranchIfRootConstant: {
      RootIndex root_index = branch->Cast<BranchIfRootConstant>()->root_index();
      return NewCheck<CheckRootConstant>(zone, {branch->input(0).node()},
                                         root_index, !deopt_if_true, reason);
    }
    case Opcode::kBranchIfUndetectable: {
      CheckType check_type = branch->Cast<BranchIfUndetectable>()->check_type();
      return NewCheck<CheckUndetectable>(zone, {branch->input(0).node()},
                                         check_type, !deopt_if_true, reason);
    }
    default:
      return nullptr;
  }
}

bool TryFoldColdBranch(Graph* graph, const Tracer& tracer, BasicBlock* block,
                       BranchControlNode* branch, bool deopt_if_true) {
  BasicBlock* deopt_block =
      deopt_if_true ? branch->if_true() : branch->if_false();
  BasicBlock* live_block =
      deopt_if_true ? branch->if_false() : branch->if_true();
  Deopt* deopt = deopt_block->control_node()->Cast<Deopt>();

  Node* check = TryBuildCheckNode(graph, branch, deopt_if_true,
                                  deopt->deoptimize_reason());
  if (check == nullptr) {
    TRACE_FOLD("skip(unsupported) " << OpcodeToString(branch->opcode()) << " "
                                    << DeoptOnString(deopt_if_true));
    return false;
  }

  // The check deopts to the same frame (and bytecode target) as the Deopt node
  // it replaces, so reuse that frame rather than copying it.
  graph->AttachEagerDeoptInfo(check, &deopt->eager_deopt_info()->top_frame(),
                              deopt->eager_deopt_info()->feedback_to_update());
  if (graph->has_graph_labeller()) {
    graph->graph_labeller()->RegisterNode(check);
  }
  TRACE_FOLD("fold " << OpcodeToString(branch->opcode()) << " "
                     << DeoptOnString(deopt_if_true));
  block->nodes().push_back(check);

  // Unlink the deopt block and rewrite the branch into an unconditional
  // jump. This mirrors MaglevGraphOptimizer::FoldBranch.
  if (!deopt_block->has_state()) {
    deopt_block->set_predecessor(nullptr);
  } else {
    DCHECK_EQ(deopt_block->predecessor_count(), 1);
    DCHECK_EQ(deopt_block->predecessor_at(0), block);
    deopt_block->state()->RemovePredecessorAt(0);
  }

  Jump* jump = branch->OverwriteWith<Jump>();
  jump->set_target(live_block);
  constexpr int kPredecessorId = 0;
#ifdef DEBUG
  if (live_block->has_state()) {
    // Split-edge form guarantees that {live_block} has a single predecessor,
    // which is {block}.
    DCHECK_EQ(live_block->predecessor_count(), 1);
    DCHECK_EQ(live_block->predecessor_at(kPredecessorId), block);
  }
#endif
  jump->set_predecessor_id(kPredecessorId);

  graph->set_may_have_unreachable_blocks(true);
  return true;
}

}  // namespace

void Graph::FoldColdBranches() {
  DCHECK(may_have_cold_branches());
  // OSR compiles are excluded: their loops typically exit into not-yet-warm
  // (deopt-only) code, and folding such a branch removes the loop's only
  // forward exit edge. The non-eager loop peeler (MaglevLoopPeeler, used by
  // Turbolev) then bails out on the loop, since it joins the peeled and the
  // original iteration at a merge on the exit edge. Maglev's own peeler is
  // unaffected: it peels while building the graph, before this pass runs.
  // TODO(victorgomes): Once we lift the exit edge restriction in the peeler,
  // we can remove the OSR check and run this pass for OSR compiles as well.
  DCHECK(!is_osr());
  Tracer tracer(compilation_info());

  bool folded_any = false;
  for (BasicBlock* block : *this) {
    if (block->is_dead()) continue;
    ControlNode* control = block->control_node();
    if (!control->Is<BranchControlNode>()) continue;
    BranchControlNode* branch = control->Cast<BranchControlNode>();
    if (branch->if_true() == branch->if_false()) continue;

    ColdSuccessor if_true = Classify(branch->if_true());
    ColdSuccessor if_false = Classify(branch->if_false());
    bool deopt_if_true;
    if (if_true == ColdSuccessor::kFoldable) {
      deopt_if_true = true;
    } else if (if_false == ColdSuccessor::kFoldable) {
      deopt_if_true = false;
    } else {
      if (if_true == ColdSuccessor::kDeoptWithNodes) {
        TRACE_FOLD("skip(deopt-with-nodes) " << OpcodeToString(branch->opcode())
                                             << " " << DeoptOnString(true));
      } else if (if_false == ColdSuccessor::kDeoptWithNodes) {
        TRACE_FOLD("skip(deopt-with-nodes) " << OpcodeToString(branch->opcode())
                                             << " " << DeoptOnString(false));
      }
      continue;
    }

    folded_any |= TryFoldColdBranch(this, tracer, block, branch, deopt_if_true);
  }
  if (folded_any) {
    DCHECK(may_have_unreachable_blocks());
    RemoveUnreachableBlocks();
  }
}

#undef TRACE_FOLD

}  // namespace maglev
}  // namespace internal
}  // namespace v8
