// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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


// Internal class to build a control flow graph (i.e the basic blocks and edges
// between them within a Schedule) from the node graph.
// Visits the graph backwards from end, following control and data edges.
class CFGBuilder : public NullNodeVisitor {
 public:
  Schedule* schedule_;

  explicit CFGBuilder(Schedule* schedule) : schedule_(schedule) {}

  // Create the blocks for the schedule in pre-order.
  void PreEdge(Node* from, int index, Node* node) {
    switch (node->opcode()) {
      case IrOpcode::kLoop:
      case IrOpcode::kMerge:
        BuildBlockForNode(node);
        break;
      case IrOpcode::kBranch:
        BuildBlocksForSuccessors(node, IrOpcode::kIfTrue, IrOpcode::kIfFalse);
        break;
      case IrOpcode::kCall:
        if (OperatorProperties::CanLazilyDeoptimize(node->op())) {
          BuildBlocksForSuccessors(node, IrOpcode::kContinuation,
                                   IrOpcode::kLazyDeoptimization);
        }
        break;
      default:
        break;
    }
  }

  // Connect the blocks after nodes have been visited in post-order.
  GenericGraphVisit::Control Post(Node* node) {
    switch (node->opcode()) {
      case IrOpcode::kLoop:
      case IrOpcode::kMerge:
        ConnectMerge(node);
        break;
      case IrOpcode::kBranch:
        ConnectBranch(node);
        break;
      case IrOpcode::kDeoptimize:
        ConnectDeoptimize(node);
      case IrOpcode::kCall:
        if (OperatorProperties::CanLazilyDeoptimize(node->op())) {
          ConnectCall(node);
        }
        break;
      case IrOpcode::kReturn:
        ConnectReturn(node);
        break;
      default:
        break;
    }
    return GenericGraphVisit::CONTINUE;
  }

  void BuildBlockForNode(Node* node) {
    if (schedule_->block(node) == NULL) {
      BasicBlock* block = schedule_->NewBasicBlock();
      Trace("Create block B%d for node %d (%s)\n", block->id(), node->id(),
            IrOpcode::Mnemonic(node->opcode()));
      schedule_->AddNode(block, node);
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
  // IfFalse, Continuation, and LazyDeoptimization.
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
      if ((*j)->opcode() != IrOpcode::kReturn &&
          (*j)->opcode() != IrOpcode::kDeoptimize) {
        TraceConnect(merge, predecessor_block, block);
        schedule_->AddGoto(predecessor_block, block);
      }
    }
  }

  void ConnectDeoptimize(Node* deopt) {
    Node* deopt_block_node = NodeProperties::GetControlInput(deopt);
    BasicBlock* deopt_block = schedule_->block(deopt_block_node);
    TraceConnect(deopt, deopt_block, NULL);
    schedule_->AddDeoptimize(deopt_block, deopt);
  }

  void ConnectReturn(Node* ret) {
    Node* return_block_node = NodeProperties::GetControlInput(ret);
    BasicBlock* return_block = schedule_->block(return_block_node);
    TraceConnect(ret, return_block, NULL);
    schedule_->AddReturn(return_block, ret);
  }

  void ConnectCall(Node* call) {
    Node* call_block_node = NodeProperties::GetControlInput(call);
    BasicBlock* call_block = schedule_->block(call_block_node);

    BasicBlock* successor_blocks[2];
    CollectSuccessorBlocks(call, successor_blocks, IrOpcode::kContinuation,
                           IrOpcode::kLazyDeoptimization);

    TraceConnect(call, call_block, successor_blocks[0]);
    TraceConnect(call, call_block, successor_blocks[1]);

    schedule_->AddCall(call_block, call, successor_blocks[0],
                       successor_blocks[1]);
  }

  void TraceConnect(Node* node, BasicBlock* block, BasicBlock* succ) {
    DCHECK_NE(NULL, block);
    if (succ == NULL) {
      Trace("node %d (%s) in block %d -> end\n", node->id(),
            IrOpcode::Mnemonic(node->opcode()), block->id());
    } else {
      Trace("node %d (%s) in block %d -> block %d\n", node->id(),
            IrOpcode::Mnemonic(node->opcode()), block->id(), succ->id());
    }
  }
};


Scheduler::Scheduler(Zone* zone, Graph* graph, Schedule* schedule)
    : zone_(zone),
      graph_(graph),
      schedule_(schedule),
      unscheduled_uses_(IntVector::allocator_type(zone)),
      scheduled_nodes_(NodeVectorVector::allocator_type(zone)),
      schedule_root_nodes_(NodeVector::allocator_type(zone)),
      schedule_early_rpo_index_(IntVector::allocator_type(zone)) {}


Schedule* Scheduler::ComputeSchedule(Graph* graph) {
  Zone tmp_zone(graph->zone()->isolate());
  Schedule* schedule = new (graph->zone()) Schedule(graph->zone());

  Scheduler::ComputeCFG(graph, schedule);

  Scheduler scheduler(&tmp_zone, graph, schedule);

  scheduler.PrepareAuxiliaryNodeData();

  scheduler.PrepareAuxiliaryBlockData();

  Scheduler::ComputeSpecialRPO(schedule);
  scheduler.GenerateImmediateDominatorTree();

  scheduler.PrepareUses();
  scheduler.ScheduleEarly();
  scheduler.ScheduleLate();

  return schedule;
}


bool Scheduler::IsBasicBlockBegin(Node* node) {
  return OperatorProperties::IsBasicBlockBegin(node->op());
}


bool Scheduler::HasFixedSchedulePosition(Node* node) {
  IrOpcode::Value opcode = node->opcode();
  return (IrOpcode::IsControlOpcode(opcode)) ||
         opcode == IrOpcode::kParameter || opcode == IrOpcode::kEffectPhi ||
         opcode == IrOpcode::kPhi;
}


bool Scheduler::IsScheduleRoot(Node* node) {
  IrOpcode::Value opcode = node->opcode();
  return opcode == IrOpcode::kEnd || opcode == IrOpcode::kEffectPhi ||
         opcode == IrOpcode::kPhi;
}


void Scheduler::ComputeCFG(Graph* graph, Schedule* schedule) {
  CFGBuilder cfg_builder(schedule);
  Trace("---------------- CREATING CFG ------------------\n");
  schedule->AddNode(schedule->start(), graph->start());
  graph->VisitNodeInputsFromEnd(&cfg_builder);
  schedule->AddNode(schedule->end(), graph->end());
}


void Scheduler::PrepareAuxiliaryNodeData() {
  unscheduled_uses_.resize(graph_->NodeCount(), 0);
  schedule_early_rpo_index_.resize(graph_->NodeCount(), 0);
}


void Scheduler::PrepareAuxiliaryBlockData() {
  Zone* zone = schedule_->zone();
  scheduled_nodes_.resize(schedule_->BasicBlockCount(),
                          NodeVector(NodeVector::allocator_type(zone)));
  schedule_->immediate_dominator_.resize(schedule_->BasicBlockCount(), NULL);
}


BasicBlock* Scheduler::GetCommonDominator(BasicBlock* b1, BasicBlock* b2) {
  while (b1 != b2) {
    int b1_rpo = GetRPONumber(b1);
    int b2_rpo = GetRPONumber(b2);
    DCHECK(b1_rpo != b2_rpo);
    if (b1_rpo < b2_rpo) {
      b2 = schedule_->immediate_dominator_[b2->id()];
    } else {
      b1 = schedule_->immediate_dominator_[b1->id()];
    }
  }
  return b1;
}


void Scheduler::GenerateImmediateDominatorTree() {
  // Build the dominator graph.  TODO(danno): consider using Lengauer & Tarjan's
  // if this becomes really slow.
  Trace("------------ IMMEDIATE BLOCK DOMINATORS -----------\n");
  for (size_t i = 0; i < schedule_->rpo_order_.size(); i++) {
    BasicBlock* current_rpo = schedule_->rpo_order_[i];
    if (current_rpo != schedule_->start()) {
      BasicBlock::Predecessors::iterator current_pred =
          current_rpo->predecessors().begin();
      BasicBlock::Predecessors::iterator end =
          current_rpo->predecessors().end();
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
      schedule_->immediate_dominator_[current_rpo->id()] = dominator;
      Trace("Block %d's idom is %d\n", current_rpo->id(), dominator->id());
    }
  }
}


class ScheduleEarlyNodeVisitor : public NullNodeVisitor {
 public:
  explicit ScheduleEarlyNodeVisitor(Scheduler* scheduler)
      : has_changed_rpo_constraints_(true),
        scheduler_(scheduler),
        schedule_(scheduler->schedule_) {}

  GenericGraphVisit::Control Pre(Node* node) {
    int id = node->id();
    int max_rpo = 0;
    // Fixed nodes already know their schedule early position.
    if (scheduler_->HasFixedSchedulePosition(node)) {
      BasicBlock* block = schedule_->block(node);
      DCHECK(block != NULL);
      max_rpo = block->rpo_number_;
      if (scheduler_->schedule_early_rpo_index_[id] != max_rpo) {
        has_changed_rpo_constraints_ = true;
      }
      scheduler_->schedule_early_rpo_index_[id] = max_rpo;
      Trace("Node %d pre-scheduled early at rpo limit %d\n", id, max_rpo);
    }
    return GenericGraphVisit::CONTINUE;
  }

  GenericGraphVisit::Control Post(Node* node) {
    int id = node->id();
    int max_rpo = 0;
    // Otherwise, the minimum rpo for the node is the max of all of the inputs.
    if (!scheduler_->HasFixedSchedulePosition(node)) {
      for (InputIter i = node->inputs().begin(); i != node->inputs().end();
           ++i) {
        int control_rpo = scheduler_->schedule_early_rpo_index_[(*i)->id()];
        if (control_rpo > max_rpo) {
          max_rpo = control_rpo;
        }
      }
      if (scheduler_->schedule_early_rpo_index_[id] != max_rpo) {
        has_changed_rpo_constraints_ = true;
      }
      scheduler_->schedule_early_rpo_index_[id] = max_rpo;
      Trace("Node %d post-scheduled early at rpo limit %d\n", id, max_rpo);
    }
    return GenericGraphVisit::CONTINUE;
  }

  // TODO(mstarzinger): Dirty hack to unblock others, schedule early should be
  // rewritten to use a pre-order traversal from the start instead.
  bool has_changed_rpo_constraints_;

 private:
  Scheduler* scheduler_;
  Schedule* schedule_;
};


void Scheduler::ScheduleEarly() {
  Trace("------------------- SCHEDULE EARLY ----------------\n");

  int fixpoint_count = 0;
  ScheduleEarlyNodeVisitor visitor(this);
  while (visitor.has_changed_rpo_constraints_) {
    visitor.has_changed_rpo_constraints_ = false;
    graph_->VisitNodeInputsFromEnd(&visitor);
    fixpoint_count++;
  }

  Trace("It took %d iterations to determine fixpoint\n", fixpoint_count);
}


class PrepareUsesVisitor : public NullNodeVisitor {
 public:
  explicit PrepareUsesVisitor(Scheduler* scheduler)
      : scheduler_(scheduler), schedule_(scheduler->schedule_) {}

  GenericGraphVisit::Control Pre(Node* node) {
    // Some nodes must be scheduled explicitly to ensure they are in exactly the
    // right place; it's a convenient place during the preparation of use counts
    // to schedule them.
    if (!schedule_->IsScheduled(node) &&
        scheduler_->HasFixedSchedulePosition(node)) {
      Trace("Fixed position node %d is unscheduled, scheduling now\n",
            node->id());
      IrOpcode::Value opcode = node->opcode();
      BasicBlock* block =
          opcode == IrOpcode::kParameter
              ? schedule_->start()
              : schedule_->block(NodeProperties::GetControlInput(node));
      DCHECK(block != NULL);
      schedule_->AddNode(block, node);
    }

    if (scheduler_->IsScheduleRoot(node)) {
      scheduler_->schedule_root_nodes_.push_back(node);
    }

    return GenericGraphVisit::CONTINUE;
  }

  void PostEdge(Node* from, int index, Node* to) {
    // If the edge is from an unscheduled node, then tally it in the use count
    // for all of its inputs. The same criterion will be used in ScheduleLate
    // for decrementing use counts.
    if (!schedule_->IsScheduled(from)) {
      DCHECK(!scheduler_->HasFixedSchedulePosition(from));
      ++scheduler_->unscheduled_uses_[to->id()];
      Trace("Incrementing uses of node %d from %d to %d\n", to->id(),
            from->id(), scheduler_->unscheduled_uses_[to->id()]);
    }
  }

 private:
  Scheduler* scheduler_;
  Schedule* schedule_;
};


void Scheduler::PrepareUses() {
  Trace("------------------- PREPARE USES ------------------\n");
  // Count the uses of every node, it will be used to ensure that all of a
  // node's uses are scheduled before the node itself.
  PrepareUsesVisitor prepare_uses(this);
  graph_->VisitNodeInputsFromEnd(&prepare_uses);
}


class ScheduleLateNodeVisitor : public NullNodeVisitor {
 public:
  explicit ScheduleLateNodeVisitor(Scheduler* scheduler)
      : scheduler_(scheduler), schedule_(scheduler_->schedule_) {}

  GenericGraphVisit::Control Pre(Node* node) {
    // Don't schedule nodes that are already scheduled.
    if (schedule_->IsScheduled(node)) {
      return GenericGraphVisit::CONTINUE;
    }
    DCHECK(!scheduler_->HasFixedSchedulePosition(node));

    // If all the uses of a node have been scheduled, then the node itself can
    // be scheduled.
    bool eligible = scheduler_->unscheduled_uses_[node->id()] == 0;
    Trace("Testing for schedule eligibility for node %d -> %s\n", node->id(),
          eligible ? "true" : "false");
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

    int min_rpo = scheduler_->schedule_early_rpo_index_[node->id()];
    Trace(
        "Schedule late conservative for node %d is block %d at "
        "loop depth %d, min rpo = %d\n",
        node->id(), block->id(), block->loop_depth_, min_rpo);
    // Hoist nodes out of loops if possible. Nodes can be hoisted iteratively
    // into enlcosing loop pre-headers until they would preceed their
    // ScheduleEarly position.
    BasicBlock* hoist_block = block;
    while (hoist_block != NULL && hoist_block->rpo_number_ >= min_rpo) {
      if (hoist_block->loop_depth_ < block->loop_depth_) {
        block = hoist_block;
        Trace("Hoisting node %d to block %d\n", node->id(), block->id());
      }
      // Try to hoist to the pre-header of the loop header.
      hoist_block = hoist_block->loop_header();
      if (hoist_block != NULL) {
        BasicBlock* pre_header = schedule_->dominator(hoist_block);
        DCHECK(pre_header == NULL ||
               *hoist_block->predecessors().begin() == pre_header);
        Trace(
            "Try hoist to pre-header block %d of loop header block %d,"
            " depth would be %d\n",
            pre_header->id(), hoist_block->id(), pre_header->loop_depth_);
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
    // If the use is a phi, forward through the phi to the basic block
    // corresponding to the phi's input.
    if (opcode == IrOpcode::kPhi || opcode == IrOpcode::kEffectPhi) {
      int index = edge.index();
      Trace("Use %d is input %d to a phi\n", use->id(), index);
      use = NodeProperties::GetControlInput(use, 0);
      opcode = use->opcode();
      DCHECK(opcode == IrOpcode::kMerge || opcode == IrOpcode::kLoop);
      use = NodeProperties::GetControlInput(use, index);
    }
    BasicBlock* result = schedule_->block(use);
    if (result == NULL) return NULL;
    Trace("Must dominate use %d in block %d\n", use->id(), result->id());
    return result;
  }

  void ScheduleNode(BasicBlock* block, Node* node) {
    schedule_->PlanNode(block, node);
    scheduler_->scheduled_nodes_[block->id()].push_back(node);

    // Reduce the use count of the node's inputs to potentially make them
    // scheduable.
    for (InputIter i = node->inputs().begin(); i != node->inputs().end(); ++i) {
      DCHECK(scheduler_->unscheduled_uses_[(*i)->id()] > 0);
      --scheduler_->unscheduled_uses_[(*i)->id()];
      if (FLAG_trace_turbo_scheduler) {
        Trace("Decrementing use count for node %d from node %d (now %d)\n",
              (*i)->id(), i.edge().from()->id(),
              scheduler_->unscheduled_uses_[(*i)->id()]);
        if (scheduler_->unscheduled_uses_[(*i)->id()] == 0) {
          Trace("node %d is now eligible for scheduling\n", (*i)->id());
        }
      }
    }
  }

  Scheduler* scheduler_;
  Schedule* schedule_;
};


void Scheduler::ScheduleLate() {
  Trace("------------------- SCHEDULE LATE -----------------\n");

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


// Numbering for BasicBlockData.rpo_number_ for this block traversal:
static const int kBlockOnStack = -2;
static const int kBlockVisited1 = -3;
static const int kBlockVisited2 = -4;
static const int kBlockUnvisited1 = -1;
static const int kBlockUnvisited2 = kBlockVisited1;

struct SpecialRPOStackFrame {
  BasicBlock* block;
  int index;
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
      l->block->rpo_number_ = static_cast<int>(final_order->size());
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
  if (child->rpo_number_ == unvisited) {
    stack[depth].block = child;
    stack[depth].index = 0;
    child->rpo_number_ = kBlockOnStack;
    return depth + 1;
  }
  return depth;
}


// Computes loop membership from the backedges of the control flow graph.
static LoopInfo* ComputeLoopInfo(
    Zone* zone, SpecialRPOStackFrame* queue, int num_loops, int num_blocks,
    ZoneList<std::pair<BasicBlock*, int> >* backedges) {
  LoopInfo* loops = zone->NewArray<LoopInfo>(num_loops);
  memset(loops, 0, num_loops * sizeof(LoopInfo));

  // Compute loop membership starting from backedges.
  // O(max(loop_depth) * max(|loop|)
  for (int i = 0; i < backedges->length(); i++) {
    BasicBlock* member = backedges->at(i).first;
    BasicBlock* header = member->SuccessorAt(backedges->at(i).second);
    int loop_num = header->loop_end_;
    if (loops[loop_num].header == NULL) {
      loops[loop_num].header = header;
      loops[loop_num].members = new (zone) BitVector(num_blocks, zone);
    }

    int queue_length = 0;
    if (member != header) {
      // As long as the header doesn't have a backedge to itself,
      // Push the member onto the queue and process its predecessors.
      if (!loops[loop_num].members->Contains(member->id())) {
        loops[loop_num].members->Add(member->id());
      }
      queue[queue_length++].block = member;
    }

    // Propagate loop membership backwards. All predecessors of M up to the
    // loop header H are members of the loop too. O(|blocks between M and H|).
    while (queue_length > 0) {
      BasicBlock* block = queue[--queue_length].block;
      for (int i = 0; i < block->PredecessorCount(); i++) {
        BasicBlock* pred = block->PredecessorAt(i);
        if (pred != header) {
          if (!loops[loop_num].members->Contains(pred->id())) {
            loops[loop_num].members->Add(pred->id());
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
  PrintF("-- RPO with %d loops ", num_loops);
  if (num_loops > 0) {
    PrintF("(");
    for (int i = 0; i < num_loops; i++) {
      if (i > 0) PrintF(" ");
      PrintF("B%d", loops[i].header->id());
    }
    PrintF(") ");
  }
  PrintF("-- \n");

  for (int i = 0; i < static_cast<int>(order->size()); i++) {
    BasicBlock* block = (*order)[i];
    int bid = block->id();
    PrintF("%5d:", i);
    for (int i = 0; i < num_loops; i++) {
      bool membership = loops[i].members->Contains(bid);
      bool range = loops[i].header->LoopContains(block);
      PrintF(membership ? " |" : "  ");
      PrintF(range ? "x" : " ");
    }
    PrintF("  B%d: ", bid);
    if (block->loop_end_ >= 0) {
      PrintF(" range: [%d, %d)", block->rpo_number_, block->loop_end_);
    }
    PrintF("\n");
  }
}


static void VerifySpecialRPO(int num_loops, LoopInfo* loops,
                             BasicBlockVector* order) {
  DCHECK(order->size() > 0);
  DCHECK((*order)[0]->id() == 0);  // entry should be first.

  for (int i = 0; i < num_loops; i++) {
    LoopInfo* loop = &loops[i];
    BasicBlock* header = loop->header;

    DCHECK(header != NULL);
    DCHECK(header->rpo_number_ >= 0);
    DCHECK(header->rpo_number_ < static_cast<int>(order->size()));
    DCHECK(header->loop_end_ >= 0);
    DCHECK(header->loop_end_ <= static_cast<int>(order->size()));
    DCHECK(header->loop_end_ > header->rpo_number_);

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
      DCHECK(l->block->rpo_number_ == links + loop->header->rpo_number_);
      links++;
      l = l->next;
      DCHECK(links < static_cast<int>(2 * order->size()));  // cycle?
    }
    DCHECK(links > 0);
    DCHECK(links == (header->loop_end_ - header->rpo_number_));
    DCHECK(end_found);

    // Check the contiguousness of loops.
    int count = 0;
    for (int j = 0; j < static_cast<int>(order->size()); j++) {
      BasicBlock* block = order->at(j);
      DCHECK(block->rpo_number_ == j);
      if (j < header->rpo_number_ || j >= header->loop_end_) {
        DCHECK(!loop->members->Contains(block->id()));
      } else {
        if (block == header) {
          DCHECK(!loop->members->Contains(block->id()));
        } else {
          DCHECK(loop->members->Contains(block->id()));
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
  Trace("------------- COMPUTING SPECIAL RPO ---------------\n");
  // RPO should not have been computed for this schedule yet.
  CHECK_EQ(kBlockUnvisited1, schedule->start()->rpo_number_);
  CHECK_EQ(0, static_cast<int>(schedule->rpo_order_.size()));

  // Perform an iterative RPO traversal using an explicit stack,
  // recording backedges that form cycles. O(|B|).
  ZoneList<std::pair<BasicBlock*, int> > backedges(1, zone);
  SpecialRPOStackFrame* stack =
      zone->NewArray<SpecialRPOStackFrame>(schedule->BasicBlockCount());
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
      if (succ->rpo_number_ == kBlockVisited1) continue;
      if (succ->rpo_number_ == kBlockOnStack) {
        // The successor is on the stack, so this is a backedge (cycle).
        backedges.Add(
            std::pair<BasicBlock*, int>(frame->block, frame->index - 1), zone);
        if (succ->loop_end_ < 0) {
          // Assign a new loop number to the header if it doesn't have one.
          succ->loop_end_ = num_loops++;
        }
      } else {
        // Push the successor onto the stack.
        DCHECK(succ->rpo_number_ == kBlockUnvisited1);
        stack_depth = Push(stack, stack_depth, succ, kBlockUnvisited1);
      }
    } else {
      // Finished with all successors; pop the stack and add the block.
      order = order->Add(zone, frame->block);
      frame->block->rpo_number_ = kBlockVisited1;
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
    LoopInfo* loop = entry->IsLoopHeader() ? &loops[entry->loop_end_] : NULL;
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
        if (block->rpo_number_ == kBlockOnStack) {
          // Finish the loop body the first time the header is left on the
          // stack.
          DCHECK(loop != NULL && loop->header == block);
          loop->start = order->Add(zone, block);
          order = loop->end;
          block->rpo_number_ = kBlockVisited2;
          // Pop the loop stack and continue visiting outgoing edges within the
          // the context of the outer loop, if any.
          loop = loop->prev;
          // We leave the loop header on the stack; the rest of this iteration
          // and later iterations will go through its outgoing edges list.
        }

        // Use the next outgoing edge if there are any.
        int outgoing_index = frame->index - block->SuccessorCount();
        LoopInfo* info = &loops[block->loop_end_];
        DCHECK(loop != info);
        if (info->outgoing != NULL &&
            outgoing_index < info->outgoing->length()) {
          succ = info->outgoing->at(outgoing_index);
          frame->index++;
        }
      }

      if (succ != NULL) {
        // Process the next successor.
        if (succ->rpo_number_ == kBlockOnStack) continue;
        if (succ->rpo_number_ == kBlockVisited2) continue;
        DCHECK(succ->rpo_number_ == kBlockUnvisited2);
        if (loop != NULL && !loop->members->Contains(succ->id())) {
          // The successor is not in the current loop or any nested loop.
          // Add it to the outgoing edges of this loop and visit it later.
          loop->AddOutgoing(zone, succ);
        } else {
          // Push the successor onto the stack.
          stack_depth = Push(stack, stack_depth, succ, kBlockUnvisited2);
          if (succ->IsLoopHeader()) {
            // Push the inner loop onto the loop stack.
            DCHECK(succ->loop_end_ >= 0 && succ->loop_end_ < num_loops);
            LoopInfo* next = &loops[succ->loop_end_];
            next->end = order;
            next->prev = loop;
            loop = next;
          }
        }
      } else {
        // Finished with all successors of the current block.
        if (block->IsLoopHeader()) {
          // If we are going to pop a loop header, then add its entire body.
          LoopInfo* info = &loops[block->loop_end_];
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
          block->rpo_number_ = kBlockVisited2;
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
    current->loop_header_ = current_header;
    if (current->IsLoopHeader()) {
      loop_depth++;
      current_loop = &loops[current->loop_end_];
      BlockList* end = current_loop->end;
      current->loop_end_ = end == NULL ? static_cast<int>(final_order->size())
                                       : end->block->rpo_number_;
      current_header = current_loop->header;
      Trace("Block %d is a loop header, increment loop depth to %d\n",
            current->id(), loop_depth);
    } else {
      while (current_header != NULL &&
             current->rpo_number_ >= current_header->loop_end_) {
        DCHECK(current_header->IsLoopHeader());
        DCHECK(current_loop != NULL);
        current_loop = current_loop->prev;
        current_header = current_loop == NULL ? NULL : current_loop->header;
        --loop_depth;
      }
    }
    current->loop_depth_ = loop_depth;
    Trace("Block %d's loop header is block %d, loop depth %d\n", current->id(),
          current->loop_header_ == NULL ? -1 : current->loop_header_->id(),
          current->loop_depth_);
  }

#if DEBUG
  if (FLAG_trace_turbo_scheduler) PrintRPO(num_loops, loops, final_order);
  VerifySpecialRPO(num_loops, loops, final_order);
#endif
  return final_order;
}
}
}
}  // namespace v8::internal::compiler
