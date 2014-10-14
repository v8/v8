// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <deque>
#include <queue>

#include "src/compiler/scheduler.h"

#include "src/compiler/graph.h"
#include "src/compiler/graph-inl.h"
#include "src/compiler/node.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node-properties-inl.h"
#include "src/data-flow.h"

namespace v8 {
namespace internal {
namespace compiler {

static inline void Trace(const char* msg, ...) {
  if (FLAG_trace_turbo_scheduler) {
    va_list arguments;
    va_start(arguments, msg);
    base::OS::VPrint(msg, arguments);
    va_end(arguments);
  }
}


Scheduler::Scheduler(Zone* zone, Graph* graph, Schedule* schedule)
    : zone_(zone),
      graph_(graph),
      schedule_(schedule),
      scheduled_nodes_(zone),
      schedule_root_nodes_(zone),
      node_data_(graph_->NodeCount(), DefaultSchedulerData(), zone),
      has_floating_control_(false) {}


Schedule* Scheduler::ComputeSchedule(Graph* graph) {
  Schedule* schedule;
  bool had_floating_control = false;
  do {
    Zone tmp_zone(graph->zone()->isolate());
    schedule = new (graph->zone())
        Schedule(graph->zone(), static_cast<size_t>(graph->NodeCount()));
    Scheduler scheduler(&tmp_zone, graph, schedule);

    scheduler.BuildCFG();
    Scheduler::ComputeSpecialRPO(schedule);
    scheduler.GenerateImmediateDominatorTree();

    scheduler.PrepareUses();
    scheduler.ScheduleEarly();
    scheduler.ScheduleLate();

    had_floating_control = scheduler.ConnectFloatingControl();
  } while (had_floating_control);

  return schedule;
}


Scheduler::SchedulerData Scheduler::DefaultSchedulerData() {
  SchedulerData def = {NULL, 0, false, false, kUnknown};
  return def;
}


Scheduler::Placement Scheduler::GetPlacement(Node* node) {
  SchedulerData* data = GetData(node);
  if (data->placement_ == kUnknown) {  // Compute placement, once, on demand.
    switch (node->opcode()) {
      case IrOpcode::kParameter:
        // Parameters are always fixed to the start node.
        data->placement_ = kFixed;
        break;
      case IrOpcode::kPhi:
      case IrOpcode::kEffectPhi: {
        // Phis and effect phis are fixed if their control inputs are.
        data->placement_ = GetPlacement(NodeProperties::GetControlInput(node));
        break;
      }
#define DEFINE_FLOATING_CONTROL_CASE(V) case IrOpcode::k##V:
        CONTROL_OP_LIST(DEFINE_FLOATING_CONTROL_CASE)
#undef DEFINE_FLOATING_CONTROL_CASE
        {
          // Control nodes that were not control-reachable from end may float.
          data->placement_ = kSchedulable;
          if (!data->is_connected_control_) {
            data->is_floating_control_ = true;
            has_floating_control_ = true;
            Trace("Floating control found: #%d:%s\n", node->id(),
                  node->op()->mnemonic());
          }
          break;
        }
      default:
        data->placement_ = kSchedulable;
        break;
    }
  }
  return data->placement_;
}


BasicBlock* Scheduler::GetCommonDominator(BasicBlock* b1, BasicBlock* b2) {
  while (b1 != b2) {
    int b1_rpo = GetRPONumber(b1);
    int b2_rpo = GetRPONumber(b2);
    DCHECK(b1_rpo != b2_rpo);
    if (b1_rpo < b2_rpo) {
      b2 = b2->dominator();
    } else {
      b1 = b1->dominator();
    }
  }
  return b1;
}


// -----------------------------------------------------------------------------
// Phase 1: Build control-flow graph and dominator tree.


// Internal class to build a control flow graph (i.e the basic blocks and edges
// between them within a Schedule) from the node graph.
// Visits the control edges of the graph backwards from end in order to find
// the connected control subgraph, needed for scheduling.
class CFGBuilder {
 public:
  Scheduler* scheduler_;
  Schedule* schedule_;
  ZoneQueue<Node*> queue_;
  NodeVector control_;

  CFGBuilder(Zone* zone, Scheduler* scheduler)
      : scheduler_(scheduler),
        schedule_(scheduler->schedule_),
        queue_(zone),
        control_(zone) {}

  // Run the control flow graph construction algorithm by walking the graph
  // backwards from end through control edges, building and connecting the
  // basic blocks for control nodes.
  void Run() {
    Graph* graph = scheduler_->graph_;
    FixNode(schedule_->start(), graph->start());
    Queue(graph->end());

    while (!queue_.empty()) {  // Breadth-first backwards traversal.
      Node* node = queue_.front();
      queue_.pop();
      int max = NodeProperties::PastControlIndex(node);
      for (int i = NodeProperties::FirstControlIndex(node); i < max; i++) {
        Queue(node->InputAt(i));
      }
    }

    for (NodeVector::iterator i = control_.begin(); i != control_.end(); ++i) {
      ConnectBlocks(*i);  // Connect block to its predecessor/successors.
    }

    FixNode(schedule_->end(), graph->end());
  }

  void FixNode(BasicBlock* block, Node* node) {
    schedule_->AddNode(block, node);
    scheduler_->GetData(node)->is_connected_control_ = true;
    scheduler_->GetData(node)->placement_ = Scheduler::kFixed;
  }

  void Queue(Node* node) {
    // Mark the connected control nodes as they queued.
    Scheduler::SchedulerData* data = scheduler_->GetData(node);
    if (!data->is_connected_control_) {
      BuildBlocks(node);
      queue_.push(node);
      control_.push_back(node);
      data->is_connected_control_ = true;
    }
  }

  void BuildBlocks(Node* node) {
    switch (node->opcode()) {
      case IrOpcode::kLoop:
      case IrOpcode::kMerge:
        BuildBlockForNode(node);
        break;
      case IrOpcode::kBranch:
        BuildBlocksForSuccessors(node, IrOpcode::kIfTrue, IrOpcode::kIfFalse);
        break;
      default:
        break;
    }
  }

  void ConnectBlocks(Node* node) {
    switch (node->opcode()) {
      case IrOpcode::kLoop:
      case IrOpcode::kMerge:
        ConnectMerge(node);
        break;
      case IrOpcode::kBranch:
        scheduler_->schedule_root_nodes_.push_back(node);
        ConnectBranch(node);
        break;
      case IrOpcode::kReturn:
        scheduler_->schedule_root_nodes_.push_back(node);
        ConnectReturn(node);
        break;
      default:
        break;
    }
  }

  void BuildBlockForNode(Node* node) {
    if (schedule_->block(node) == NULL) {
      BasicBlock* block = schedule_->NewBasicBlock();
      Trace("Create block B%d for #%d:%s\n", block->id().ToInt(), node->id(),
            node->op()->mnemonic());
      FixNode(block, node);
    }
  }

  void BuildBlocksForSuccessors(Node* node, IrOpcode::Value a,
                                IrOpcode::Value b) {
    Node* successors[2];
    CollectSuccessorProjections(node, successors, a, b);
    BuildBlockForNode(successors[0]);
    BuildBlockForNode(successors[1]);
  }

  // Collect the branch-related projections from a node, such as IfTrue,
  // IfFalse.
  // TODO(titzer): consider moving this to node.h
  void CollectSuccessorProjections(Node* node, Node** buffer,
                                   IrOpcode::Value true_opcode,
                                   IrOpcode::Value false_opcode) {
    buffer[0] = NULL;
    buffer[1] = NULL;
    for (UseIter i = node->uses().begin(); i != node->uses().end(); ++i) {
      if ((*i)->opcode() == true_opcode) {
        DCHECK_EQ(NULL, buffer[0]);
        buffer[0] = *i;
      }
      if ((*i)->opcode() == false_opcode) {
        DCHECK_EQ(NULL, buffer[1]);
        buffer[1] = *i;
      }
    }
    DCHECK_NE(NULL, buffer[0]);
    DCHECK_NE(NULL, buffer[1]);
  }

  void CollectSuccessorBlocks(Node* node, BasicBlock** buffer,
                              IrOpcode::Value true_opcode,
                              IrOpcode::Value false_opcode) {
    Node* successors[2];
    CollectSuccessorProjections(node, successors, true_opcode, false_opcode);
    buffer[0] = schedule_->block(successors[0]);
    buffer[1] = schedule_->block(successors[1]);
  }

  void ConnectBranch(Node* branch) {
    Node* branch_block_node = NodeProperties::GetControlInput(branch);
    BasicBlock* branch_block = schedule_->block(branch_block_node);
    DCHECK(branch_block != NULL);

    BasicBlock* successor_blocks[2];
    CollectSuccessorBlocks(branch, successor_blocks, IrOpcode::kIfTrue,
                           IrOpcode::kIfFalse);

    TraceConnect(branch, branch_block, successor_blocks[0]);
    TraceConnect(branch, branch_block, successor_blocks[1]);

    schedule_->AddBranch(branch_block, branch, successor_blocks[0],
                         successor_blocks[1]);
  }

  void ConnectMerge(Node* merge) {
    BasicBlock* block = schedule_->block(merge);
    DCHECK(block != NULL);
    // For all of the merge's control inputs, add a goto at the end to the
    // merge's basic block.
    for (InputIter j = merge->inputs().begin(); j != merge->inputs().end();
         ++j) {
      BasicBlock* predecessor_block = schedule_->block(*j);
      if ((*j)->opcode() != IrOpcode::kReturn) {
        TraceConnect(merge, predecessor_block, block);
        schedule_->AddGoto(predecessor_block, block);
      }
    }
  }

  void ConnectReturn(Node* ret) {
    Node* return_block_node = NodeProperties::GetControlInput(ret);
    BasicBlock* return_block = schedule_->block(return_block_node);
    TraceConnect(ret, return_block, NULL);
    schedule_->AddReturn(return_block, ret);
  }

  void TraceConnect(Node* node, BasicBlock* block, BasicBlock* succ) {
    DCHECK_NE(NULL, block);
    if (succ == NULL) {
      Trace("Connect #%d:%s, B%d -> end\n", node->id(), node->op()->mnemonic(),
            block->id().ToInt());
    } else {
      Trace("Connect #%d:%s, B%d -> B%d\n", node->id(), node->op()->mnemonic(),
            block->id().ToInt(), succ->id().ToInt());
    }
  }
};


void Scheduler::BuildCFG() {
  Trace("--- CREATING CFG -------------------------------------------\n");
  CFGBuilder cfg_builder(zone_, this);
  cfg_builder.Run();
  // Initialize per-block data.
  scheduled_nodes_.resize(schedule_->BasicBlockCount(), NodeVector(zone_));
}


void Scheduler::GenerateImmediateDominatorTree() {
  // Build the dominator graph.  TODO(danno): consider using Lengauer & Tarjan's
  // if this becomes really slow.
  Trace("--- IMMEDIATE BLOCK DOMINATORS -----------------------------\n");
  for (size_t i = 0; i < schedule_->rpo_order_.size(); i++) {
    BasicBlock* current_rpo = schedule_->rpo_order_[i];
    if (current_rpo != schedule_->start()) {
      BasicBlock::Predecessors::iterator current_pred =
          current_rpo->predecessors_begin();
      BasicBlock::Predecessors::iterator end = current_rpo->predecessors_end();
      DCHECK(current_pred != end);
      BasicBlock* dominator = *current_pred;
      ++current_pred;
      // For multiple predecessors, walk up the rpo ordering until a common
      // dominator is found.
      int current_rpo_pos = GetRPONumber(current_rpo);
      while (current_pred != end) {
        // Don't examine backwards edges
        BasicBlock* pred = *current_pred;
        if (GetRPONumber(pred) < current_rpo_pos) {
          dominator = GetCommonDominator(dominator, *current_pred);
        }
        ++current_pred;
      }
      current_rpo->set_dominator(dominator);
      Trace("Block %d's idom is %d\n", current_rpo->id().ToInt(),
            dominator->id().ToInt());
    }
  }
}


// -----------------------------------------------------------------------------
// Phase 2: Prepare use counts for nodes.


class PrepareUsesVisitor : public NullNodeVisitor {
 public:
  explicit PrepareUsesVisitor(Scheduler* scheduler)
      : scheduler_(scheduler), schedule_(scheduler->schedule_) {}

  GenericGraphVisit::Control Pre(Node* node) {
    if (scheduler_->GetPlacement(node) == Scheduler::kFixed) {
      // Fixed nodes are always roots for schedule late.
      scheduler_->schedule_root_nodes_.push_back(node);
      if (!schedule_->IsScheduled(node)) {
        // Make sure root nodes are scheduled in their respective blocks.
        Trace("  Scheduling fixed position node #%d:%s\n", node->id(),
              node->op()->mnemonic());
        IrOpcode::Value opcode = node->opcode();
        BasicBlock* block =
            opcode == IrOpcode::kParameter
                ? schedule_->start()
                : schedule_->block(NodeProperties::GetControlInput(node));
        DCHECK(block != NULL);
        schedule_->AddNode(block, node);
      }
    }

    return GenericGraphVisit::CONTINUE;
  }

  void PostEdge(Node* from, int index, Node* to) {
    // If the edge is from an unscheduled node, then tally it in the use count
    // for all of its inputs. The same criterion will be used in ScheduleLate
    // for decrementing use counts.
    if (!schedule_->IsScheduled(from)) {
      DCHECK_NE(Scheduler::kFixed, scheduler_->GetPlacement(from));
      ++(scheduler_->GetData(to)->unscheduled_count_);
      Trace("  Use count of #%d:%s (used by #%d:%s)++ = %d\n", to->id(),
            to->op()->mnemonic(), from->id(), from->op()->mnemonic(),
            scheduler_->GetData(to)->unscheduled_count_);
    }
  }

 private:
  Scheduler* scheduler_;
  Schedule* schedule_;
};


void Scheduler::PrepareUses() {
  Trace("--- PREPARE USES -------------------------------------------\n");

  // Count the uses of every node, it will be used to ensure that all of a
  // node's uses are scheduled before the node itself.
  PrepareUsesVisitor prepare_uses(this);
  graph_->VisitNodeInputsFromEnd(&prepare_uses);
}


// -----------------------------------------------------------------------------
// Phase 3: Schedule nodes early.


class ScheduleEarlyNodeVisitor : public NullNodeVisitor {
 public:
  explicit ScheduleEarlyNodeVisitor(Scheduler* scheduler)
      : scheduler_(scheduler), schedule_(scheduler->schedule_) {}

  GenericGraphVisit::Control Pre(Node* node) {
    Scheduler::SchedulerData* data = scheduler_->GetData(node);
    if (scheduler_->GetPlacement(node) == Scheduler::kFixed) {
      // Fixed nodes already know their schedule early position.
      if (data->minimum_block_ == NULL) {
        data->minimum_block_ = schedule_->block(node);
        Trace("Preschedule #%d:%s minimum_rpo = %d (fixed)\n", node->id(),
              node->op()->mnemonic(), data->minimum_block_->rpo_number());
      }
    } else {
      // For unfixed nodes the minimum RPO is the max of all of the inputs.
      if (data->minimum_block_ == NULL) {
        data->minimum_block_ = ComputeMaximumInputRPO(node);
        if (data->minimum_block_ == NULL) return GenericGraphVisit::REENTER;
        Trace("Preschedule #%d:%s minimum_rpo = %d\n", node->id(),
              node->op()->mnemonic(), data->minimum_block_->rpo_number());
      }
    }
    DCHECK_NE(data->minimum_block_, NULL);
    return GenericGraphVisit::CONTINUE;
  }

  GenericGraphVisit::Control Post(Node* node) {
    Scheduler::SchedulerData* data = scheduler_->GetData(node);
    if (scheduler_->GetPlacement(node) != Scheduler::kFixed) {
      // For unfixed nodes the minimum RPO is the max of all of the inputs.
      if (data->minimum_block_ == NULL) {
        data->minimum_block_ = ComputeMaximumInputRPO(node);
        Trace("Postschedule #%d:%s minimum_rpo = %d\n", node->id(),
              node->op()->mnemonic(), data->minimum_block_->rpo_number());
      }
    }
    DCHECK_NE(data->minimum_block_, NULL);
    return GenericGraphVisit::CONTINUE;
  }

  // Computes the maximum of the minimum RPOs for all inputs. If the maximum
  // cannot be determined (i.e. minimum RPO for at least one input is {NULL}),
  // then {NULL} is returned.
  BasicBlock* ComputeMaximumInputRPO(Node* node) {
    BasicBlock* max_block = schedule_->start();
    for (InputIter i = node->inputs().begin(); i != node->inputs().end(); ++i) {
      DCHECK_NE(node, *i);  // Loops only exist for fixed nodes.
      BasicBlock* block = scheduler_->GetData(*i)->minimum_block_;
      if (block == NULL) return NULL;
      if (block->rpo_number() > max_block->rpo_number()) {
        max_block = block;
      }
    }
    return max_block;
  }

 private:
  Scheduler* scheduler_;
  Schedule* schedule_;
};


void Scheduler::ScheduleEarly() {
  Trace("--- SCHEDULE EARLY -----------------------------------------\n");

  // Compute the minimum RPO for each node thereby determining the earliest
  // position each node could be placed within a valid schedule.
  ScheduleEarlyNodeVisitor visitor(this);
  graph_->VisitNodeInputsFromEnd(&visitor);
}


// -----------------------------------------------------------------------------
// Phase 4: Schedule nodes late.


class ScheduleLateNodeVisitor : public NullNodeVisitor {
 public:
  explicit ScheduleLateNodeVisitor(Scheduler* scheduler)
      : scheduler_(scheduler), schedule_(scheduler_->schedule_) {}

  GenericGraphVisit::Control Pre(Node* node) {
    // Don't schedule nodes that are already scheduled.
    if (schedule_->IsScheduled(node)) {
      return GenericGraphVisit::CONTINUE;
    }
    Scheduler::SchedulerData* data = scheduler_->GetData(node);
    DCHECK_EQ(Scheduler::kSchedulable, data->placement_);

    // If all the uses of a node have been scheduled, then the node itself can
    // be scheduled.
    bool eligible = data->unscheduled_count_ == 0;
    Trace("Testing for schedule eligibility for #%d:%s = %s\n", node->id(),
          node->op()->mnemonic(), eligible ? "true" : "false");
    if (!eligible) return GenericGraphVisit::DEFER;

    // Determine the dominating block for all of the uses of this node. It is
    // the latest block that this node can be scheduled in.
    BasicBlock* block = NULL;
    for (Node::Uses::iterator i = node->uses().begin(); i != node->uses().end();
         ++i) {
      BasicBlock* use_block = GetBlockForUse(i.edge());
      block = block == NULL ? use_block : use_block == NULL
                                              ? block
                                              : scheduler_->GetCommonDominator(
                                                    block, use_block);
    }
    DCHECK(block != NULL);

    int min_rpo = data->minimum_block_->rpo_number();
    Trace(
        "Schedule late conservative for #%d:%s is B%d at loop depth %d, "
        "minimum_rpo = %d\n",
        node->id(), node->op()->mnemonic(), block->id().ToInt(),
        block->loop_depth(), min_rpo);
    // Hoist nodes out of loops if possible. Nodes can be hoisted iteratively
    // into enclosing loop pre-headers until they would preceed their
    // ScheduleEarly position.
    BasicBlock* hoist_block = block;
    while (hoist_block != NULL && hoist_block->rpo_number() >= min_rpo) {
      if (hoist_block->loop_depth() < block->loop_depth()) {
        block = hoist_block;
        Trace("  hoisting #%d:%s to block %d\n", node->id(),
              node->op()->mnemonic(), block->id().ToInt());
      }
      // Try to hoist to the pre-header of the loop header.
      hoist_block = hoist_block->loop_header();
      if (hoist_block != NULL) {
        BasicBlock* pre_header = hoist_block->dominator();
        DCHECK(pre_header == NULL ||
               *hoist_block->predecessors_begin() == pre_header);
        Trace(
            "  hoist to pre-header B%d of loop header B%d, depth would be %d\n",
            pre_header->id().ToInt(), hoist_block->id().ToInt(),
            pre_header->loop_depth());
        hoist_block = pre_header;
      }
    }

    ScheduleNode(block, node);

    return GenericGraphVisit::CONTINUE;
  }

 private:
  BasicBlock* GetBlockForUse(Node::Edge edge) {
    Node* use = edge.from();
    IrOpcode::Value opcode = use->opcode();
    if (opcode == IrOpcode::kPhi || opcode == IrOpcode::kEffectPhi) {
      // If the use is from a fixed (i.e. non-floating) phi, use the block
      // of the corresponding control input to the merge.
      int index = edge.index();
      if (scheduler_->GetPlacement(use) == Scheduler::kFixed) {
        Trace("  input@%d into a fixed phi #%d:%s\n", index, use->id(),
              use->op()->mnemonic());
        Node* merge = NodeProperties::GetControlInput(use, 0);
        opcode = merge->opcode();
        DCHECK(opcode == IrOpcode::kMerge || opcode == IrOpcode::kLoop);
        use = NodeProperties::GetControlInput(merge, index);
      }
    }
    BasicBlock* result = schedule_->block(use);
    if (result == NULL) return NULL;
    Trace("  must dominate use #%d:%s in B%d\n", use->id(),
          use->op()->mnemonic(), result->id().ToInt());
    return result;
  }

  void ScheduleNode(BasicBlock* block, Node* node) {
    schedule_->PlanNode(block, node);
    scheduler_->scheduled_nodes_[block->id().ToSize()].push_back(node);

    // Reduce the use count of the node's inputs to potentially make them
    // schedulable.
    for (InputIter i = node->inputs().begin(); i != node->inputs().end(); ++i) {
      Scheduler::SchedulerData* data = scheduler_->GetData(*i);
      DCHECK(data->unscheduled_count_ > 0);
      --data->unscheduled_count_;
      if (FLAG_trace_turbo_scheduler) {
        Trace("  Use count for #%d:%s (used by #%d:%s)-- = %d\n", (*i)->id(),
              (*i)->op()->mnemonic(), i.edge().from()->id(),
              i.edge().from()->op()->mnemonic(), data->unscheduled_count_);
        if (data->unscheduled_count_ == 0) {
          Trace("  newly eligible #%d:%s\n", (*i)->id(),
                (*i)->op()->mnemonic());
        }
      }
    }
  }

  Scheduler* scheduler_;
  Schedule* schedule_;
};


void Scheduler::ScheduleLate() {
  Trace("--- SCHEDULE LATE ------------------------------------------\n");
  if (FLAG_trace_turbo_scheduler) {
    Trace("roots: ");
    for (NodeVectorIter i = schedule_root_nodes_.begin();
         i != schedule_root_nodes_.end(); ++i) {
      Trace("#%d:%s ", (*i)->id(), (*i)->op()->mnemonic());
    }
    Trace("\n");
  }

  // Schedule: Places nodes in dominator block of all their uses.
  ScheduleLateNodeVisitor schedule_late_visitor(this);

  {
    Zone zone(zone_->isolate());
    GenericGraphVisit::Visit<ScheduleLateNodeVisitor,
                             NodeInputIterationTraits<Node> >(
        graph_, &zone, schedule_root_nodes_.begin(), schedule_root_nodes_.end(),
        &schedule_late_visitor);
  }

  // Add collected nodes for basic blocks to their blocks in the right order.
  int block_num = 0;
  for (NodeVectorVectorIter i = scheduled_nodes_.begin();
       i != scheduled_nodes_.end(); ++i) {
    for (NodeVectorRIter j = i->rbegin(); j != i->rend(); ++j) {
      schedule_->AddNode(schedule_->all_blocks_.at(block_num), *j);
    }
    block_num++;
  }
}


// -----------------------------------------------------------------------------


bool Scheduler::ConnectFloatingControl() {
  if (!has_floating_control_) return false;

  Trace("Connecting floating control...\n");

  // Process blocks and instructions backwards to find and connect floating
  // control nodes into the control graph according to the block they were
  // scheduled into.
  int max = static_cast<int>(schedule_->rpo_order()->size());
  for (int i = max - 1; i >= 0; i--) {
    BasicBlock* block = schedule_->rpo_order()->at(i);
    // TODO(titzer): we place at most one floating control structure per
    // basic block because scheduling currently can interleave phis from
    // one subgraph with the merges from another subgraph.
    bool one_placed = false;
    for (size_t j = 0; j < block->NodeCount(); j++) {
      Node* node = block->NodeAt(block->NodeCount() - 1 - j);
      SchedulerData* data = GetData(node);
      if (data->is_floating_control_ && !data->is_connected_control_ &&
          !one_placed) {
        Trace("  Floating control #%d:%s was scheduled in B%d\n", node->id(),
              node->op()->mnemonic(), block->id().ToInt());
        ConnectFloatingControlSubgraph(block, node);
        one_placed = true;
      }
    }
  }

  return true;
}


void Scheduler::ConnectFloatingControlSubgraph(BasicBlock* block, Node* end) {
  Node* block_start = block->NodeAt(0);
  DCHECK(IrOpcode::IsControlOpcode(block_start->opcode()));
  // Find the current "control successor" of the node that starts the block
  // by searching the control uses for a control input edge from a connected
  // control node.
  Node* control_succ = NULL;
  for (UseIter i = block_start->uses().begin(); i != block_start->uses().end();
       ++i) {
    Node::Edge edge = i.edge();
    if (NodeProperties::IsControlEdge(edge) &&
        GetData(edge.from())->is_connected_control_) {
      DCHECK_EQ(NULL, control_succ);
      control_succ = edge.from();
      control_succ->ReplaceInput(edge.index(), end);
    }
  }
  DCHECK_NE(NULL, control_succ);
  Trace("  Inserting floating control end %d:%s between %d:%s -> %d:%s\n",
        end->id(), end->op()->mnemonic(), control_succ->id(),
        control_succ->op()->mnemonic(), block_start->id(),
        block_start->op()->mnemonic());

  // Find the "start" node of the control subgraph, which should be the
  // unique node that is itself floating control but has a control input that
  // is not floating.
  Node* start = NULL;
  ZoneQueue<Node*> queue(zone_);
  queue.push(end);
  GetData(end)->is_connected_control_ = true;
  while (!queue.empty()) {
    Node* node = queue.front();
    queue.pop();
    Trace("  Search #%d:%s for control subgraph start\n", node->id(),
          node->op()->mnemonic());
    int max = NodeProperties::PastControlIndex(node);
    for (int i = NodeProperties::FirstControlIndex(node); i < max; i++) {
      Node* input = node->InputAt(i);
      SchedulerData* data = GetData(input);
      if (data->is_floating_control_) {
        // {input} is floating control.
        if (!data->is_connected_control_) {
          // First time seeing {input} during this traversal, queue it.
          queue.push(input);
          data->is_connected_control_ = true;
        }
      } else {
        // Otherwise, {node} is the start node, because it is floating control
        // but is connected to {input} that is not floating control.
        DCHECK_EQ(NULL, start);  // There can be only one.
        start = node;
      }
    }
  }

  DCHECK_NE(NULL, start);
  start->ReplaceInput(NodeProperties::FirstControlIndex(start), block_start);

  Trace("  Connecting floating control start %d:%s to %d:%s\n", start->id(),
        start->op()->mnemonic(), block_start->id(),
        block_start->op()->mnemonic());
}


// Numbering for BasicBlockData.rpo_number_ for this block traversal:
static const int kBlockOnStack = -2;
static const int kBlockVisited1 = -3;
static const int kBlockVisited2 = -4;
static const int kBlockUnvisited1 = -1;
static const int kBlockUnvisited2 = kBlockVisited1;

struct SpecialRPOStackFrame {
  BasicBlock* block;
  size_t index;
};

struct BlockList {
  BasicBlock* block;
  BlockList* next;

  BlockList* Add(Zone* zone, BasicBlock* b) {
    BlockList* list = static_cast<BlockList*>(zone->New(sizeof(BlockList)));
    list->block = b;
    list->next = this;
    return list;
  }

  void Serialize(BasicBlockVector* final_order) {
    for (BlockList* l = this; l != NULL; l = l->next) {
      l->block->set_rpo_number(static_cast<int>(final_order->size()));
      final_order->push_back(l->block);
    }
  }
};

struct LoopInfo {
  BasicBlock* header;
  ZoneList<BasicBlock*>* outgoing;
  BitVector* members;
  LoopInfo* prev;
  BlockList* end;
  BlockList* start;

  void AddOutgoing(Zone* zone, BasicBlock* block) {
    if (outgoing == NULL) outgoing = new (zone) ZoneList<BasicBlock*>(2, zone);
    outgoing->Add(block, zone);
  }
};


static int Push(SpecialRPOStackFrame* stack, int depth, BasicBlock* child,
                int unvisited) {
  if (child->rpo_number() == unvisited) {
    stack[depth].block = child;
    stack[depth].index = 0;
    child->set_rpo_number(kBlockOnStack);
    return depth + 1;
  }
  return depth;
}


// Computes loop membership from the backedges of the control flow graph.
static LoopInfo* ComputeLoopInfo(
    Zone* zone, SpecialRPOStackFrame* queue, int num_loops, size_t num_blocks,
    ZoneList<std::pair<BasicBlock*, size_t> >* backedges) {
  LoopInfo* loops = zone->NewArray<LoopInfo>(num_loops);
  memset(loops, 0, num_loops * sizeof(LoopInfo));

  // Compute loop membership starting from backedges.
  // O(max(loop_depth) * max(|loop|)
  for (int i = 0; i < backedges->length(); i++) {
    BasicBlock* member = backedges->at(i).first;
    BasicBlock* header = member->SuccessorAt(backedges->at(i).second);
    int loop_num = header->loop_end();
    if (loops[loop_num].header == NULL) {
      loops[loop_num].header = header;
      loops[loop_num].members =
          new (zone) BitVector(static_cast<int>(num_blocks), zone);
    }

    int queue_length = 0;
    if (member != header) {
      // As long as the header doesn't have a backedge to itself,
      // Push the member onto the queue and process its predecessors.
      if (!loops[loop_num].members->Contains(member->id().ToInt())) {
        loops[loop_num].members->Add(member->id().ToInt());
      }
      queue[queue_length++].block = member;
    }

    // Propagate loop membership backwards. All predecessors of M up to the
    // loop header H are members of the loop too. O(|blocks between M and H|).
    while (queue_length > 0) {
      BasicBlock* block = queue[--queue_length].block;
      for (size_t i = 0; i < block->PredecessorCount(); i++) {
        BasicBlock* pred = block->PredecessorAt(i);
        if (pred != header) {
          if (!loops[loop_num].members->Contains(pred->id().ToInt())) {
            loops[loop_num].members->Add(pred->id().ToInt());
            queue[queue_length++].block = pred;
          }
        }
      }
    }
  }
  return loops;
}


#if DEBUG
static void PrintRPO(int num_loops, LoopInfo* loops, BasicBlockVector* order) {
  OFStream os(stdout);
  os << "-- RPO with " << num_loops << " loops ";
  if (num_loops > 0) {
    os << "(";
    for (int i = 0; i < num_loops; i++) {
      if (i > 0) os << " ";
      os << "B" << loops[i].header->id();
    }
    os << ") ";
  }
  os << "-- \n";

  for (size_t i = 0; i < order->size(); i++) {
    BasicBlock* block = (*order)[i];
    BasicBlock::Id bid = block->id();
    // TODO(jarin,svenpanne): Add formatting here once we have support for that
    // in streams (we want an equivalent of PrintF("%5d:", i) here).
    os << i << ":";
    for (int j = 0; j < num_loops; j++) {
      bool membership = loops[j].members->Contains(bid.ToInt());
      bool range = loops[j].header->LoopContains(block);
      os << (membership ? " |" : "  ");
      os << (range ? "x" : " ");
    }
    os << "  B" << bid << ": ";
    if (block->loop_end() >= 0) {
      os << " range: [" << block->rpo_number() << ", " << block->loop_end()
         << ")";
    }
    os << "\n";
  }
}


static void VerifySpecialRPO(int num_loops, LoopInfo* loops,
                             BasicBlockVector* order) {
  DCHECK(order->size() > 0);
  DCHECK((*order)[0]->id().ToInt() == 0);  // entry should be first.

  for (int i = 0; i < num_loops; i++) {
    LoopInfo* loop = &loops[i];
    BasicBlock* header = loop->header;

    DCHECK(header != NULL);
    DCHECK(header->rpo_number() >= 0);
    DCHECK(header->rpo_number() < static_cast<int>(order->size()));
    DCHECK(header->loop_end() >= 0);
    DCHECK(header->loop_end() <= static_cast<int>(order->size()));
    DCHECK(header->loop_end() > header->rpo_number());

    // Verify the start ... end list relationship.
    int links = 0;
    BlockList* l = loop->start;
    DCHECK(l != NULL && l->block == header);
    bool end_found;
    while (true) {
      if (l == NULL || l == loop->end) {
        end_found = (loop->end == l);
        break;
      }
      // The list should be in same order as the final result.
      DCHECK(l->block->rpo_number() == links + loop->header->rpo_number());
      links++;
      l = l->next;
      DCHECK(links < static_cast<int>(2 * order->size()));  // cycle?
    }
    DCHECK(links > 0);
    DCHECK(links == (header->loop_end() - header->rpo_number()));
    DCHECK(end_found);

    // Check the contiguousness of loops.
    int count = 0;
    for (int j = 0; j < static_cast<int>(order->size()); j++) {
      BasicBlock* block = order->at(j);
      DCHECK(block->rpo_number() == j);
      if (j < header->rpo_number() || j >= header->loop_end()) {
        DCHECK(!loop->members->Contains(block->id().ToInt()));
      } else {
        if (block == header) {
          DCHECK(!loop->members->Contains(block->id().ToInt()));
        } else {
          DCHECK(loop->members->Contains(block->id().ToInt()));
        }
        count++;
      }
    }
    DCHECK(links == count);
  }
}
#endif  // DEBUG


// Compute the special reverse-post-order block ordering, which is essentially
// a RPO of the graph where loop bodies are contiguous. Properties:
// 1. If block A is a predecessor of B, then A appears before B in the order,
//    unless B is a loop header and A is in the loop headed at B
//    (i.e. A -> B is a backedge).
// => If block A dominates block B, then A appears before B in the order.
// => If block A is a loop header, A appears before all blocks in the loop
//    headed at A.
// 2. All loops are contiguous in the order (i.e. no intervening blocks that
//    do not belong to the loop.)
// Note a simple RPO traversal satisfies (1) but not (3).
BasicBlockVector* Scheduler::ComputeSpecialRPO(Schedule* schedule) {
  Zone tmp_zone(schedule->zone()->isolate());
  Zone* zone = &tmp_zone;
  Trace("--- COMPUTING SPECIAL RPO ----------------------------------\n");
  // RPO should not have been computed for this schedule yet.
  CHECK_EQ(kBlockUnvisited1, schedule->start()->rpo_number());
  CHECK_EQ(0, static_cast<int>(schedule->rpo_order_.size()));

  // Perform an iterative RPO traversal using an explicit stack,
  // recording backedges that form cycles. O(|B|).
  ZoneList<std::pair<BasicBlock*, size_t> > backedges(1, zone);
  SpecialRPOStackFrame* stack = zone->NewArray<SpecialRPOStackFrame>(
      static_cast<int>(schedule->BasicBlockCount()));
  BasicBlock* entry = schedule->start();
  BlockList* order = NULL;
  int stack_depth = Push(stack, 0, entry, kBlockUnvisited1);
  int num_loops = 0;

  while (stack_depth > 0) {
    int current = stack_depth - 1;
    SpecialRPOStackFrame* frame = stack + current;

    if (frame->index < frame->block->SuccessorCount()) {
      // Process the next successor.
      BasicBlock* succ = frame->block->SuccessorAt(frame->index++);
      if (succ->rpo_number() == kBlockVisited1) continue;
      if (succ->rpo_number() == kBlockOnStack) {
        // The successor is on the stack, so this is a backedge (cycle).
        backedges.Add(
            std::pair<BasicBlock*, size_t>(frame->block, frame->index - 1),
            zone);
        if (succ->loop_end() < 0) {
          // Assign a new loop number to the header if it doesn't have one.
          succ->set_loop_end(num_loops++);
        }
      } else {
        // Push the successor onto the stack.
        DCHECK(succ->rpo_number() == kBlockUnvisited1);
        stack_depth = Push(stack, stack_depth, succ, kBlockUnvisited1);
      }
    } else {
      // Finished with all successors; pop the stack and add the block.
      order = order->Add(zone, frame->block);
      frame->block->set_rpo_number(kBlockVisited1);
      stack_depth--;
    }
  }

  // If no loops were encountered, then the order we computed was correct.
  LoopInfo* loops = NULL;
  if (num_loops != 0) {
    // Otherwise, compute the loop information from the backedges in order
    // to perform a traversal that groups loop bodies together.
    loops = ComputeLoopInfo(zone, stack, num_loops, schedule->BasicBlockCount(),
                            &backedges);

    // Initialize the "loop stack". Note the entry could be a loop header.
    LoopInfo* loop = entry->IsLoopHeader() ? &loops[entry->loop_end()] : NULL;
    order = NULL;

    // Perform an iterative post-order traversal, visiting loop bodies before
    // edges that lead out of loops. Visits each block once, but linking loop
    // sections together is linear in the loop size, so overall is
    // O(|B| + max(loop_depth) * max(|loop|))
    stack_depth = Push(stack, 0, entry, kBlockUnvisited2);
    while (stack_depth > 0) {
      SpecialRPOStackFrame* frame = stack + (stack_depth - 1);
      BasicBlock* block = frame->block;
      BasicBlock* succ = NULL;

      if (frame->index < block->SuccessorCount()) {
        // Process the next normal successor.
        succ = block->SuccessorAt(frame->index++);
      } else if (block->IsLoopHeader()) {
        // Process additional outgoing edges from the loop header.
        if (block->rpo_number() == kBlockOnStack) {
          // Finish the loop body the first time the header is left on the
          // stack.
          DCHECK(loop != NULL && loop->header == block);
          loop->start = order->Add(zone, block);
          order = loop->end;
          block->set_rpo_number(kBlockVisited2);
          // Pop the loop stack and continue visiting outgoing edges within the
          // the context of the outer loop, if any.
          loop = loop->prev;
          // We leave the loop header on the stack; the rest of this iteration
          // and later iterations will go through its outgoing edges list.
        }

        // Use the next outgoing edge if there are any.
        int outgoing_index =
            static_cast<int>(frame->index - block->SuccessorCount());
        LoopInfo* info = &loops[block->loop_end()];
        DCHECK(loop != info);
        if (info->outgoing != NULL &&
            outgoing_index < info->outgoing->length()) {
          succ = info->outgoing->at(outgoing_index);
          frame->index++;
        }
      }

      if (succ != NULL) {
        // Process the next successor.
        if (succ->rpo_number() == kBlockOnStack) continue;
        if (succ->rpo_number() == kBlockVisited2) continue;
        DCHECK(succ->rpo_number() == kBlockUnvisited2);
        if (loop != NULL && !loop->members->Contains(succ->id().ToInt())) {
          // The successor is not in the current loop or any nested loop.
          // Add it to the outgoing edges of this loop and visit it later.
          loop->AddOutgoing(zone, succ);
        } else {
          // Push the successor onto the stack.
          stack_depth = Push(stack, stack_depth, succ, kBlockUnvisited2);
          if (succ->IsLoopHeader()) {
            // Push the inner loop onto the loop stack.
            DCHECK(succ->loop_end() >= 0 && succ->loop_end() < num_loops);
            LoopInfo* next = &loops[succ->loop_end()];
            next->end = order;
            next->prev = loop;
            loop = next;
          }
        }
      } else {
        // Finished with all successors of the current block.
        if (block->IsLoopHeader()) {
          // If we are going to pop a loop header, then add its entire body.
          LoopInfo* info = &loops[block->loop_end()];
          for (BlockList* l = info->start; true; l = l->next) {
            if (l->next == info->end) {
              l->next = order;
              info->end = order;
              break;
            }
          }
          order = info->start;
        } else {
          // Pop a single node off the stack and add it to the order.
          order = order->Add(zone, block);
          block->set_rpo_number(kBlockVisited2);
        }
        stack_depth--;
      }
    }
  }

  // Construct the final order from the list.
  BasicBlockVector* final_order = &schedule->rpo_order_;
  order->Serialize(final_order);

  // Compute the correct loop header for every block and set the correct loop
  // ends.
  LoopInfo* current_loop = NULL;
  BasicBlock* current_header = NULL;
  int loop_depth = 0;
  for (BasicBlockVectorIter i = final_order->begin(); i != final_order->end();
       ++i) {
    BasicBlock* current = *i;
    current->set_loop_header(current_header);
    if (current->IsLoopHeader()) {
      loop_depth++;
      current_loop = &loops[current->loop_end()];
      BlockList* end = current_loop->end;
      current->set_loop_end(end == NULL ? static_cast<int>(final_order->size())
                                        : end->block->rpo_number());
      current_header = current_loop->header;
      Trace("B%d is a loop header, increment loop depth to %d\n",
            current->id().ToInt(), loop_depth);
    } else {
      while (current_header != NULL &&
             current->rpo_number() >= current_header->loop_end()) {
        DCHECK(current_header->IsLoopHeader());
        DCHECK(current_loop != NULL);
        current_loop = current_loop->prev;
        current_header = current_loop == NULL ? NULL : current_loop->header;
        --loop_depth;
      }
    }
    current->set_loop_depth(loop_depth);
    if (current->loop_header() == NULL) {
      Trace("B%d is not in a loop (depth == %d)\n", current->id().ToInt(),
            current->loop_depth());
    } else {
      Trace("B%d has loop header B%d, (depth == %d)\n", current->id().ToInt(),
            current->loop_header()->id().ToInt(), current->loop_depth());
    }
  }

#if DEBUG
  if (FLAG_trace_turbo_scheduler) PrintRPO(num_loops, loops, final_order);
  VerifySpecialRPO(num_loops, loops, final_order);
#endif
  return final_order;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
