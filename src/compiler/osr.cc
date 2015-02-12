// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler.h"
#include "src/compiler/all-nodes.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/control-reducer.h"
#include "src/compiler/frame.h"
#include "src/compiler/graph.h"
#include "src/compiler/graph-visualizer.h"
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


// Peel outer loops and rewire the graph so that control reduction can
// produce a properly formed graph.
static void PeelOuterLoopsForOsr(Graph* graph, CommonOperatorBuilder* common,
                                 Zone* tmp_zone, Node* dead,
                                 LoopTree* loop_tree, LoopTree::Loop* osr_loop,
                                 Node* osr_normal_entry, Node* osr_loop_entry) {
  const int original_count = graph->NodeCount();
  AllNodes all(tmp_zone, graph);
  NodeVector tmp_inputs(tmp_zone);
  Node* sentinel = graph->NewNode(dead->op());

  // Make a copy of the graph for each outer loop.
  ZoneVector<NodeVector*> copies(tmp_zone);
  for (LoopTree::Loop* loop = osr_loop->parent(); loop; loop = loop->parent()) {
    void* stuff = tmp_zone->New(sizeof(NodeVector));
    NodeVector* mapping =
        new (stuff) NodeVector(original_count, sentinel, tmp_zone);
    copies.push_back(mapping);

    // Prepare the mapping for OSR values and the OSR loop entry.
    mapping->at(osr_normal_entry->id()) = dead;
    mapping->at(osr_loop_entry->id()) = dead;
    // Don't duplicate the OSR values.
    for (Node* use : osr_loop_entry->uses()) {
      if (use->opcode() == IrOpcode::kOsrValue) mapping->at(use->id()) = use;
    }

    // The outer loops are dead in this copy.
    for (LoopTree::Loop* outer = loop->parent(); outer;
         outer = outer->parent()) {
      for (Node* node : loop_tree->HeaderNodes(outer)) {
        mapping->at(node->id()) = dead;
      }
    }

    // Copy all nodes.
    for (size_t i = 0; i < all.live.size(); i++) {
      Node* orig = all.live[i];
      Node* copy = mapping->at(orig->id());
      if (copy != sentinel) {
        // Mapping already exists.
        continue;
      }
      if (orig->InputCount() == 0) {
        // No need to copy leaf nodes.
        mapping->at(orig->id()) = orig;
        continue;
      }

      // Copy the node.
      tmp_inputs.clear();
      for (Node* input : orig->inputs()) {
        tmp_inputs.push_back(mapping->at(input->id()));
      }
      copy = graph->NewNode(orig->op(), orig->InputCount(), &tmp_inputs[0]);
      if (NodeProperties::IsTyped(orig)) {
        NodeProperties::SetBounds(copy, NodeProperties::GetBounds(orig));
      }
      mapping->at(orig->id()) = copy;
    }

    // Fix missing inputs.
    for (size_t i = 0; i < all.live.size(); i++) {
      Node* orig = all.live[i];
      Node* copy = mapping->at(orig->id());
      for (int j = 0; j < copy->InputCount(); j++) {
        Node* input = copy->InputAt(j);
        if (input == sentinel)
          copy->ReplaceInput(j, mapping->at(orig->InputAt(j)->id()));
      }
    }

    // Construct the transfer from the previous graph copies to the new copy.
    Node* loop_header = loop_tree->HeaderNode(loop);
    NodeVector* previous =
        copies.size() > 1 ? copies[copies.size() - 2] : nullptr;
    const int backedges = loop_header->op()->ControlInputCount() - 1;
    if (backedges == 1) {
      // Simple case. Map the incoming edges to the loop to the previous copy.
      for (Node* node : loop_tree->HeaderNodes(loop)) {
        Node* copy = mapping->at(node->id());
        Node* backedge = node->InputAt(1);
        if (previous) backedge = previous->at(backedge->id());
        copy->ReplaceInput(0, backedge);
      }
    } else {
      // Complex case. Multiple backedges. Introduce a merge for incoming edges.
      tmp_inputs.clear();
      for (int i = 0; i < backedges; i++) {
        Node* backedge = loop_header->InputAt(i + 1);
        if (previous) backedge = previous->at(backedge->id());
        tmp_inputs.push_back(backedge);
      }
      Node* merge =
          graph->NewNode(common->Merge(backedges), backedges, &tmp_inputs[0]);
      for (Node* node : loop_tree->HeaderNodes(loop)) {
        Node* copy = mapping->at(node->id());
        if (node == loop_header) {
          // The entry to the loop is the merge.
          copy->ReplaceInput(0, merge);
        } else {
          // Merge inputs to the phi at the loop entry.
          tmp_inputs.clear();
          for (int i = 0; i < backedges; i++) {
            Node* backedge = node->InputAt(i + 1);
            if (previous) backedge = previous->at(backedge->id());
            tmp_inputs.push_back(backedge);
          }
          tmp_inputs.push_back(merge);
          Node* phi =
              graph->NewNode(common->ResizeMergeOrPhi(node->op(), backedges),
                             backedges + 1, &tmp_inputs[0]);
          copy->ReplaceInput(0, phi);
        }
      }
    }
  }

  // Kill the outer loops in the original graph.
  for (LoopTree::Loop* outer = osr_loop->parent(); outer;
       outer = outer->parent()) {
    loop_tree->HeaderNode(outer)->ReplaceUses(dead);
  }

  // Merge the ends of the graph copies.
  Node* end = graph->end();
  tmp_inputs.clear();
  for (int i = -1; i < static_cast<int>(copies.size()); i++) {
    Node* input = end->InputAt(0);
    if (i >= 0) input = copies[i]->at(input->id());
    if (input->opcode() == IrOpcode::kMerge) {
      for (Node* node : input->inputs()) tmp_inputs.push_back(node);
    } else {
      tmp_inputs.push_back(input);
    }
  }
  int count = static_cast<int>(tmp_inputs.size());
  Node* merge = graph->NewNode(common->Merge(count), count, &tmp_inputs[0]);
  end->ReplaceInput(0, merge);

  if (FLAG_trace_turbo_graph) {  // Simple textual RPO.
    OFStream os(stdout);
    os << "-- Graph after OSR duplication -- " << std::endl;
    os << AsRPO(*graph);
  }
}


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

  Node* dead = graph->NewNode(common->Dead());
  LoopTree::Loop* loop = loop_tree->ContainingLoop(osr_loop);
  if (loop->depth() > 0) {
    PeelOuterLoopsForOsr(graph, common, tmp_zone, dead, loop_tree, loop,
                         osr_normal_entry, osr_loop_entry);
  }

  // Replace the normal entry with {Dead} and the loop entry with {Start}
  // and run the control reducer to clean up the graph.
  osr_normal_entry->ReplaceUses(dead);
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
