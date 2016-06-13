// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/checkpoint-elimination.h"

#include "src/compiler/node-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

CheckpointElimination::CheckpointElimination(Editor* editor)
    : AdvancedReducer(editor) {}

namespace {

// The given checkpoint is redundant if it is effect-wise dominated by another
// checkpoint and there is no observable write in between. For now we consider
// a linear effect chain only instead of true effect-wise dominance.
bool IsRedundantCheckpoint(Node* node) {
  Node* effect = NodeProperties::GetEffectInput(node);
  while (effect->op()->HasProperty(Operator::kNoWrite) &&
         effect->op()->EffectInputCount() == 1) {
    if (effect->opcode() == IrOpcode::kCheckpoint) return true;
    effect = NodeProperties::GetEffectInput(effect);
  }
  return false;
}

}  // namespace

Reduction CheckpointElimination::Reduce(Node* node) {
  if (node->opcode() != IrOpcode::kCheckpoint) return NoChange();
  if (IsRedundantCheckpoint(node)) {
    return Replace(NodeProperties::GetEffectInput(node));
  }
  return NoChange();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
