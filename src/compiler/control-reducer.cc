// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/common-operator.h"
#include "src/compiler/common-operator-reducer.h"
#include "src/compiler/control-reducer.h"
#include "src/compiler/graph.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE(...)                                       \
  do {                                                   \
    if (FLAG_trace_turbo_reduction) PrintF(__VA_ARGS__); \
  } while (false)

enum Decision { kFalse, kUnknown, kTrue };

class ControlReducerImpl final : public AdvancedReducer {
 public:
  Zone* zone_;
  JSGraph* jsgraph_;
  int max_phis_for_select_;

  ControlReducerImpl(Editor* editor, Zone* zone, JSGraph* jsgraph)
      : AdvancedReducer(editor),
        zone_(zone),
        jsgraph_(jsgraph),
        max_phis_for_select_(0) {}

  Graph* graph() { return jsgraph_->graph(); }
  CommonOperatorBuilder* common() { return jsgraph_->common(); }
  Node* dead() { return jsgraph_->DeadControl(); }

  //===========================================================================
  // Reducer implementation: perform reductions on a node.
  //===========================================================================
  Reduction Reduce(Node* node) override {
    if (node->op()->ControlInputCount() == 1 ||
        node->opcode() == IrOpcode::kLoop) {
      // If a node has only one control input and it is dead, replace with dead.
      Node* control = NodeProperties::GetControlInput(node);
      if (control->opcode() == IrOpcode::kDeadControl) {
        TRACE("ControlDead: #%d:%s\n", node->id(), node->op()->mnemonic());
        return Replace(control);
      }
    }

    Node* result = node;
    // Reduce branches, phis, and merges.
    switch (node->opcode()) {
      case IrOpcode::kBranch:
        result = ReduceBranch(node);
        break;
      case IrOpcode::kIfTrue:
        result = ReduceIfProjection(node, kTrue);
        break;
      case IrOpcode::kIfFalse:
        result = ReduceIfProjection(node, kFalse);
        break;
      case IrOpcode::kLoop:  // fallthrough
      case IrOpcode::kMerge:
        result = ReduceMerge(node);
        break;
      case IrOpcode::kEnd:
        result = ReduceEnd(node);
        break;
      default:
        break;
    }

    return result == node ? NoChange() : Replace(result);
  }

  // Try to statically fold a condition.
  Decision DecideCondition(Node* cond) {
    switch (cond->opcode()) {
      case IrOpcode::kInt32Constant:
        return Int32Matcher(cond).Is(0) ? kFalse : kTrue;
      case IrOpcode::kInt64Constant:
        return Int64Matcher(cond).Is(0) ? kFalse : kTrue;
      case IrOpcode::kHeapConstant: {
        Handle<HeapObject> object = HeapObjectMatcher(cond).Value().handle();
        return object->BooleanValue() ? kTrue : kFalse;
      }
      default:
        break;
    }
    return kUnknown;
  }

  // Reduce branches.
  Node* ReduceBranch(Node* branch) {
    if (DecideCondition(branch->InputAt(0)) != kUnknown) {
      for (Node* use : branch->uses()) Revisit(use);
    }
    return branch;
  }

  // Reduce end by trimming away dead inputs.
  Node* ReduceEnd(Node* node) {
    // Count the number of live inputs.
    int live = 0;
    for (int index = 0; index < node->InputCount(); ++index) {
      // Skip dead inputs.
      if (node->InputAt(index)->opcode() == IrOpcode::kDeadControl) continue;
      // Compact live inputs.
      if (index != live) node->ReplaceInput(live, node->InputAt(index));
      ++live;
    }

    TRACE("ReduceEnd: #%d:%s (%d of %d live)\n", node->id(),
          node->op()->mnemonic(), live, node->InputCount());

    if (live == 0) return dead();  // No remaining inputs.

    if (live < node->InputCount()) {
      node->set_op(common()->End(live));
      node->TrimInputCount(live);
    }

    return node;
  }

  // Reduce merges by trimming away dead inputs from the merge and phis.
  Node* ReduceMerge(Node* node) {
    // Count the number of live inputs.
    int live = 0;
    int index = 0;
    int live_index = 0;
    for (Node* const input : node->inputs()) {
      if (input->opcode() != IrOpcode::kDeadControl) {
        live++;
        live_index = index;
      }
      index++;
    }

    TRACE("ReduceMerge: #%d:%s (%d of %d live)\n", node->id(),
          node->op()->mnemonic(), live, index);

    if (live == 0) return dead();  // no remaining inputs.

    // Gather terminates, phis and effect phis to be edited.
    NodeVector phis(zone_);
    Node* terminate = nullptr;
    for (Node* const use : node->uses()) {
      if (NodeProperties::IsPhi(use)) {
        phis.push_back(use);
      } else if (use->opcode() == IrOpcode::kTerminate) {
        DCHECK_NULL(terminate);
        terminate = use;
      }
    }

    if (live == 1) {
      // All phis are redundant. Replace them with their live input.
      for (Node* const phi : phis) {
        Replace(phi, phi->InputAt(live_index));
      }
      // The terminate is not needed anymore.
      if (terminate) Replace(terminate, dead());
      // The merge itself is redundant.
      return node->InputAt(live_index);
    }

    DCHECK_LE(2, live);

    if (live < node->InputCount()) {
      // Edit phis in place, removing dead inputs and revisiting them.
      for (Node* const phi : phis) {
        TRACE("  PhiInMerge: #%d:%s (%d live)\n", phi->id(),
              phi->op()->mnemonic(), live);
        RemoveDeadInputs(node, phi);
        Revisit(phi);
      }
      // Edit the merge in place, removing dead inputs.
      RemoveDeadInputs(node, node);
    }

    DCHECK_EQ(live, node->InputCount());

    // Try to remove dead diamonds or introduce selects.
    if (live == 2 && CheckPhisForSelect(phis)) {
      DiamondMatcher matcher(node);
      if (matcher.Matched() && matcher.IfProjectionsAreOwned()) {
        // Dead diamond, i.e. neither the IfTrue nor the IfFalse nodes
        // have uses except for the Merge. Remove the branch if there
        // are no phis or replace phis with selects.
        Node* control = NodeProperties::GetControlInput(matcher.Branch());
        if (phis.size() == 0) {
          // No phis. Remove the branch altogether.
          TRACE("  DeadDiamond: #%d:Branch #%d:IfTrue #%d:IfFalse\n",
                matcher.Branch()->id(), matcher.IfTrue()->id(),
                matcher.IfFalse()->id());
          return control;
        } else {
          // A small number of phis. Replace with selects.
          Node* cond = matcher.Branch()->InputAt(0);
          for (Node* phi : phis) {
            Node* select = graph()->NewNode(
                common()->Select(OpParameter<MachineType>(phi),
                                 BranchHintOf(matcher.Branch()->op())),
                cond, matcher.TrueInputOf(phi), matcher.FalseInputOf(phi));
            TRACE("  MatchSelect: #%d:Branch #%d:IfTrue #%d:IfFalse -> #%d\n",
                  matcher.Branch()->id(), matcher.IfTrue()->id(),
                  matcher.IfFalse()->id(), select->id());
            Replace(phi, select);
          }
          return control;
        }
      }
    }

    return node;
  }

  bool CheckPhisForSelect(const NodeVector& phis) {
    if (phis.size() > static_cast<size_t>(max_phis_for_select_)) return false;
    for (Node* phi : phis) {
      if (phi->opcode() != IrOpcode::kPhi) return false;  // no EffectPhis.
    }
    return true;
  }

  // Reduce if projections if the branch has a constant input.
  Node* ReduceIfProjection(Node* node, Decision decision) {
    Node* branch = node->InputAt(0);
    DCHECK_EQ(IrOpcode::kBranch, branch->opcode());
    Decision result = DecideCondition(branch->InputAt(0));
    if (result == decision) {
      // Fold a branch by replacing IfTrue/IfFalse with the branch control.
      TRACE("  BranchReduce: #%d:%s => #%d:%s\n", branch->id(),
            branch->op()->mnemonic(), node->id(), node->op()->mnemonic());
      return branch->InputAt(1);
    }
    return result == kUnknown ? node : dead();
  }

  // Remove inputs to {node} corresponding to the dead inputs to {merge}
  // and compact the remaining inputs, updating the operator.
  void RemoveDeadInputs(Node* merge, Node* node) {
    int live = 0;
    for (int i = 0; i < merge->InputCount(); i++) {
      // skip dead inputs.
      if (merge->InputAt(i)->opcode() == IrOpcode::kDeadControl) continue;
      // compact live inputs.
      if (live != i) node->ReplaceInput(live, node->InputAt(i));
      live++;
    }
    // compact remaining inputs.
    int total = live;
    for (int i = merge->InputCount(); i < node->InputCount(); i++) {
      if (total != i) node->ReplaceInput(total, node->InputAt(i));
      total++;
    }
    DCHECK_EQ(total, live + node->InputCount() - merge->InputCount());
    DCHECK_NE(total, node->InputCount());
    node->TrimInputCount(total);
    node->set_op(common()->ResizeMergeOrPhi(node->op(), live));
  }
};


void ControlReducer::ReduceGraph(Zone* zone, JSGraph* jsgraph,
                                 int max_phis_for_select) {
  GraphReducer graph_reducer(zone, jsgraph->graph());
  CommonOperatorReducer common(&graph_reducer, jsgraph->graph(),
                               jsgraph->common(), jsgraph->machine());
  ControlReducerImpl impl(&graph_reducer, zone, jsgraph);
  impl.max_phis_for_select_ = max_phis_for_select;
  graph_reducer.AddReducer(&impl);
  graph_reducer.AddReducer(&common);
  graph_reducer.ReduceGraph();
}


namespace {

class DummyEditor final : public AdvancedReducer::Editor {
 public:
  void Replace(Node* node, Node* replacement) final {
    node->ReplaceUses(replacement);
  }
  void Revisit(Node* node) final {}
  void ReplaceWithValue(Node* node, Node* value, Node* effect,
                        Node* control) final {}
};

}  // namespace


Node* ControlReducer::ReduceMerge(JSGraph* jsgraph, Node* node,
                                  int max_phis_for_select) {
  Zone zone;
  DummyEditor editor;
  ControlReducerImpl impl(&editor, &zone, jsgraph);
  impl.max_phis_for_select_ = max_phis_for_select;
  return impl.ReduceMerge(node);
}


Node* ControlReducer::ReduceIfNodeForTesting(JSGraph* jsgraph, Node* node) {
  Zone zone;
  DummyEditor editor;
  ControlReducerImpl impl(&editor, &zone, jsgraph);
  switch (node->opcode()) {
    case IrOpcode::kIfTrue:
      return impl.ReduceIfProjection(node, kTrue);
    case IrOpcode::kIfFalse:
      return impl.ReduceIfProjection(node, kFalse);
    default:
      return node;
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
