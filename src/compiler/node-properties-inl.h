// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NODE_PROPERTIES_INL_H_
#define V8_COMPILER_NODE_PROPERTIES_INL_H_

#include "src/v8.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/operator-properties-inl.h"
#include "src/compiler/operator-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

// -----------------------------------------------------------------------------
// Input counts & layout.
// Inputs are always arranged in order as follows:
//     0 [ values, context, effects, control ] node->InputCount()

inline bool NodeProperties::HasValueInput(Node* node) {
  return OperatorProperties::GetValueInputCount(node->op()) > 0;
}

inline bool NodeProperties::HasContextInput(Node* node) {
  return OperatorProperties::HasContextInput(node->op());
}

inline bool NodeProperties::HasEffectInput(Node* node) {
  return OperatorProperties::GetEffectInputCount(node->op()) > 0;
}

inline bool NodeProperties::HasControlInput(Node* node) {
  return OperatorProperties::GetControlInputCount(node->op()) > 0;
}


inline int NodeProperties::GetValueInputCount(Node* node) {
  return OperatorProperties::GetValueInputCount(node->op());
}

inline int NodeProperties::GetContextInputCount(Node* node) {
  return OperatorProperties::HasContextInput(node->op()) ? 1 : 0;
}

inline int NodeProperties::GetEffectInputCount(Node* node) {
  return OperatorProperties::GetEffectInputCount(node->op());
}

inline int NodeProperties::GetControlInputCount(Node* node) {
  return OperatorProperties::GetControlInputCount(node->op());
}


inline int NodeProperties::FirstValueIndex(Node* node) { return 0; }

inline int NodeProperties::FirstContextIndex(Node* node) {
  return PastValueIndex(node);
}

inline int NodeProperties::FirstEffectIndex(Node* node) {
  return PastContextIndex(node);
}

inline int NodeProperties::FirstControlIndex(Node* node) {
  return PastEffectIndex(node);
}


inline int NodeProperties::PastValueIndex(Node* node) {
  return FirstValueIndex(node) + GetValueInputCount(node);
}

inline int NodeProperties::PastContextIndex(Node* node) {
  return FirstContextIndex(node) + GetContextInputCount(node);
}

inline int NodeProperties::PastEffectIndex(Node* node) {
  return FirstEffectIndex(node) + GetEffectInputCount(node);
}

inline int NodeProperties::PastControlIndex(Node* node) {
  return FirstControlIndex(node) + GetControlInputCount(node);
}


// -----------------------------------------------------------------------------
// Input accessors.

inline Node* NodeProperties::GetValueInput(Node* node, int index) {
  DCHECK(0 <= index && index < GetValueInputCount(node));
  return node->InputAt(FirstValueIndex(node) + index);
}

inline Node* NodeProperties::GetContextInput(Node* node) {
  DCHECK(GetContextInputCount(node) > 0);
  return node->InputAt(FirstContextIndex(node));
}

inline Node* NodeProperties::GetEffectInput(Node* node, int index) {
  DCHECK(0 <= index && index < GetEffectInputCount(node));
  return node->InputAt(FirstEffectIndex(node) + index);
}

inline Node* NodeProperties::GetControlInput(Node* node, int index) {
  DCHECK(0 <= index && index < GetControlInputCount(node));
  return node->InputAt(FirstControlIndex(node) + index);
}


// -----------------------------------------------------------------------------
// Output counts.

inline bool NodeProperties::HasValueOutput(Node* node) {
  return GetValueOutputCount(node) > 0;
}

inline bool NodeProperties::HasEffectOutput(Node* node) {
  return node->opcode() == IrOpcode::kStart ||
         NodeProperties::GetEffectInputCount(node) > 0;
}

inline bool NodeProperties::HasControlOutput(Node* node) {
  return (node->opcode() != IrOpcode::kEnd && IsControl(node)) ||
         NodeProperties::CanLazilyDeoptimize(node);
}


inline int NodeProperties::GetValueOutputCount(Node* node) {
  return OperatorProperties::GetValueOutputCount(node->op());
}

inline int NodeProperties::GetEffectOutputCount(Node* node) {
  return HasEffectOutput(node) ? 1 : 0;
}

inline int NodeProperties::GetControlOutputCount(Node* node) {
  return node->opcode() == IrOpcode::kBranch ? 2 : HasControlOutput(node) ? 1
                                                                          : 0;
}


// -----------------------------------------------------------------------------
// Edge kinds.

inline bool NodeProperties::IsInputRange(Node::Edge edge, int first, int num) {
  // TODO(titzer): edge.index() is linear time;
  // edges maybe need to be marked as value/effect/control.
  if (num == 0) return false;
  int index = edge.index();
  return first <= index && index < first + num;
}

inline bool NodeProperties::IsValueEdge(Node::Edge edge) {
  Node* node = edge.from();
  return IsInputRange(edge, FirstValueIndex(node), GetValueInputCount(node));
}

inline bool NodeProperties::IsContextEdge(Node::Edge edge) {
  Node* node = edge.from();
  return IsInputRange(edge, FirstContextIndex(node),
                      GetContextInputCount(node));
}

inline bool NodeProperties::IsEffectEdge(Node::Edge edge) {
  Node* node = edge.from();
  return IsInputRange(edge, FirstEffectIndex(node), GetEffectInputCount(node));
}

inline bool NodeProperties::IsControlEdge(Node::Edge edge) {
  Node* node = edge.from();
  return IsInputRange(edge, FirstControlIndex(node),
                      GetControlInputCount(node));
}


// -----------------------------------------------------------------------------
// Miscellaneous predicates.

inline bool NodeProperties::IsControl(Node* node) {
  return IrOpcode::IsControlOpcode(node->opcode());
}

inline bool NodeProperties::IsBasicBlockBegin(Node* node) {
  return OperatorProperties::IsBasicBlockBegin(node->op());
}

inline bool NodeProperties::CanBeScheduled(Node* node) {
  return OperatorProperties::CanBeScheduled(node->op());
}

inline bool NodeProperties::HasFixedSchedulePosition(Node* node) {
  return OperatorProperties::HasFixedSchedulePosition(node->op());
}

inline bool NodeProperties::IsScheduleRoot(Node* node) {
  return OperatorProperties::IsScheduleRoot(node->op());
}


// -----------------------------------------------------------------------------
// Miscellaneous mutators.

inline void NodeProperties::ReplaceEffectInput(Node* node, Node* effect,
                                               int index) {
  DCHECK(index < GetEffectInputCount(node));
  return node->ReplaceInput(
      GetValueInputCount(node) + GetContextInputCount(node) + index, effect);
}

inline void NodeProperties::RemoveNonValueInputs(Node* node) {
  node->TrimInputCount(GetValueInputCount(node));
}


// -----------------------------------------------------------------------------
// Type Bounds.

inline Bounds NodeProperties::GetBounds(Node* node) { return node->bounds(); }

inline void NodeProperties::SetBounds(Node* node, Bounds b) {
  node->set_bounds(b);
}


inline bool NodeProperties::CanLazilyDeoptimize(Node* node) {
  return OperatorProperties::CanLazilyDeoptimize(node->op());
}
}
}
}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_NODE_PROPERTIES_INL_H_
