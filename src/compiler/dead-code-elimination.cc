// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/dead-code-elimination.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/operator-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

DeadCodeElimination::DeadCodeElimination(Editor* editor, Graph* graph,
                                         CommonOperatorBuilder* common)
    : AdvancedReducer(editor),
      graph_(graph),
      common_(common),
      dead_(graph->NewNode(common->Dead())),
      dead_value_(graph->NewNode(common->DeadValue())) {
  NodeProperties::SetType(dead_, Type::None());
  NodeProperties::SetType(dead_value_, Type::None());
}

namespace {

// True if we can guarantee that {node} will never actually produce a value or
// effect.
bool NoReturn(Node* node) {
  return node->opcode() == IrOpcode::kDead ||
         node->opcode() == IrOpcode::kUnreachable ||
         node->opcode() == IrOpcode::kDeadValue ||
         !NodeProperties::GetTypeOrAny(node)->IsInhabited();
}

bool HasDeadInput(Node* node) {
  for (Node* input : node->inputs()) {
    if (NoReturn(input)) return true;
  }
  return false;
}

}  // namespace

Reduction DeadCodeElimination::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kEnd:
      return ReduceEnd(node);
    case IrOpcode::kLoop:
    case IrOpcode::kMerge:
      return ReduceLoopOrMerge(node);
    case IrOpcode::kLoopExit:
      return ReduceLoopExit(node);
    default:
      return ReduceNode(node);
  }
  UNREACHABLE();
}


Reduction DeadCodeElimination::ReduceEnd(Node* node) {
  DCHECK_EQ(IrOpcode::kEnd, node->opcode());
  Node::Inputs inputs = node->inputs();
  DCHECK_LE(1, inputs.count());
  int live_input_count = 0;
  for (int i = 0; i < inputs.count(); ++i) {
    Node* const input = inputs[i];
    // Skip dead inputs.
    if (input->opcode() == IrOpcode::kDead) continue;
    // Compact live inputs.
    if (i != live_input_count) node->ReplaceInput(live_input_count, input);
    ++live_input_count;
  }
  if (live_input_count == 0) {
    return Replace(dead());
  } else if (live_input_count < inputs.count()) {
    node->TrimInputCount(live_input_count);
    NodeProperties::ChangeOp(node, common()->End(live_input_count));
    return Changed(node);
  }
  DCHECK_EQ(inputs.count(), live_input_count);
  return NoChange();
}


Reduction DeadCodeElimination::ReduceLoopOrMerge(Node* node) {
  DCHECK(IrOpcode::IsMergeOpcode(node->opcode()));
  Node::Inputs inputs = node->inputs();
  DCHECK_LE(1, inputs.count());
  // Count the number of live inputs to {node} and compact them on the fly, also
  // compacting the inputs of the associated {Phi} and {EffectPhi} uses at the
  // same time.  We consider {Loop}s dead even if only the first control input
  // is dead.
  int live_input_count = 0;
  if (node->opcode() != IrOpcode::kLoop ||
      node->InputAt(0)->opcode() != IrOpcode::kDead) {
    for (int i = 0; i < inputs.count(); ++i) {
      Node* const input = inputs[i];
      // Skip dead inputs.
      if (input->opcode() == IrOpcode::kDead) continue;
      // Compact live inputs.
      if (live_input_count != i) {
        node->ReplaceInput(live_input_count, input);
        for (Node* const use : node->uses()) {
          if (NodeProperties::IsPhi(use)) {
            DCHECK_EQ(inputs.count() + 1, use->InputCount());
            use->ReplaceInput(live_input_count, use->InputAt(i));
          }
        }
      }
      ++live_input_count;
    }
  }
  if (live_input_count == 0) {
    return Replace(dead());
  } else if (live_input_count == 1) {
    // Due to compaction above, the live input is at offset 0.
    for (Node* const use : node->uses()) {
      if (NodeProperties::IsPhi(use)) {
        Replace(use, use->InputAt(0));
      } else if (use->opcode() == IrOpcode::kLoopExit &&
                 use->InputAt(1) == node) {
        RemoveLoopExit(use);
      } else if (use->opcode() == IrOpcode::kTerminate) {
        DCHECK_EQ(IrOpcode::kLoop, node->opcode());
        Replace(use, dead());
      }
    }
    return Replace(node->InputAt(0));
  }
  DCHECK_LE(2, live_input_count);
  DCHECK_LE(live_input_count, inputs.count());
  // Trim input count for the {Merge} or {Loop} node.
  if (live_input_count < inputs.count()) {
    // Trim input counts for all phi uses and revisit them.
    for (Node* const use : node->uses()) {
      if (NodeProperties::IsPhi(use)) {
        use->ReplaceInput(live_input_count, node);
        TrimMergeOrPhi(use, live_input_count);
        Revisit(use);
      }
    }
    TrimMergeOrPhi(node, live_input_count);
    return Changed(node);
  }
  return NoChange();
}

Reduction DeadCodeElimination::RemoveLoopExit(Node* node) {
  DCHECK_EQ(IrOpcode::kLoopExit, node->opcode());
  for (Node* const use : node->uses()) {
    if (use->opcode() == IrOpcode::kLoopExitValue ||
        use->opcode() == IrOpcode::kLoopExitEffect) {
      Replace(use, use->InputAt(0));
    }
  }
  Node* control = NodeProperties::GetControlInput(node, 0);
  Replace(node, control);
  return Replace(control);
}

Reduction DeadCodeElimination::ReduceNode(Node* node) {
  int const effect_input_count = node->op()->EffectInputCount();
  int const control_input_count = node->op()->ControlInputCount();
  if (control_input_count == 0 && effect_input_count == 0) {
    return ReducePureNode(node);
  }

  if (control_input_count == 1) {
    // If {node} has exactly one control input and this is {Dead},
    // replace {node} with {Dead}.
    Node* control = NodeProperties::GetControlInput(node);
    if (control->opcode() == IrOpcode::kDead) return Replace(control);

    if (node->opcode() == IrOpcode::kPhi &&
        (PhiRepresentationOf(node->op()) == MachineRepresentation::kNone ||
         !NodeProperties::GetTypeOrAny(node)->IsInhabited())) {
      return Replace(dead_value());
    }
  }

  if (effect_input_count > 0 && !NodeProperties::IsPhi(node))
    return ReduceEffectNode(node);

  return NoChange();
}

Reduction DeadCodeElimination::ReducePureNode(Node* node) {
  DCHECK_EQ(0, node->op()->EffectInputCount());
  DCHECK_EQ(0, node->op()->ControlInputCount());
  int input_count = node->op()->ValueInputCount();
  for (int i = 0; i < input_count; ++i) {
    Node* input = NodeProperties::GetValueInput(node, i);
    if (NoReturn(input)) {
      return Replace(dead_value());
    }
  }
  return NoChange();
}

Reduction DeadCodeElimination::ReduceEffectNode(Node* node) {
  if (IrOpcode::IsGraphTerminator(node->opcode())) {
    return ReduceGraphTerminator(node);
  }

  DCHECK_EQ(1, node->op()->EffectInputCount());
  Node* effect = NodeProperties::GetEffectInput(node, 0);
  if (effect->opcode() == IrOpcode::kDead) {
    return Replace(effect);
  }
  if (HasDeadInput(node) && node->opcode() != IrOpcode::kIfException) {
    if (effect->opcode() == IrOpcode::kUnreachable) {
      RelaxEffectsAndControls(node);
      return Replace(dead_value());
    }

    if (node->opcode() == IrOpcode::kUnreachable) return NoChange();

    Node* control = node->op()->ControlInputCount() == 1
                        ? NodeProperties::GetControlInput(node, 0)
                        : graph()->start();
    Node* unreachable =
        graph()->NewNode(common()->Unreachable(), effect, control);
    ReplaceWithValue(node, dead_value(), node, control);
    return Replace(unreachable);
  }

  return NoChange();
}

Reduction DeadCodeElimination::ReduceGraphTerminator(Node* node) {
  DCHECK(IrOpcode::IsGraphTerminator(node->opcode()));
  if (node->opcode() == IrOpcode::kThrow) return NoChange();
  if (HasDeadInput(node)) {
    Node* effect = NodeProperties::GetEffectInput(node, 0);
    Node* control = NodeProperties::GetControlInput(node, 0);
    if (effect->opcode() != IrOpcode::kUnreachable) {
      effect = graph()->NewNode(common()->Unreachable(), effect, control);
    }
    node->TrimInputCount(2);
    node->ReplaceInput(0, effect);
    node->ReplaceInput(1, control);
    NodeProperties::ChangeOp(node, common()->Throw());
    return Changed(node);
  }
  return NoChange();
}

Reduction DeadCodeElimination::ReduceLoopExit(Node* node) {
  Node* control = NodeProperties::GetControlInput(node, 0);
  Node* loop = NodeProperties::GetControlInput(node, 1);
  if (control->opcode() == IrOpcode::kDead ||
      loop->opcode() == IrOpcode::kDead) {
    return RemoveLoopExit(node);
  }
  return NoChange();
}

void DeadCodeElimination::TrimMergeOrPhi(Node* node, int size) {
  const Operator* const op = common()->ResizeMergeOrPhi(node->op(), size);
  node->TrimInputCount(OperatorProperties::GetTotalInputCount(op));
  NodeProperties::ChangeOp(node, op);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
