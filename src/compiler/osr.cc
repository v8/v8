// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/control-reducer.h"
#include "src/compiler/frame.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/osr.h"
#include "src/scopes.h"

namespace v8 {
namespace internal {
namespace compiler {

OsrHelper::OsrHelper(CompilationInfo* info)
    : parameter_count_(info->scope()->num_parameters()),
      stack_slot_count_(info->scope()->num_stack_slots()) {}


void OsrHelper::Deconstruct(JSGraph* jsgraph, CommonOperatorBuilder* common,
                            Zone* tmp_zone) {
  NodeDeque queue(tmp_zone);
  Graph* graph = jsgraph->graph();
  NodeMarker<bool> marker(graph, 2);
  queue.push_back(graph->end());
  marker.Set(graph->end(), true);

  while (!queue.empty()) {
    Node* node = queue.front();
    queue.pop_front();

    // Rewrite OSR-related nodes.
    switch (node->opcode()) {
      case IrOpcode::kOsrNormalEntry:
        node->ReplaceUses(graph->NewNode(common->Dead()));
        break;
      case IrOpcode::kOsrLoopEntry:
        node->ReplaceUses(graph->start());
        break;
      default:
        break;
    }
    for (Node* const input : node->inputs()) {
      if (!marker.Get(input)) {
        marker.Set(input, true);
        queue.push_back(input);
      }
    }
  }

  ControlReducer::ReduceGraph(tmp_zone, jsgraph, common);
}


void OsrHelper::SetupFrame(Frame* frame) {
  // The optimized frame will subsume the unoptimized frame. Do so by reserving
  // the first spill slots.
  frame->ReserveSpillSlots(UnoptimizedFrameSlots());
}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
