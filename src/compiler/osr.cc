// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/control-reducer.h"
#include "src/compiler/frame.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/loop-analysis.h"
#include "src/compiler/node.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/osr.h"
#include "src/scopes.h"

namespace v8 {
namespace internal {
namespace compiler {

OsrHelper::OsrHelper(CompilationInfo* info)
    : parameter_count_(info->scope()->num_parameters()),
      stack_slot_count_(info->scope()->num_stack_slots() +
                        info->osr_expr_stack_height()) {}


bool OsrHelper::Deconstruct(JSGraph* jsgraph, CommonOperatorBuilder* common,
                            Zone* tmp_zone) {
  Graph* graph = jsgraph->graph();
  Node* osr_normal_entry = nullptr;
  Node* osr_loop_entry = nullptr;
  Node* osr_loop = nullptr;

  for (Node* node : graph->start()->uses()) {
    if (node->opcode() == IrOpcode::kOsrLoopEntry) {
      osr_loop_entry = node;  // found the OSR loop entry
    } else if (node->opcode() == IrOpcode::kOsrNormalEntry) {
      osr_normal_entry = node;
    }
  }

  if (osr_loop_entry == nullptr) {
    // No OSR entry found, do nothing.
    CHECK(osr_normal_entry);
    return true;
  }

  for (Node* use : osr_loop_entry->uses()) {
    if (use->opcode() == IrOpcode::kLoop) {
      CHECK(!osr_loop);             // should be only one OSR loop.
      osr_loop = use;               // found the OSR loop.
    }
  }

  CHECK(osr_loop);  // Should have found the OSR loop.

  // Analyze the graph to determine how deeply nested the OSR loop is.
  LoopTree* loop_tree = LoopFinder::BuildLoopTree(graph, tmp_zone);

  LoopTree::Loop* loop = loop_tree->ContainingLoop(osr_loop);
  if (loop->depth() > 0) return false;  // cannot OSR inner loops yet.

  // TODO(titzer): perform loop peeling or graph duplication.

  // Replace the normal entry with {Dead} and the loop entry with {Start}
  // and run the control reducer to clean up the graph.
  osr_normal_entry->ReplaceUses(graph->NewNode(common->Dead()));
  osr_loop_entry->ReplaceUses(graph->start());
  ControlReducer::ReduceGraph(tmp_zone, jsgraph, common);

  return true;
}


void OsrHelper::SetupFrame(Frame* frame) {
  // The optimized frame will subsume the unoptimized frame. Do so by reserving
  // the first spill slots.
  frame->ReserveSpillSlots(UnoptimizedFrameSlots());
  // The frame needs to be adjusted by the number of unoptimized frame slots.
  frame->SetOsrStackSlotCount(static_cast<int>(UnoptimizedFrameSlots()));
}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
