// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/escape-analysis-reducer.h"

#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

EscapeAnalysisReducer::EscapeAnalysisReducer(Editor* editor, JSGraph* jsgraph,
                                             EscapeAnalysis* escape_analysis,
                                             Zone* zone)
    : AdvancedReducer(editor),
      jsgraph_(jsgraph),
      escape_analysis_(escape_analysis),
      zone_(zone) {}


Reduction EscapeAnalysisReducer::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kLoadField:
    case IrOpcode::kLoadElement:
      return ReduceLoad(node);
    case IrOpcode::kStoreField:
    case IrOpcode::kStoreElement:
      return ReduceStore(node);
    case IrOpcode::kAllocate:
      return ReduceAllocate(node);
    case IrOpcode::kFinishRegion:
      return ReduceFinishRegion(node);
    case IrOpcode::kReferenceEqual:
      return ReduceReferenceEqual(node);
    case IrOpcode::kObjectIsSmi:
      return ReduceObjectIsSmi(node);
    case IrOpcode::kStateValues:
    case IrOpcode::kFrameState:
      return ReplaceWithDeoptDummy(node);
    default:
      break;
  }
  return NoChange();
}


Reduction EscapeAnalysisReducer::ReduceLoad(Node* node) {
  DCHECK(node->opcode() == IrOpcode::kLoadField ||
         node->opcode() == IrOpcode::kLoadElement);
  if (Node* rep = escape_analysis()->GetReplacement(node)) {
    if (FLAG_trace_turbo_escape) {
      PrintF("Replaced #%d (%s) with #%d (%s)\n", node->id(),
             node->op()->mnemonic(), rep->id(), rep->op()->mnemonic());
    }
    ReplaceWithValue(node, rep);
    return Changed(rep);
  }
  return NoChange();
}


Reduction EscapeAnalysisReducer::ReduceStore(Node* node) {
  DCHECK(node->opcode() == IrOpcode::kStoreField ||
         node->opcode() == IrOpcode::kStoreElement);
  if (escape_analysis()->IsVirtual(NodeProperties::GetValueInput(node, 0))) {
    if (FLAG_trace_turbo_escape) {
      PrintF("Removed #%d (%s) from effect chain\n", node->id(),
             node->op()->mnemonic());
    }
    RelaxEffectsAndControls(node);
    return Changed(node);
  }
  return NoChange();
}


Reduction EscapeAnalysisReducer::ReduceAllocate(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kAllocate);
  if (escape_analysis()->IsVirtual(node)) {
    RelaxEffectsAndControls(node);
    if (FLAG_trace_turbo_escape) {
      PrintF("Removed allocate #%d from effect chain\n", node->id());
    }
    return Changed(node);
  }
  return NoChange();
}


Reduction EscapeAnalysisReducer::ReduceFinishRegion(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kFinishRegion);
  Node* effect = NodeProperties::GetEffectInput(node, 0);
  if (effect->opcode() == IrOpcode::kBeginRegion) {
    RelaxEffectsAndControls(effect);
    RelaxEffectsAndControls(node);
    if (FLAG_trace_turbo_escape) {
      PrintF("Removed region #%d / #%d from effect chain,", effect->id(),
             node->id());
      PrintF(" %d user(s) of #%d remain(s):", node->UseCount(), node->id());
      for (Edge edge : node->use_edges()) {
        PrintF(" #%d", edge.from()->id());
      }
      PrintF("\n");
    }
    return Changed(node);
  }
  return NoChange();
}


Reduction EscapeAnalysisReducer::ReduceReferenceEqual(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kReferenceEqual);
  Node* left = NodeProperties::GetValueInput(node, 0);
  Node* right = NodeProperties::GetValueInput(node, 1);
  if (escape_analysis()->IsVirtual(left)) {
    if (escape_analysis()->IsVirtual(right)) {
      if (Node* rep = escape_analysis()->GetReplacement(left)) {
        left = rep;
      }
      if (Node* rep = escape_analysis()->GetReplacement(right)) {
        right = rep;
      }
      // TODO(sigurds): What to do if either is a PHI?
      if (left == right) {
        ReplaceWithValue(node, jsgraph()->TrueConstant());
        if (FLAG_trace_turbo_escape) {
          PrintF("Replaced ref eq #%d with true\n", node->id());
        }
        return Replace(node);
      }
    }
    // Right-hand side is either not virtual, or a different node.
    ReplaceWithValue(node, jsgraph()->FalseConstant());
    if (FLAG_trace_turbo_escape) {
      PrintF("Replaced ref eq #%d with false\n", node->id());
    }
    return Replace(node);
  } else if (escape_analysis()->IsVirtual(right)) {
    // Left-hand side is not a virtual object.
    ReplaceWithValue(node, jsgraph()->FalseConstant());
    if (FLAG_trace_turbo_escape) {
      PrintF("Replaced ref eq #%d with false\n", node->id());
    }
  }
  return NoChange();
}


Reduction EscapeAnalysisReducer::ReduceObjectIsSmi(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kObjectIsSmi);
  Node* input = NodeProperties::GetValueInput(node, 0);
  if (escape_analysis()->IsVirtual(input)) {
    ReplaceWithValue(node, jsgraph()->FalseConstant());
    if (FLAG_trace_turbo_escape) {
      PrintF("Replaced ObjectIsSmi #%d with false\n", node->id());
    }
    return Replace(node);
  }
  return NoChange();
}


// TODO(sigurds): This is a temporary solution until escape analysis
// supports deoptimization.
Reduction EscapeAnalysisReducer::ReplaceWithDeoptDummy(Node* node) {
  DCHECK(node->opcode() == IrOpcode::kStateValues ||
         node->opcode() == IrOpcode::kFrameState);
  Reduction r = NoChange();
  for (int i = 0; i < node->op()->ValueInputCount(); ++i) {
    Node* input = NodeProperties::GetValueInput(node, i);
    if (input->opcode() == IrOpcode::kFinishRegion ||
        input->opcode() == IrOpcode::kAllocate ||
        input->opcode() == IrOpcode::kPhi) {
      if (escape_analysis()->IsVirtual(input)) {
        NodeProperties::ReplaceValueInput(node, jsgraph()->UndefinedConstant(),
                                          i);
        if (FLAG_trace_turbo_escape) {
          PrintF("Replaced state value (#%d) input with dummy\n", node->id());
        }
        r = Changed(node);
      }
    }
  }
  return r;
}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
