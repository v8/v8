// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/branch-condition-duplicator.h"

#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/graph.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/opcodes.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

bool IsBranch(Node* node) { return node->opcode() == IrOpcode::kBranch; }

bool CanDuplicate(Node* node) {
  if (node->op()->EffectOutputCount() > 0) {
    return false;
  }
  if (node->op()->ControlOutputCount() > 0) {
    return false;
  }
  switch (node->opcode()) {
    case IrOpcode::kProjection:
    case IrOpcode::kParameter:
    case IrOpcode::kOsrValue:
      return false;
    default:
      break;
  }
  int all_inputs_have_only_a_single_use = true;
  for (Node* input : node->inputs()) {
    if (input->UseCount() > 1) {
      all_inputs_have_only_a_single_use = false;
    }
  }
  if (all_inputs_have_only_a_single_use) {
    // Duplicating this node would increase the lifespan of its inputs, which
    // could increase register pressure.
    return false;
  }
  return true;
}

}  // namespace

Node* BranchConditionDuplicator::DuplicateNode(Node* node) {
  return graph_->CloneNode(node);
}

void BranchConditionDuplicator::DuplicateConditionIfNeeded(Node* node) {
  if (!IsBranch(node)) return;

  Node* condNode = node->InputAt(0);
  if (condNode->UseCount() > 1 && CanDuplicate(condNode)) {
    node->ReplaceInput(0, DuplicateNode(condNode));
  }
}

void BranchConditionDuplicator::Enqueue(Node* node) {
  if (seen_.Get(node)) return;
  seen_.Set(node, true);
  to_visit_.push(node);
}

void BranchConditionDuplicator::VisitNode(Node* node) {
  DuplicateConditionIfNeeded(node);

  for (int i = 0; i < node->op()->ControlInputCount(); i++) {
    Enqueue(NodeProperties::GetControlInput(node, i));
  }
}

void BranchConditionDuplicator::ProcessGraph() {
  Enqueue(graph_->end());
  while (!to_visit_.empty()) {
    Node* node = to_visit_.front();
    to_visit_.pop();
    VisitNode(node);
  }
}

BranchConditionDuplicator::BranchConditionDuplicator(Zone* zone, Graph* graph)
    : graph_(graph), to_visit_(zone), seen_(graph, 2) {}

void BranchConditionDuplicator::Reduce() { ProcessGraph(); }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
