// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/int64-lowering.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node.h"
#include "src/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

Int64Lowering::Int64Lowering(Graph* graph, MachineOperatorBuilder* machine,
                             CommonOperatorBuilder* common, Zone* zone)
    : graph_(graph),
      machine_(machine),
      common_(common),
      state_(graph, 4),
      stack_(zone),
      replacements_(zone->NewArray<Replacement>(graph->NodeCount())) {
  memset(replacements_, 0, sizeof(Replacement) * graph->NodeCount());
}

void Int64Lowering::ReduceGraph() {
  stack_.push(graph()->end());
  state_.Set(graph()->end(), State::kOnStack);

  while (!stack_.empty()) {
    Node* top = stack_.top();
    if (state_.Get(top) == State::kInputsPushed) {
      stack_.pop();
      state_.Set(top, State::kVisited);
      // All inputs of top have already been reduced, now reduce top.
      ReduceNode(top);
    } else {
      // Push all children onto the stack.
      for (Node* input : top->inputs()) {
        if (state_.Get(input) == State::kUnvisited) {
          stack_.push(input);
          state_.Set(input, State::kOnStack);
        }
      }
      state_.Set(top, State::kInputsPushed);
    }
  }
}

void Int64Lowering::ReduceNode(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kInt64Constant: {
      int64_t value = OpParameter<int64_t>(node);
      Node* low_node = graph()->NewNode(
          common()->Int32Constant(static_cast<int32_t>(value & 0xFFFFFFFF)));
      Node* high_node = graph()->NewNode(
          common()->Int32Constant(static_cast<int32_t>(value >> 32)));
      replacements_[node->id()].low = low_node;
      replacements_[node->id()].high = high_node;
      break;
    }
    case IrOpcode::kWord64And: {
      Node* left = node->InputAt(0);
      DCHECK(replacements_[left->id()].low);
      Node* left_low = replacements_[left->id()].low;
      Node* left_high = replacements_[left->id()].high;

      Node* right = node->InputAt(1);
      DCHECK(replacements_[right->id()].low);
      Node* right_low = replacements_[right->id()].low;
      Node* right_high = replacements_[right->id()].high;

      replacements_[node->id()].low =
          graph()->NewNode(machine()->Word32And(), left_low, right_low);
      replacements_[node->id()].high =
          graph()->NewNode(machine()->Word32And(), left_high, right_high);
      break;
    }
    case IrOpcode::kTruncateInt64ToInt32: {
      Node* input = node->InputAt(0);
      DCHECK(replacements_[input->id()].low);
      replacements_[node->id()].low = replacements_[input->id()].low;
      break;
    }
    default: {
      // Also the inputs of nodes can change which do not expect int64 inputs.
      for (int i = 0; i < node->InputCount(); i++) {
        Node* input = node->InputAt(i);
        // The input has changed altough it was not an int64 input. This can
        // happen e.g. if the input node is IrOpcode::kTruncateInt64ToInt32. We
        // use the low word replacement as the new input.
        if (replacements_[input->id()].low) {
          node->ReplaceInput(i, replacements_[input->id()].low);
        }
      }
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
