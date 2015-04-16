// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/common-operator.h"
#include "src/compiler/control-reducer.h"
#include "src/compiler/graph.h"
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

enum VisitState { kUnvisited = 0, kOnStack = 1, kRevisit = 2, kVisited = 3 };
enum Decision { kFalse, kUnknown, kTrue };

class ReachabilityMarker : public NodeMarker<uint8_t> {
 public:
  explicit ReachabilityMarker(Graph* graph) : NodeMarker<uint8_t>(graph, 8) {}
  bool SetReachableFromEnd(Node* node) {
    uint8_t before = Get(node);
    Set(node, before | kFromEnd);
    return before & kFromEnd;
  }
  bool IsReachableFromEnd(Node* node) { return Get(node) & kFromEnd; }
  bool SetReachableFromStart(Node* node) {
    uint8_t before = Get(node);
    Set(node, before | kFromStart);
    return before & kFromStart;
  }
  bool IsReachableFromStart(Node* node) { return Get(node) & kFromStart; }
  void Push(Node* node) { Set(node, Get(node) | kFwStack); }
  void Pop(Node* node) { Set(node, Get(node) & ~kFwStack); }
  bool IsOnStack(Node* node) { return Get(node) & kFwStack; }

 private:
  enum Bit { kFromEnd = 1, kFromStart = 2, kFwStack = 4 };
};


class ControlReducerImpl {
 public:
  ControlReducerImpl(Zone* zone, JSGraph* jsgraph,
                     CommonOperatorBuilder* common)
      : zone_(zone),
        jsgraph_(jsgraph),
        common_(common),
        state_(jsgraph->graph()->NodeCount(), kUnvisited, zone_),
        stack_(zone_),
        revisit_(zone_),
        max_phis_for_select_(0) {}

  Zone* zone_;
  JSGraph* jsgraph_;
  CommonOperatorBuilder* common_;
  ZoneVector<VisitState> state_;
  ZoneDeque<Node*> stack_;
  ZoneDeque<Node*> revisit_;
  int max_phis_for_select_;

  void Reduce() {
    Push(graph()->end());
    do {
      // Process the node on the top of the stack, potentially pushing more
      // or popping the node off the stack.
      ReduceTop();
      // If the stack becomes empty, revisit any nodes in the revisit queue.
      // If no nodes in the revisit queue, try removing dead loops.
      // If no dead loops, then finish.
    } while (!stack_.empty() || TryRevisit() || RepairAndRemoveLoops());
  }

  bool TryRevisit() {
    while (!revisit_.empty()) {
      Node* n = revisit_.back();
      revisit_.pop_back();
      if (state_[n->id()] == kRevisit) {  // state can change while in queue.
        Push(n);
        return true;
      }
    }
    return false;
  }

  // Repair the graph after the possible creation of non-terminating or dead
  // loops. Removing dead loops can produce more opportunities for reduction.
  bool RepairAndRemoveLoops() {
    // TODO(turbofan): we can skip this if the graph has no loops, but
    // we have to be careful about proper loop detection during reduction.

    // Gather all nodes backwards-reachable from end (through inputs).
    ReachabilityMarker marked(graph());
    NodeVector nodes(zone_);
    AddNodesReachableFromRoots(marked, nodes);

    // Walk forward through control nodes, looking for back edges to nodes
    // that are not connected to end. Those are non-terminating loops (NTLs).
    Node* start = graph()->start();
    marked.Push(start);
    marked.SetReachableFromStart(start);

    // We use a stack of (Node, Node::UseEdges::iterator) pairs to avoid
    // O(n^2) traversal.
    typedef std::pair<Node*, Node::UseEdges::iterator> FwIter;
    ZoneVector<FwIter> fw_stack(zone_);
    fw_stack.push_back(FwIter(start, start->use_edges().begin()));

    while (!fw_stack.empty()) {
      Node* node = fw_stack.back().first;
      TRACE("ControlFw: #%d:%s\n", node->id(), node->op()->mnemonic());
      bool pop = true;
      while (fw_stack.back().second != node->use_edges().end()) {
        Edge edge = *(fw_stack.back().second);
        if (NodeProperties::IsControlEdge(edge) &&
            edge.from()->op()->ControlOutputCount() > 0) {
          // Only walk control edges to control nodes.
          Node* succ = edge.from();

          if (marked.IsOnStack(succ) && !marked.IsReachableFromEnd(succ)) {
            // {succ} is on stack and not reachable from end.
            Node* added = ConnectNTL(succ);
            nodes.push_back(added);
            marked.SetReachableFromEnd(added);
            AddBackwardsReachableNodes(marked, nodes, nodes.size() - 1);

            // Reset the use iterators for the entire stack.
            for (size_t i = 0; i < fw_stack.size(); i++) {
              FwIter& iter = fw_stack[i];
              fw_stack[i] = FwIter(iter.first, iter.first->use_edges().begin());
            }
            pop = false;  // restart traversing successors of this node.
            break;
          }
          if (!marked.IsReachableFromStart(succ)) {
            // {succ} is not yet reached from start.
            marked.Push(succ);
            marked.SetReachableFromStart(succ);
            fw_stack.push_back(FwIter(succ, succ->use_edges().begin()));
            pop = false;  // "recurse" into successor control node.
            break;
          }
        }
        ++fw_stack.back().second;
      }
      if (pop) {
        marked.Pop(node);
        fw_stack.pop_back();
      }
    }

    // Trim references from dead nodes to live nodes first.
    TrimNodes(marked, nodes);

    // Any control nodes not reachable from start are dead, even loops.
    for (size_t i = 0; i < nodes.size(); i++) {
      Node* node = nodes[i];
      if (node->op()->ControlOutputCount() > 0 &&
          !marked.IsReachableFromStart(node)) {
        ReplaceNode(node, dead());  // uses will be added to revisit queue.
      }
    }
    return TryRevisit();  // try to push a node onto the stack.
  }

  // Connect {loop}, the header of a non-terminating loop, to the end node.
  Node* ConnectNTL(Node* loop) {
    TRACE("ConnectNTL: #%d:%s\n", loop->id(), loop->op()->mnemonic());
    DCHECK_EQ(IrOpcode::kLoop, loop->opcode());

    Node* always = graph()->NewNode(common_->Always());
    // Mark the node as visited so that we can revisit later.
    MarkAsVisited(always);

    Node* branch = graph()->NewNode(common_->Branch(), always, loop);
    // Mark the node as visited so that we can revisit later.
    MarkAsVisited(branch);

    Node* if_true = graph()->NewNode(common_->IfTrue(), branch);
    // Mark the node as visited so that we can revisit later.
    MarkAsVisited(if_true);

    Node* if_false = graph()->NewNode(common_->IfFalse(), branch);
    // Mark the node as visited so that we can revisit later.
    MarkAsVisited(if_false);

    // Hook up the branch into the loop and collect all loop effects.
    NodeVector effects(zone_);
    for (auto edge : loop->use_edges()) {
      DCHECK_EQ(loop, edge.to());
      DCHECK(NodeProperties::IsControlEdge(edge));
      if (edge.from() == branch) continue;
      switch (edge.from()->opcode()) {
        case IrOpcode::kPhi:
          break;
        case IrOpcode::kEffectPhi:
          effects.push_back(edge.from());
          break;
        default:
          // Update all control edges (except {branch}) pointing to the {loop}.
          edge.UpdateTo(if_true);
          break;
      }
    }

    // Compute effects for the Return.
    Node* effect = graph()->start();
    int const effects_count = static_cast<int>(effects.size());
    if (effects_count == 1) {
      effect = effects[0];
    } else if (effects_count > 1) {
      effect = graph()->NewNode(common_->EffectSet(effects_count),
                                effects_count, &effects.front());
      // Mark the node as visited so that we can revisit later.
      MarkAsVisited(effect);
    }

    // Add a return to connect the NTL to the end.
    Node* ret = graph()->NewNode(
        common_->Return(), jsgraph_->UndefinedConstant(), effect, if_false);
    // Mark the node as visited so that we can revisit later.
    MarkAsVisited(ret);

    Node* end = graph()->end();
    CHECK_EQ(IrOpcode::kEnd, end->opcode());
    Node* merge = end->InputAt(0);
    if (merge == NULL || merge->opcode() == IrOpcode::kDead) {
      // The end node died; just connect end to {ret}.
      end->ReplaceInput(0, ret);
    } else if (merge->opcode() != IrOpcode::kMerge) {
      // Introduce a final merge node for {end->InputAt(0)} and {ret}.
      merge = graph()->NewNode(common_->Merge(2), merge, ret);
      end->ReplaceInput(0, merge);
      ret = merge;
      // Mark the node as visited so that we can revisit later.
      MarkAsVisited(merge);
    } else {
      // Append a new input to the final merge at the end.
      merge->AppendInput(graph()->zone(), ret);
      merge->set_op(common_->Merge(merge->InputCount()));
    }
    return ret;
  }

  void AddNodesReachableFromRoots(ReachabilityMarker& marked,
                                  NodeVector& nodes) {
    jsgraph_->GetCachedNodes(&nodes);  // Consider cached nodes roots.
    Node* end = graph()->end();
    marked.SetReachableFromEnd(end);
    if (!end->IsDead()) nodes.push_back(end);  // Consider end to be a root.
    for (Node* node : nodes) marked.SetReachableFromEnd(node);
    AddBackwardsReachableNodes(marked, nodes, 0);
  }

  void AddBackwardsReachableNodes(ReachabilityMarker& marked, NodeVector& nodes,
                                  size_t cursor) {
    while (cursor < nodes.size()) {
      Node* node = nodes[cursor++];
      for (Node* const input : node->inputs()) {
        if (!marked.SetReachableFromEnd(input)) {
          nodes.push_back(input);
        }
      }
    }
  }

  void Trim() {
    // Gather all nodes backwards-reachable from end through inputs.
    ReachabilityMarker marked(graph());
    NodeVector nodes(zone_);
    jsgraph_->GetCachedNodes(&nodes);
    AddNodesReachableFromRoots(marked, nodes);
    TrimNodes(marked, nodes);
  }

  void TrimNodes(ReachabilityMarker& marked, NodeVector& nodes) {
    // Remove dead->live edges.
    for (size_t j = 0; j < nodes.size(); j++) {
      Node* node = nodes[j];
      for (Edge edge : node->use_edges()) {
        Node* use = edge.from();
        if (!marked.IsReachableFromEnd(use)) {
          TRACE("DeadLink: #%d:%s(%d) -> #%d:%s\n", use->id(),
                use->op()->mnemonic(), edge.index(), node->id(),
                node->op()->mnemonic());
          edge.UpdateTo(NULL);
        }
      }
    }
#if DEBUG
    // Verify that no inputs to live nodes are NULL.
    for (Node* node : nodes) {
      for (int index = 0; index < node->InputCount(); index++) {
        Node* input = node->InputAt(index);
        if (input == nullptr) {
          std::ostringstream str;
          str << "GraphError: node #" << node->id() << ":" << *node->op()
              << "(input @" << index << ") == null";
          FATAL(str.str().c_str());
        }
        if (input->opcode() == IrOpcode::kDead) {
          std::ostringstream str;
          str << "GraphError: node #" << node->id() << ":" << *node->op()
              << "(input @" << index << ") == dead";
          FATAL(str.str().c_str());
        }
      }
      for (Node* use : node->uses()) {
        CHECK(marked.IsReachableFromEnd(use));
      }
    }
#endif
  }

  // Reduce the node on the top of the stack.
  // If an input {i} is not yet visited or needs to be revisited, push {i} onto
  // the stack and return. Otherwise, all inputs are visited, so apply
  // reductions for {node} and pop it off the stack.
  void ReduceTop() {
    size_t height = stack_.size();
    Node* node = stack_.back();

    if (node->IsDead()) return Pop();  // Node was killed while on stack.

    TRACE("ControlReduce: #%d:%s\n", node->id(), node->op()->mnemonic());

    // Recurse on an input if necessary.
    for (Node* const input : node->inputs()) {
      DCHECK(input);
      if (Recurse(input)) return;
    }

    // All inputs should be visited or on stack. Apply reductions to node.
    Node* replacement = ReduceNode(node);
    if (replacement != node) ReplaceNode(node, replacement);

    // After reducing the node, pop it off the stack.
    CHECK_EQ(static_cast<int>(height), static_cast<int>(stack_.size()));
    Pop();

    // If there was a replacement, reduce it after popping {node}.
    if (replacement != node) Recurse(replacement);
  }

  void EnsureStateSize(size_t id) {
    if (id >= state_.size()) {
      state_.resize((3 * id) / 2, kUnvisited);
    }
  }

  // Push a node onto the stack if its state is {kUnvisited} or {kRevisit}.
  bool Recurse(Node* node) {
    size_t id = static_cast<size_t>(node->id());
    EnsureStateSize(id);
    if (state_[id] != kRevisit && state_[id] != kUnvisited) return false;
    Push(node);
    return true;
  }

  void Push(Node* node) {
    state_[node->id()] = kOnStack;
    stack_.push_back(node);
  }

  void Pop() {
    int pos = static_cast<int>(stack_.size()) - 1;
    DCHECK_GE(pos, 0);
    DCHECK_EQ(kOnStack, state_[stack_[pos]->id()]);
    state_[stack_[pos]->id()] = kVisited;
    stack_.pop_back();
  }

  // Queue a node to be revisited if it has been visited once already.
  void Revisit(Node* node) {
    size_t id = static_cast<size_t>(node->id());
    if (id < state_.size() && state_[id] == kVisited) {
      TRACE("  Revisit #%d:%s\n", node->id(), node->op()->mnemonic());
      state_[id] = kRevisit;
      revisit_.push_back(node);
    }
  }

  // Mark {node} as visited.
  void MarkAsVisited(Node* node) {
    size_t id = static_cast<size_t>(node->id());
    EnsureStateSize(id);
    state_[id] = kVisited;
  }

  Node* dead() { return jsgraph_->DeadControl(); }

  //===========================================================================
  // Reducer implementation: perform reductions on a node.
  //===========================================================================
  Node* ReduceNode(Node* node) {
    if (node->op()->ControlInputCount() == 1 ||
        node->opcode() == IrOpcode::kLoop) {
      // If a node has only one control input and it is dead, replace with dead.
      Node* control = NodeProperties::GetControlInput(node);
      if (control->opcode() == IrOpcode::kDead) {
        TRACE("ControlDead: #%d:%s\n", node->id(), node->op()->mnemonic());
        return control;
      }
    }

    // Reduce branches, phis, and merges.
    switch (node->opcode()) {
      case IrOpcode::kBranch:
        return ReduceBranch(node);
      case IrOpcode::kIfTrue:
        return ReduceIfProjection(node, kTrue);
      case IrOpcode::kIfFalse:
        return ReduceIfProjection(node, kFalse);
      case IrOpcode::kLoop:
      case IrOpcode::kMerge:
        return ReduceMerge(node);
      case IrOpcode::kSelect:
        return ReduceSelect(node);
      case IrOpcode::kPhi:
      case IrOpcode::kEffectPhi:
        return ReducePhi(node);
      default:
        return node;
    }
  }

  // Try to statically fold a condition.
  Decision DecideCondition(Node* cond, bool recurse = true) {
    switch (cond->opcode()) {
      case IrOpcode::kInt32Constant:
        return Int32Matcher(cond).Is(0) ? kFalse : kTrue;
      case IrOpcode::kInt64Constant:
        return Int64Matcher(cond).Is(0) ? kFalse : kTrue;
      case IrOpcode::kNumberConstant:
        return NumberMatcher(cond).Is(0) ? kFalse : kTrue;
      case IrOpcode::kHeapConstant: {
        Handle<Object> object =
            HeapObjectMatcher<Object>(cond).Value().handle();
        return object->BooleanValue() ? kTrue : kFalse;
      }
      case IrOpcode::kPhi: {
        if (!recurse) return kUnknown;  // Only go one level deep checking phis.
        Decision result = kUnknown;
        // Check if all inputs to a phi result in the same decision.
        for (int i = cond->op()->ValueInputCount() - 1; i >= 0; i--) {
          // Recurse only one level, since phis can be involved in cycles.
          Decision decision = DecideCondition(cond->InputAt(i), false);
          if (decision == kUnknown) return kUnknown;
          if (result == kUnknown) result = decision;
          if (result != decision) return kUnknown;
        }
        return result;
      }
      default:
        break;
    }
    if (NodeProperties::IsTyped(cond)) {
      // If the node has a range type, check whether the range excludes 0.
      Type* type = NodeProperties::GetBounds(cond).upper;
      if (type->IsRange() && (type->Min() > 0 || type->Max() < 0)) return kTrue;
    }
    return kUnknown;
  }

  // Reduce redundant selects.
  Node* ReduceSelect(Node* const node) {
    Node* const tvalue = node->InputAt(1);
    Node* const fvalue = node->InputAt(2);
    if (tvalue == fvalue) return tvalue;
    Decision result = DecideCondition(node->InputAt(0));
    if (result == kTrue) return tvalue;
    if (result == kFalse) return fvalue;
    return node;
  }

  // Reduce redundant phis.
  Node* ReducePhi(Node* node) {
    int n = node->InputCount();
    if (n <= 1) return dead();            // No non-control inputs.
    if (n == 2) return node->InputAt(0);  // Only one non-control input.

    // Never remove an effect phi from a (potentially non-terminating) loop.
    // Otherwise, we might end up eliminating effect nodes, such as calls,
    // before the loop.
    if (node->opcode() == IrOpcode::kEffectPhi &&
        NodeProperties::GetControlInput(node)->opcode() == IrOpcode::kLoop) {
      return node;
    }

    Node* replacement = NULL;
    auto const inputs = node->inputs();
    for (auto it = inputs.begin(); n > 1; --n, ++it) {
      Node* input = *it;
      if (input->opcode() == IrOpcode::kDead) continue;  // ignore dead inputs.
      if (input != node && input != replacement) {       // non-redundant input.
        if (replacement != NULL) return node;
        replacement = input;
      }
    }
    return replacement == NULL ? dead() : replacement;
  }

  // Reduce branches.
  Node* ReduceBranch(Node* branch) {
    if (DecideCondition(branch->InputAt(0)) != kUnknown) {
      for (Node* use : branch->uses()) Revisit(use);
    }
    return branch;
  }

  // Reduce merges by trimming away dead inputs from the merge and phis.
  Node* ReduceMerge(Node* node) {
    // Count the number of live inputs.
    int live = 0;
    int index = 0;
    int live_index = 0;
    for (Node* const input : node->inputs()) {
      if (input->opcode() != IrOpcode::kDead) {
        live++;
        live_index = index;
      }
      index++;
    }

    TRACE("ReduceMerge: #%d:%s (%d of %d live)\n", node->id(),
          node->op()->mnemonic(), live, index);

    if (live == 0) return dead();  // no remaining inputs.

    // Gather phis and effect phis to be edited.
    NodeVector phis(zone_);
    for (Node* const use : node->uses()) {
      if (NodeProperties::IsPhi(use)) phis.push_back(use);
    }

    if (live == 1) {
      // All phis are redundant. Replace them with their live input.
      for (Node* const phi : phis) ReplaceNode(phi, phi->InputAt(live_index));
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
                common_->Select(OpParameter<MachineType>(phi),
                                BranchHintOf(matcher.Branch()->op())),
                cond, matcher.TrueInputOf(phi), matcher.FalseInputOf(phi));
            TRACE("  MatchSelect: #%d:Branch #%d:IfTrue #%d:IfFalse -> #%d\n",
                  matcher.Branch()->id(), matcher.IfTrue()->id(),
                  matcher.IfFalse()->id(), select->id());
            ReplaceNode(phi, select);
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
      if (merge->InputAt(i)->opcode() == IrOpcode::kDead) continue;
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
    node->set_op(common_->ResizeMergeOrPhi(node->op(), live));
  }

  // Replace uses of {node} with {replacement} and revisit the uses.
  void ReplaceNode(Node* node, Node* replacement) {
    if (node == replacement) return;
    TRACE("  Replace: #%d:%s with #%d:%s\n", node->id(), node->op()->mnemonic(),
          replacement->id(), replacement->op()->mnemonic());
    for (Node* const use : node->uses()) {
      // Don't revisit this node if it refers to itself.
      if (use != node) Revisit(use);
    }
    node->ReplaceUses(replacement);
    node->Kill();
  }

  Graph* graph() { return jsgraph_->graph(); }
};


void ControlReducer::ReduceGraph(Zone* zone, JSGraph* jsgraph,
                                 CommonOperatorBuilder* common,
                                 int max_phis_for_select) {
  ControlReducerImpl impl(zone, jsgraph, common);
  impl.max_phis_for_select_ = max_phis_for_select;
  impl.Reduce();
}


void ControlReducer::TrimGraph(Zone* zone, JSGraph* jsgraph) {
  ControlReducerImpl impl(zone, jsgraph, NULL);
  impl.Trim();
}


Node* ControlReducer::ReduceMerge(JSGraph* jsgraph,
                                  CommonOperatorBuilder* common, Node* node,
                                  int max_phis_for_select) {
  Zone zone;
  ControlReducerImpl impl(&zone, jsgraph, common);
  impl.max_phis_for_select_ = max_phis_for_select;
  return impl.ReduceMerge(node);
}


Node* ControlReducer::ReducePhiForTesting(JSGraph* jsgraph,
                                          CommonOperatorBuilder* common,
                                          Node* node) {
  Zone zone;
  ControlReducerImpl impl(&zone, jsgraph, common);
  return impl.ReducePhi(node);
}


Node* ControlReducer::ReduceIfNodeForTesting(JSGraph* jsgraph,
                                             CommonOperatorBuilder* common,
                                             Node* node) {
  Zone zone;
  ControlReducerImpl impl(&zone, jsgraph, common);
  switch (node->opcode()) {
    case IrOpcode::kIfTrue:
      return impl.ReduceIfProjection(node, kTrue);
    case IrOpcode::kIfFalse:
      return impl.ReduceIfProjection(node, kFalse);
    default:
      return node;
  }
}
}
}
}  // namespace v8::internal::compiler
