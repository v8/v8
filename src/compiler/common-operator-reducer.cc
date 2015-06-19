// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/common-operator-reducer.h"

#include <algorithm>

#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/node-matchers.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

enum class Decision { kUnknown, kTrue, kFalse };

Decision DecideCondition(Node* const cond) {
  switch (cond->opcode()) {
    case IrOpcode::kInt32Constant: {
      Int32Matcher mcond(cond);
      return mcond.Value() ? Decision::kTrue : Decision::kFalse;
    }
    case IrOpcode::kInt64Constant: {
      Int64Matcher mcond(cond);
      return mcond.Value() ? Decision::kTrue : Decision::kFalse;
    }
    case IrOpcode::kHeapConstant: {
      HeapObjectMatcher<HeapObject> mcond(cond);
      return mcond.Value().handle()->BooleanValue() ? Decision::kTrue
                                                    : Decision::kFalse;
    }
    default:
      return Decision::kUnknown;
  }
}

}  // namespace


Reduction CommonOperatorReducer::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kBranch:
      return ReduceBranch(node);
    case IrOpcode::kMerge:
      return ReduceMerge(node);
    case IrOpcode::kEffectPhi:
      return ReduceEffectPhi(node);
    case IrOpcode::kPhi:
      return ReducePhi(node);
    case IrOpcode::kSelect:
      return ReduceSelect(node);
    default:
      break;
  }
  return NoChange();
}


Reduction CommonOperatorReducer::ReduceBranch(Node* node) {
  DCHECK_EQ(IrOpcode::kBranch, node->opcode());
  Node* const cond = node->InputAt(0);
  Decision const decision = DecideCondition(cond);
  if (decision == Decision::kUnknown) return NoChange();
  Node* const control = node->InputAt(1);
  node->set_op(common()->Dead());
  node->TrimInputCount(0);
  for (Node* const use : node->uses()) {
    switch (use->opcode()) {
      case IrOpcode::kIfTrue:
        Replace(use, (decision == Decision::kTrue) ? control : node);
        break;
      case IrOpcode::kIfFalse:
        Replace(use, (decision == Decision::kFalse) ? control : node);
        break;
      default:
        UNREACHABLE();
    }
  }
  return Changed(node);
}


Reduction CommonOperatorReducer::ReduceMerge(Node* node) {
  DCHECK_EQ(IrOpcode::kMerge, node->opcode());
  //
  // Check if this is a merge that belongs to an unused diamond, which means
  // that:
  //
  //  a) the {Merge} has no {Phi} or {EffectPhi} uses, and
  //  b) the {Merge} has two inputs, one {IfTrue} and one {IfFalse}, which are
  //     both owned by the Merge, and
  //  c) and the {IfTrue} and {IfFalse} nodes point to the same {Branch}.
  //
  if (node->InputCount() == 2) {
    for (Node* const use : node->uses()) {
      if (IrOpcode::IsPhiOpcode(use->opcode())) return NoChange();
    }
    Node* if_true = node->InputAt(0);
    Node* if_false = node->InputAt(1);
    if (if_true->opcode() != IrOpcode::kIfTrue) std::swap(if_true, if_false);
    if (if_true->opcode() == IrOpcode::kIfTrue &&
        if_false->opcode() == IrOpcode::kIfFalse &&
        if_true->InputAt(0) == if_false->InputAt(0) && if_true->OwnedBy(node) &&
        if_false->OwnedBy(node)) {
      Node* const branch = if_true->InputAt(0);
      DCHECK_EQ(IrOpcode::kBranch, branch->opcode());
      DCHECK(branch->OwnedBy(if_true, if_false));
      Node* const control = branch->InputAt(1);
      // Mark the {branch} as {Dead}.
      branch->set_op(common()->Dead());
      branch->TrimInputCount(0);
      return Replace(control);
    }
  }
  return NoChange();
}


Reduction CommonOperatorReducer::ReduceEffectPhi(Node* node) {
  DCHECK_EQ(IrOpcode::kEffectPhi, node->opcode());
  int const input_count = node->InputCount() - 1;
  DCHECK_LE(1, input_count);
  Node* const merge = node->InputAt(input_count);
  DCHECK(IrOpcode::IsMergeOpcode(merge->opcode()));
  DCHECK_EQ(input_count, merge->InputCount());
  Node* const effect = node->InputAt(0);
  DCHECK_NE(node, effect);
  for (int i = 1; i < input_count; ++i) {
    Node* const input = node->InputAt(i);
    if (input == node) {
      // Ignore redundant inputs.
      DCHECK_EQ(IrOpcode::kLoop, merge->opcode());
      continue;
    }
    if (input != effect) return NoChange();
  }
  // We might now be able to further reduce the {merge} node.
  Revisit(merge);
  return Replace(effect);
}


Reduction CommonOperatorReducer::ReducePhi(Node* node) {
  DCHECK_EQ(IrOpcode::kPhi, node->opcode());
  int const input_count = node->InputCount() - 1;
  DCHECK_LE(1, input_count);
  Node* const merge = node->InputAt(input_count);
  DCHECK(IrOpcode::IsMergeOpcode(merge->opcode()));
  DCHECK_EQ(input_count, merge->InputCount());
  if (input_count == 2) {
    Node* vtrue = node->InputAt(0);
    Node* vfalse = node->InputAt(1);
    Node* if_true = merge->InputAt(0);
    Node* if_false = merge->InputAt(1);
    if (if_true->opcode() != IrOpcode::kIfTrue) {
      std::swap(if_true, if_false);
      std::swap(vtrue, vfalse);
    }
    if (if_true->opcode() == IrOpcode::kIfTrue &&
        if_false->opcode() == IrOpcode::kIfFalse &&
        if_true->InputAt(0) == if_false->InputAt(0)) {
      Node* const branch = if_true->InputAt(0);
      Node* const cond = branch->InputAt(0);
      if (cond->opcode() == IrOpcode::kFloat32LessThan) {
        Float32BinopMatcher mcond(cond);
        if (mcond.left().Is(0.0) && mcond.right().Equals(vtrue) &&
            vfalse->opcode() == IrOpcode::kFloat32Sub) {
          Float32BinopMatcher mvfalse(vfalse);
          if (mvfalse.left().IsZero() && mvfalse.right().Equals(vtrue)) {
            // We might now be able to further reduce the {merge} node.
            Revisit(merge);
            return Change(node, machine()->Float32Abs(), vtrue);
          }
        }
        if (mcond.left().Equals(vtrue) && mcond.right().Equals(vfalse) &&
            machine()->HasFloat32Min()) {
          // We might now be able to further reduce the {merge} node.
          Revisit(merge);
          return Change(node, machine()->Float32Min(), vtrue, vfalse);
        } else if (mcond.left().Equals(vfalse) && mcond.right().Equals(vtrue) &&
                   machine()->HasFloat32Max()) {
          // We might now be able to further reduce the {merge} node.
          Revisit(merge);
          return Change(node, machine()->Float32Max(), vtrue, vfalse);
        }
      } else if (cond->opcode() == IrOpcode::kFloat64LessThan) {
        Float64BinopMatcher mcond(cond);
        if (mcond.left().Is(0.0) && mcond.right().Equals(vtrue) &&
            vfalse->opcode() == IrOpcode::kFloat64Sub) {
          Float64BinopMatcher mvfalse(vfalse);
          if (mvfalse.left().IsZero() && mvfalse.right().Equals(vtrue)) {
            // We might now be able to further reduce the {merge} node.
            Revisit(merge);
            return Change(node, machine()->Float64Abs(), vtrue);
          }
        }
        if (mcond.left().Equals(vtrue) && mcond.right().Equals(vfalse) &&
            machine()->HasFloat64Min()) {
          // We might now be able to further reduce the {merge} node.
          Revisit(merge);
          return Change(node, machine()->Float64Min(), vtrue, vfalse);
        } else if (mcond.left().Equals(vfalse) && mcond.right().Equals(vtrue) &&
                   machine()->HasFloat64Max()) {
          // We might now be able to further reduce the {merge} node.
          Revisit(merge);
          return Change(node, machine()->Float64Max(), vtrue, vfalse);
        }
      }
    }
  }
  Node* const value = node->InputAt(0);
  DCHECK_NE(node, value);
  for (int i = 1; i < input_count; ++i) {
    Node* const input = node->InputAt(i);
    if (input == node) {
      // Ignore redundant inputs.
      DCHECK_EQ(IrOpcode::kLoop, merge->opcode());
      continue;
    }
    if (input != value) return NoChange();
  }
  // We might now be able to further reduce the {merge} node.
  Revisit(merge);
  return Replace(value);
}


Reduction CommonOperatorReducer::ReduceSelect(Node* node) {
  DCHECK_EQ(IrOpcode::kSelect, node->opcode());
  Node* const cond = node->InputAt(0);
  Node* const vtrue = node->InputAt(1);
  Node* const vfalse = node->InputAt(2);
  if (vtrue == vfalse) return Replace(vtrue);
  switch (DecideCondition(cond)) {
    case Decision::kTrue:
      return Replace(vtrue);
    case Decision::kFalse:
      return Replace(vfalse);
    case Decision::kUnknown:
      break;
  }
  switch (cond->opcode()) {
    case IrOpcode::kFloat32LessThan: {
      Float32BinopMatcher mcond(cond);
      if (mcond.left().Is(0.0) && mcond.right().Equals(vtrue) &&
          vfalse->opcode() == IrOpcode::kFloat32Sub) {
        Float32BinopMatcher mvfalse(vfalse);
        if (mvfalse.left().IsZero() && mvfalse.right().Equals(vtrue)) {
          return Change(node, machine()->Float32Abs(), vtrue);
        }
      }
      if (mcond.left().Equals(vtrue) && mcond.right().Equals(vfalse) &&
          machine()->HasFloat32Min()) {
        return Change(node, machine()->Float32Min(), vtrue, vfalse);
      } else if (mcond.left().Equals(vfalse) && mcond.right().Equals(vtrue) &&
                 machine()->HasFloat32Max()) {
        return Change(node, machine()->Float32Max(), vtrue, vfalse);
      }
      break;
    }
    case IrOpcode::kFloat64LessThan: {
      Float64BinopMatcher mcond(cond);
      if (mcond.left().Is(0.0) && mcond.right().Equals(vtrue) &&
          vfalse->opcode() == IrOpcode::kFloat64Sub) {
        Float64BinopMatcher mvfalse(vfalse);
        if (mvfalse.left().IsZero() && mvfalse.right().Equals(vtrue)) {
          return Change(node, machine()->Float64Abs(), vtrue);
        }
      }
      if (mcond.left().Equals(vtrue) && mcond.right().Equals(vfalse) &&
          machine()->HasFloat64Min()) {
        return Change(node, machine()->Float64Min(), vtrue, vfalse);
      } else if (mcond.left().Equals(vfalse) && mcond.right().Equals(vtrue) &&
                 machine()->HasFloat64Max()) {
        return Change(node, machine()->Float64Max(), vtrue, vfalse);
      }
      break;
    }
    default:
      break;
  }
  return NoChange();
}


Reduction CommonOperatorReducer::Change(Node* node, Operator const* op,
                                        Node* a) {
  node->set_op(op);
  node->ReplaceInput(0, a);
  node->TrimInputCount(1);
  return Changed(node);
}


Reduction CommonOperatorReducer::Change(Node* node, Operator const* op, Node* a,
                                        Node* b) {
  node->set_op(op);
  node->ReplaceInput(0, a);
  node->ReplaceInput(1, b);
  node->TrimInputCount(2);
  return Changed(node);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
