// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-regalloc.h"

#include "src/base/logging.h"
#include "src/compiler/backend/instruction.h"
#include "src/maglev/maglev-compilation-data.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph-printer.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir.h"
#include "src/maglev/maglev-regalloc-data.h"

namespace v8 {
namespace internal {

namespace maglev {

namespace {

constexpr RegisterStateFlags initialized_node{true, false};
constexpr RegisterStateFlags initialized_merge{true, true};

using BlockReverseIterator = std::vector<BasicBlock>::reverse_iterator;

// A target is a fallthrough of a control node if its ID is the next ID
// after the control node.
//
// TODO(leszeks): Consider using the block iterator instead.
bool IsTargetOfNodeFallthrough(ControlNode* node, BasicBlock* target) {
  return node->id() + 1 == target->first_id();
}

ControlNode* NearestPostDominatingHole(ControlNode* node) {
  // Conditional control nodes don't cause holes themselves. So, the nearest
  // post-dominating hole is the conditional control node's next post-dominating
  // hole.
  if (node->Is<ConditionalControlNode>()) {
    return node->next_post_dominating_hole();
  }

  // If the node is a Jump, it may be a hole, but only if it is not a
  // fallthrough (jump to the immediately next block). Otherwise, it will point
  // to the nearest post-dominating hole in its own "next" field.
  if (Jump* jump = node->TryCast<Jump>()) {
    if (IsTargetOfNodeFallthrough(jump, jump->target())) {
      return jump->next_post_dominating_hole();
    }
  }

  return node;
}

bool IsLiveAtTarget(LiveNodeInfo* info, ControlNode* source,
                    BasicBlock* target) {
  if (info == nullptr) return false;

  // If we're looping, a value can only be live if it was live before the loop.
  if (target->control_node()->id() <= source->id()) {
    // Gap moves may already be inserted in the target, so skip over those.
    return info->node->id() < target->FirstNonGapMoveId();
  }
  // TODO(verwaest): This should be true but isn't because we don't yet
  // eliminate dead code.
  // DCHECK_GT(info->next_use, source->id());
  // TODO(verwaest): Since we don't support deopt yet we can only deal with
  // direct branches. Add support for holes.
  return info->last_use >= target->first_id();
}

}  // namespace

StraightForwardRegisterAllocator::StraightForwardRegisterAllocator(
    MaglevCompilationUnit* compilation_unit, Graph* graph)
    : compilation_unit_(compilation_unit) {
  ComputePostDominatingHoles(graph);
  for (int i = 0; i < kAllocatableGeneralRegisterCount; i++) {
    free_registers_[i] = i;
  }
  AllocateRegisters(graph);
  graph->set_stack_slots(top_of_stack_);
}

StraightForwardRegisterAllocator::~StraightForwardRegisterAllocator() = default;

// Compute, for all forward control nodes (i.e. excluding Return and JumpLoop) a
// tree of post-dominating control flow holes.
//
// Control flow which interrupts linear control flow fallthrough for basic
// blocks is considered to introduce a control flow "hole".
//
//                   A──────┐                │
//                   │ Jump │                │
//                   └──┬───┘                │
//                  {   │  B──────┐          │
//     Control flow {   │  │ Jump │          │ Linear control flow
//     hole after A {   │  └─┬────┘          │
//                  {   ▼    ▼ Fallthrough   │
//                     C──────┐              │
//                     │Return│              │
//                     └──────┘              ▼
//
// It is interesting, for each such hole, to know what the next hole will be
// that we will unconditionally reach on our way to an exit node. Such
// subsequent holes are in "post-dominators" of the current block.
//
// As an example, consider the following CFG, with the annotated holes. The
// post-dominating hole tree is the transitive closure of the post-dominator
// tree, up to nodes which are holes (in this example, A, D, F and H).
//
//                       CFG               Immediate       Post-dominating
//                                      post-dominators          holes
//                   A──────┐
//                   │ Jump │               A                 A
//                   └──┬───┘               │                 │
//                  {   │  B──────┐         │                 │
//     Control flow {   │  │ Jump │         │   B             │       B
//     hole after A {   │  └─┬────┘         │   │             │       │
//                  {   ▼    ▼              │   │             │       │
//                     C──────┐             │   │             │       │
//                     │Branch│             └►C◄┘             │   C   │
//                     └┬────┬┘               │               │   │   │
//                      ▼    │                │               │   │   │
//                   D──────┐│                │               │   │   │
//                   │ Jump ││              D │               │ D │   │
//                   └──┬───┘▼              │ │               │ │ │   │
//                  {   │  E──────┐         │ │               │ │ │   │
//     Control flow {   │  │ Jump │         │ │ E             │ │ │ E │
//     hole after D {   │  └─┬────┘         │ │ │             │ │ │ │ │
//                  {   ▼    ▼              │ │ │             │ │ │ │ │
//                     F──────┐             │ ▼ │             │ │ ▼ │ │
//                     │ Jump │             └►F◄┘             └─┴►F◄┴─┘
//                     └─────┬┘               │                   │
//                  {        │  G──────┐      │                   │
//     Control flow {        │  │ Jump │      │ G                 │ G
//     hole after F {        │  └─┬────┘      │ │                 │ │
//                  {        ▼    ▼           │ │                 │ │
//                          H──────┐          ▼ │                 ▼ │
//                          │Return│          H◄┘                 H◄┘
//                          └──────┘
//
// Since we only care about forward control, loop jumps are treated the same as
// returns -- they terminate the post-dominating hole chain.
//
void StraightForwardRegisterAllocator::ComputePostDominatingHoles(
    Graph* graph) {
  // For all blocks, find the list of jumps that jump over code unreachable from
  // the block. Such a list of jumps terminates in return or jumploop.
  for (BasicBlock* block : base::Reversed(*graph)) {
    ControlNode* control = block->control_node();
    if (auto node = control->TryCast<Jump>()) {
      // If the current control node is a jump, prepend it to the list of jumps
      // at the target.
      control->set_next_post_dominating_hole(
          NearestPostDominatingHole(node->target()->control_node()));
    } else if (auto node = control->TryCast<ConditionalControlNode>()) {
      ControlNode* first =
          NearestPostDominatingHole(node->if_true()->control_node());
      ControlNode* second =
          NearestPostDominatingHole(node->if_false()->control_node());

      // Either find the merge-point of both branches, or the highest reachable
      // control-node of the longest branch after the last node of the shortest
      // branch.

      // As long as there's no merge-point.
      while (first != second) {
        // Walk the highest branch to find where it goes.
        if (first->id() > second->id()) std::swap(first, second);

        // If the first branch returns or jumps back, we've found highest
        // reachable control-node of the longest branch (the second control
        // node).
        if (first->Is<Return>() || first->Is<JumpLoop>()) {
          control->set_next_post_dominating_hole(second);
          break;
        }

        // Continue one step along the highest branch. This may cross over the
        // lowest branch in case it returns or loops. If labelled blocks are
        // involved such swapping of which branch is the highest branch can
        // occur multiple times until a return/jumploop/merge is discovered.
        first = first->next_post_dominating_hole();
      }

      // Once the branches merged, we've found the gap-chain that's relevant for
      // the control node.
      control->set_next_post_dominating_hole(first);
    }
  }
}

void StraightForwardRegisterAllocator::PrintLiveRegs() const {
  bool first = true;
  for (int i = 0; i < kAllocatableGeneralRegisterCount; i++) {
    LiveNodeInfo* info = register_values_[i];
    if (info == nullptr) continue;
    if (first) {
      first = false;
    } else {
      printing_visitor_->os() << ", ";
    }
    printing_visitor_->os()
        << MapIndexToRegister(i) << "=v" << info->node->id();
  }
}

void StraightForwardRegisterAllocator::AllocateRegisters(Graph* graph) {
  if (FLAG_trace_maglev_regalloc) {
    printing_visitor_.reset(new MaglevPrintingVisitor(std::cout));
    printing_visitor_->PreProcessGraph(compilation_unit_, graph);
  }

  for (block_it_ = graph->begin(); block_it_ != graph->end(); ++block_it_) {
    BasicBlock* block = *block_it_;

    // Restore mergepoint state.
    if (block->has_state()) {
      InitializeRegisterValues(block->state()->register_state());
    }

    if (FLAG_trace_maglev_regalloc) {
      printing_visitor_->PreProcessBasicBlock(compilation_unit_, block);
      printing_visitor_->os() << "live regs: ";
      PrintLiveRegs();

      ControlNode* control = NearestPostDominatingHole(block->control_node());
      if (!control->Is<JumpLoop>()) {
        printing_visitor_->os() << "\n[holes:";
        while (true) {
          if (control->Is<Jump>()) {
            BasicBlock* target = control->Cast<Jump>()->target();
            printing_visitor_->os()
                << " " << control->id() << "-" << target->first_id();
            control = control->next_post_dominating_hole();
            DCHECK_NOT_NULL(control);
            continue;
          } else if (control->Is<Return>()) {
            printing_visitor_->os() << " " << control->id() << ".";
            break;
          } else if (control->Is<JumpLoop>()) {
            printing_visitor_->os() << " " << control->id() << "↰";
            break;
          }
          UNREACHABLE();
        }
        printing_visitor_->os() << "]";
      }
      printing_visitor_->os() << std::endl;
    }

    // Activate phis.
    if (block->has_phi()) {
      // Firstly, make the phi live, and try to assign it to an input
      // location.
      for (Phi* phi : *block->phis()) {
        phi->SetNoSpillOrHint();
        LiveNodeInfo* info = MakeLive(phi);
        TryAllocateToInput(info, phi);
      }
      // Secondly try to assign the phi to a free register.
      for (Phi* phi : *block->phis()) {
        if (phi->result().operand().IsAllocated()) continue;
        compiler::InstructionOperand allocation =
            TryAllocateRegister(&values_[phi]);
        if (allocation.IsAllocated()) {
          phi->result().SetAllocated(
              compiler::AllocatedOperand::cast(allocation));
          if (FLAG_trace_maglev_regalloc) {
            printing_visitor_->Process(
                phi, ProcessingState(compilation_unit_, block_it_, nullptr,
                                     nullptr, nullptr));
            printing_visitor_->os()
                << "phi (new reg) " << phi->result().operand() << std::endl;
          }
        }
      }
      // Finally just use a stack slot.
      for (Phi* phi : *block->phis()) {
        if (phi->result().operand().IsAllocated()) continue;
        LiveNodeInfo& info = values_[phi];
        AllocateSpillSlot(&info);
        // TODO(verwaest): Will this be used at all?
        phi->result().SetAllocated(
            compiler::AllocatedOperand::cast(info.stack_slot));
        if (FLAG_trace_maglev_regalloc) {
          printing_visitor_->Process(
              phi, ProcessingState(compilation_unit_, block_it_, nullptr,
                                   nullptr, nullptr));
          printing_visitor_->os()
              << "phi (stack) " << phi->result().operand() << std::endl;
        }
      }

      if (FLAG_trace_maglev_regalloc) {
        printing_visitor_->os() << "live regs: ";
        PrintLiveRegs();
        printing_visitor_->os() << std::endl;
      }
    }

    node_it_ = block->nodes().begin();
    for (; node_it_ != block->nodes().end(); ++node_it_) {
      AllocateNode(*node_it_);
    }
    AllocateControlNode(block->control_node(), block);
  }
}

class StraightForwardRegisterAllocator::InputsUpdater {
 public:
  explicit InputsUpdater(StraightForwardRegisterAllocator* allocator,
                         NodeBase* node)
      : allocator_(allocator), use_(node->id()) {}

  void UpdateInputUse(const Input& input) {
    ValueNode* node = input.node();
    auto it = allocator_->values_.find(node);
    // If a value is dead, make sure it's cleared.
    if (node->live_range().end == use_) {
      // Mark the info for clearing by clearing the node.
      if (it->second.node == nullptr) return;
      if (it->second.reg.is_valid()) {
        // Collect values in registers for clearing later.
        it->second.node = nullptr;
        to_clear_[register_values_to_clear_++] = it;
      } else {
        // Immediately clear values in stack slots.
        Clear(it);
      }
    } else {
      // Otherwise update the next use.
      DCHECK_NE(it, allocator_->values_.end());
      it->second.next_use = input.next_use_id();
    }
  }

  ~InputsUpdater() {
    // If no values died, simply return.
    if (register_values_to_clear_ == 0) return;
    // First clear the registers pointing to to-clear infos.
    for (int i = 0; i < kAllocatableGeneralRegisterCount; i++) {
      LiveNodeInfo* value = allocator_->register_values_[i];
      if (value == nullptr) continue;
      if (value->node != nullptr) {
        // The value shouldn't be dead yet.
        // TODO(verwaest): This won't work yet because of deopt uses.
        // DCHECK_GT(value->last_use, use_);
        continue;
      }
      allocator_->FreeRegister(i);
    }
    // Then clear the infos.
    for (int i = 0; i < register_values_to_clear_; i++) Clear(to_clear_[i]);
  }

 private:
  void Clear(LiveNodeInfoMap::iterator& it) {
    LiveNodeInfo& info = it->second;
    // If the stack slot is a local slot, free it so it can be reused.
    if (info.stack_slot.IsAnyStackSlot()) {
      compiler::AllocatedOperand slot =
          compiler::AllocatedOperand::cast(info.stack_slot);
      if (slot.index() > 0) allocator_->free_slots_.push_back(slot.index());
    }
    allocator_->values_.erase(it);
  }

  StraightForwardRegisterAllocator* const allocator_;
  const uint32_t use_;
  LiveNodeInfoMap::iterator to_clear_[kAllocatableGeneralRegisterCount];
  int register_values_to_clear_ = 0;
};

void StraightForwardRegisterAllocator::AllocateNode(Node* node) {
  for (Input& input : *node) AssignInput(input);
  AssignTemporaries(node);
  {
    InputsUpdater updater(this, node);
    for (Input& input : *node) updater.UpdateInputUse(input);
  }

  if (node->properties().is_call()) SpillAndClearRegisters();
  // TODO(verwaest): This isn't a good idea :)
  if (node->properties().can_deopt()) SpillRegisters();

  // Allocate node output.
  if (node->Is<ValueNode>()) AllocateNodeResult(node->Cast<ValueNode>());

  if (FLAG_trace_maglev_regalloc) {
    printing_visitor_->Process(
        node, ProcessingState(compilation_unit_, block_it_, nullptr, nullptr,
                              nullptr));
    printing_visitor_->os() << "live regs: ";
    PrintLiveRegs();
    printing_visitor_->os() << "\n";
  }
}

void StraightForwardRegisterAllocator::AllocateNodeResult(ValueNode* node) {
  LiveNodeInfo* info = MakeLive(node);
  DCHECK(!node->Is<Phi>());

  node->SetNoSpillOrHint();

  compiler::UnallocatedOperand operand =
      compiler::UnallocatedOperand::cast(node->result().operand());

  if (operand.basic_policy() == compiler::UnallocatedOperand::FIXED_SLOT) {
    DCHECK(node->Is<InitialValue>());
    DCHECK_LT(operand.fixed_slot_index(), 0);
    // Set the stack slot to exactly where the value is.
    node->result().SetAllocated(compiler::AllocatedOperand::STACK_SLOT,
                                MachineRepresentation::kTagged,
                                operand.fixed_slot_index());
    info->stack_slot = node->result().operand();
    return;
  }

  switch (operand.extended_policy()) {
    case compiler::UnallocatedOperand::FIXED_REGISTER: {
      Register r = Register::from_code(operand.fixed_register_index());
      node->result().SetAllocated(ForceAllocate(r, info));
      break;
    }

    case compiler::UnallocatedOperand::MUST_HAVE_REGISTER:
      node->result().SetAllocated(AllocateRegister(info));
      break;

    case compiler::UnallocatedOperand::SAME_AS_INPUT: {
      Input& input = node->input(operand.input_index());
      Register r = input.AssignedRegister();
      node->result().SetAllocated(ForceAllocate(r, info));
      break;
    }

    case compiler::UnallocatedOperand::REGISTER_OR_SLOT_OR_CONSTANT:
    case compiler::UnallocatedOperand::NONE:
    case compiler::UnallocatedOperand::FIXED_FP_REGISTER:
    case compiler::UnallocatedOperand::MUST_HAVE_SLOT:
    case compiler::UnallocatedOperand::REGISTER_OR_SLOT:
      UNREACHABLE();
  }

  // Immediately kill the register use if the node doesn't have a valid
  // live-range.
  // TODO(verwaest): Remove once we can avoid allocating such registers.
  if (!node->has_valid_live_range() &&
      node->result().operand().IsAnyRegister()) {
    auto it = values_.find(node);
    Register reg = it->second.reg;
    DCHECK(reg.is_valid());
    values_.erase(it);
    FreeRegister(MapRegisterToIndex(reg));
  }
}

void StraightForwardRegisterAllocator::Free(const Register& reg) {
  int index = MapRegisterToIndex(reg);
  LiveNodeInfo* info = register_values_[index];

  // If the register is already free, return.
  if (info == nullptr) return;

  // Free the register without adding it to the list.
  register_values_[index] = nullptr;

  // If the value we're freeing from the register is already known to be
  // assigned to a different register as well, simply reeturn.
  if (reg != info->reg) {
    DCHECK_EQ(info, register_values_[MapRegisterToIndex(info->reg)]);
    return;
  }

  info->reg = Register::no_reg();

  // If the value is already spilled, return.
  if (info->stack_slot.IsAnyStackSlot()) return;

  // If the value is already in another register, return.
  for (int i = 0; i < kAllocatableGeneralRegisterCount; i++) {
    if (register_values_[i] == info) {
      // Found an existing register.
      info->reg = MapIndexToRegister(i);
      return;
    }
  }

  // Try to move the value to another register.
  if (free_register_size_ > 0) {
    Register target_reg =
        MapIndexToRegister(free_registers_[--free_register_size_]);
    SetRegister(target_reg, info);
    // Emit a gapmove.
    compiler::AllocatedOperand source(compiler::LocationOperand::REGISTER,
                                      MachineRepresentation::kTagged,
                                      reg.code());
    compiler::AllocatedOperand target(compiler::LocationOperand::REGISTER,
                                      MachineRepresentation::kTagged,
                                      target_reg.code());

    if (FLAG_trace_maglev_regalloc) {
      printing_visitor_->os() << "gap move: ";
      graph_labeller()->PrintNodeLabel(std::cout, info->node);
      printing_visitor_->os() << ": " << target << " ← " << source << std::endl;
    }
    AddMoveBeforeCurrentNode(source, target);
    return;
  }

  // If all else fails, spill the value.
  Spill(info);
}

void StraightForwardRegisterAllocator::InitializeConditionalBranchRegisters(
    ConditionalControlNode* node, BasicBlock* target) {
  if (target->is_empty_block()) {
    // Jumping over an empty block, so we're in fact merging.
    Jump* jump = target->control_node()->Cast<Jump>();
    target = jump->target();
    return MergeRegisterValues(node, target, jump->predecessor_id());
  }
  if (target->has_state()) {
    // Not a fall-through branch, copy the state over.
    return InitializeBranchTargetRegisterValues(node, target);
  }
  // Clear dead fall-through registers.
  DCHECK_EQ(node->id() + 1, target->first_id());
  for (int i = 0; i < kAllocatableGeneralRegisterCount; i++) {
    LiveNodeInfo* info = register_values_[i];
    if (info != nullptr && !IsLiveAtTarget(info, node, target)) {
      info->reg = Register::no_reg();
      FreeRegister(i);
    }
  }
}

void StraightForwardRegisterAllocator::AllocateControlNode(ControlNode* node,
                                                           BasicBlock* block) {
  for (Input& input : *node) AssignInput(input);
  AssignTemporaries(node);
  {
    InputsUpdater updater(this, node);
    for (Input& input : *node) updater.UpdateInputUse(input);
  }

  if (node->properties().is_call()) SpillAndClearRegisters();

  // Inject allocation into target phis.
  if (auto unconditional = node->TryCast<UnconditionalControlNode>()) {
    BasicBlock* target = unconditional->target();
    if (target->has_phi()) {
      Phi::List* phis = target->phis();
      for (Phi* phi : *phis) {
        Input& input = phi->input(block->predecessor_id());
        LiveNodeInfo& info = values_[input.node()];
        input.InjectAllocated(info.allocation());
      }
      InputsUpdater updater(this, node);
      for (Phi* phi : *phis) {
        updater.UpdateInputUse(phi->input(block->predecessor_id()));
      }
    }
  }

  // TODO(verwaest): This isn't a good idea :)
  if (node->properties().can_deopt()) SpillRegisters();

  // Merge register values. Values only flowing into phis and not being
  // independently live will be killed as part of the merge.
  if (auto unconditional = node->TryCast<UnconditionalControlNode>()) {
    // Empty blocks are immediately merged at the control of their predecessor.
    if (!block->is_empty_block()) {
      MergeRegisterValues(unconditional, unconditional->target(),
                          block->predecessor_id());
    }
  } else if (auto conditional = node->TryCast<ConditionalControlNode>()) {
    InitializeConditionalBranchRegisters(conditional, conditional->if_true());
    InitializeConditionalBranchRegisters(conditional, conditional->if_false());
  }

  if (FLAG_trace_maglev_regalloc) {
    printing_visitor_->Process(
        node, ProcessingState(compilation_unit_, block_it_, nullptr, nullptr,
                              nullptr));
  }
}

void StraightForwardRegisterAllocator::TryAllocateToInput(LiveNodeInfo* info,
                                                          Phi* phi) {
  DCHECK_EQ(info->node, phi);
  // Try allocate phis to a register used by any of the inputs.
  for (Input& input : *phi) {
    if (input.operand().IsRegister()) {
      Register reg = input.AssignedRegister();
      int index = MapRegisterToIndex(reg);
      if (register_values_[index] == nullptr) {
        phi->result().SetAllocated(ForceAllocate(reg, info));
        if (FLAG_trace_maglev_regalloc) {
          Phi* phi = info->node->Cast<Phi>();
          printing_visitor_->Process(
              phi, ProcessingState(compilation_unit_, block_it_, nullptr,
                                   nullptr, nullptr));
          printing_visitor_->os()
              << "phi (reuse) " << input.operand() << std::endl;
        }
        return;
      }
    }
  }
}

void StraightForwardRegisterAllocator::AddMoveBeforeCurrentNode(
    compiler::AllocatedOperand source, compiler::AllocatedOperand target) {
  GapMove* gap_move =
      Node::New<GapMove>(compilation_unit_->zone(), {}, source, target);
  if (compilation_unit_->has_graph_labeller()) {
    graph_labeller()->RegisterNode(gap_move);
  }
  if (*node_it_ == nullptr) {
    // We're at the control node, so append instead.
    (*block_it_)->nodes().Add(gap_move);
    node_it_ = (*block_it_)->nodes().end();
  } else {
    DCHECK_NE(node_it_, (*block_it_)->nodes().end());
    node_it_.InsertBefore(gap_move);
  }
}

void StraightForwardRegisterAllocator::Spill(LiveNodeInfo* info) {
  if (info->stack_slot.IsAnyStackSlot()) return;
  AllocateSpillSlot(info);
  if (FLAG_trace_maglev_regalloc) {
    printing_visitor_->os()
        << "spill: " << info->stack_slot << " ← v"
        << graph_labeller()->NodeId(info->node) << std::endl;
  }
  info->node->Spill(compiler::AllocatedOperand::cast(info->stack_slot));
}

void StraightForwardRegisterAllocator::AssignInput(Input& input) {
  compiler::UnallocatedOperand operand =
      compiler::UnallocatedOperand::cast(input.operand());
  LiveNodeInfo* info = &values_[input.node()];
  compiler::AllocatedOperand location = info->allocation();

  switch (operand.extended_policy()) {
    case compiler::UnallocatedOperand::REGISTER_OR_SLOT:
    case compiler::UnallocatedOperand::REGISTER_OR_SLOT_OR_CONSTANT:
      input.SetAllocated(location);
      break;

    case compiler::UnallocatedOperand::FIXED_REGISTER: {
      Register reg = Register::from_code(operand.fixed_register_index());
      input.SetAllocated(ForceAllocate(reg, info));
      break;
    }

    case compiler::UnallocatedOperand::MUST_HAVE_REGISTER:
      if (location.IsAnyRegister()) {
        input.SetAllocated(location);
      } else {
        input.SetAllocated(AllocateRegister(info));
      }
      break;

    case compiler::UnallocatedOperand::FIXED_FP_REGISTER:
    case compiler::UnallocatedOperand::SAME_AS_INPUT:
    case compiler::UnallocatedOperand::NONE:
    case compiler::UnallocatedOperand::MUST_HAVE_SLOT:
      UNREACHABLE();
  }

  compiler::AllocatedOperand allocated =
      compiler::AllocatedOperand::cast(input.operand());
  if (location != allocated) {
    if (FLAG_trace_maglev_regalloc) {
      printing_visitor_->os()
          << "gap move: " << allocated << " ← " << location << std::endl;
    }
    AddMoveBeforeCurrentNode(location, allocated);
  }
}

void StraightForwardRegisterAllocator::SpillRegisters() {
  for (int i = 0; i < kAllocatableGeneralRegisterCount; i++) {
    LiveNodeInfo* info = register_values_[i];
    if (info == nullptr) continue;
    Spill(info);
  }
}

void StraightForwardRegisterAllocator::FreeRegister(int i) {
  register_values_[i] = nullptr;
  free_registers_[free_register_size_++] = i;
}

void StraightForwardRegisterAllocator::SpillAndClearRegisters() {
  for (int i = 0; i < kAllocatableGeneralRegisterCount; i++) {
    LiveNodeInfo* info = register_values_[i];
    if (info == nullptr) continue;
    Spill(info);
    info->reg = Register::no_reg();
    FreeRegister(i);
  }
}

void StraightForwardRegisterAllocator::AllocateSpillSlot(LiveNodeInfo* info) {
  DCHECK(info->stack_slot.IsInvalid());
  uint32_t free_slot;
  if (free_slots_.empty()) {
    free_slot = top_of_stack_++;
  } else {
    free_slot = free_slots_.back();
    free_slots_.pop_back();
  }
  info->stack_slot =
      compiler::AllocatedOperand(compiler::AllocatedOperand::STACK_SLOT,
                                 MachineRepresentation::kTagged, free_slot);
}

RegList StraightForwardRegisterAllocator::GetFreeRegisters(int count) {
  RegList free_registers = {};
  while (count > free_register_size_) FreeSomeRegister();

  for (int i = 0; i < count; i++) {
    Register reg = MapIndexToRegister(free_registers_[--free_register_size_]);
    free_registers = CombineRegLists(free_registers, Register::ListOf(reg));
  }

  return free_registers;
}

void StraightForwardRegisterAllocator::FreeSomeRegister() {
  int furthest_use = 0;
  int longest = -1;
  for (int i = 0; i < kAllocatableGeneralRegisterCount; i++) {
    if (!register_values_[i]) continue;
    int use = register_values_[i]->next_use;
    if (use > furthest_use) {
      furthest_use = use;
      longest = i;
    }
  }
  FreeRegister(longest);
}

compiler::AllocatedOperand StraightForwardRegisterAllocator::AllocateRegister(
    LiveNodeInfo* info) {
  if (free_register_size_ == 0) FreeSomeRegister();
  compiler::InstructionOperand allocation = TryAllocateRegister(info);
  DCHECK(allocation.IsAllocated());
  return compiler::AllocatedOperand::cast(allocation);
}

compiler::AllocatedOperand StraightForwardRegisterAllocator::ForceAllocate(
    const Register& reg, LiveNodeInfo* info) {
  if (register_values_[MapRegisterToIndex(reg)] == nullptr) {
    // If it's already free, remove it from the free list.
    for (int i = 0; i < free_register_size_; i++) {
      if (MapRegisterToIndex(reg) == free_registers_[i]) {
        std::swap(free_registers_[i], free_registers_[--free_register_size_]);
        break;
      }
    }
  } else if (register_values_[MapRegisterToIndex(reg)] == info) {
    return compiler::AllocatedOperand(compiler::LocationOperand::REGISTER,
                                      MachineRepresentation::kTagged,
                                      reg.code());
  } else {
    Free(reg);
    DCHECK_NULL(register_values_[MapRegisterToIndex(reg)]);
  }
#ifdef DEBUG
  for (int i = 0; i < free_register_size_; i++) {
    CHECK_NE(MapRegisterToIndex(reg), free_registers_[i]);
  }
#endif
  SetRegister(reg, info);
  return compiler::AllocatedOperand(compiler::LocationOperand::REGISTER,
                                    MachineRepresentation::kTagged, reg.code());
}

void StraightForwardRegisterAllocator::SetRegister(Register reg,
                                                   LiveNodeInfo* info) {
  int index = MapRegisterToIndex(reg);
  DCHECK_IMPLIES(register_values_[index] != info,
                 register_values_[index] == nullptr);
  register_values_[index] = info;
  info->reg = reg;
}

compiler::InstructionOperand
StraightForwardRegisterAllocator::TryAllocateRegister(LiveNodeInfo* info) {
  if (free_register_size_ == 0) return compiler::InstructionOperand();
  int index = free_registers_[--free_register_size_];

  // Allocation succeeded. This might have found an existing allocation.
  // Simply update the state anyway.
  SetRegister(MapIndexToRegister(index), info);
  return compiler::AllocatedOperand(compiler::LocationOperand::REGISTER,
                                    MachineRepresentation::kTagged,
                                    MapIndexToRegister(index).code());
}

void StraightForwardRegisterAllocator::AssignTemporaries(NodeBase* node) {
  node->assign_temporaries(GetFreeRegisters(node->num_temporaries_needed()));
}

void StraightForwardRegisterAllocator::InitializeRegisterValues(
    RegisterState* target_state) {
  // First clear the register state.
  for (int i = 0; i < kAllocatableGeneralRegisterCount; i++) {
    LiveNodeInfo* info = register_values_[i];
    if (info == nullptr) continue;
    info->reg = Register::no_reg();
  }

  // Mark no register as free.
  free_register_size_ = 0;

  // Then fill it in with target information.
  for (int i = 0; i < kAllocatableGeneralRegisterCount; i++) {
    LiveNodeInfo* node;
    RegisterMerge* merge;
    LoadMergeState(target_state[i], &node, &merge);
    if (node == nullptr) {
      DCHECK(!target_state[i].GetPayload().is_merge);
      FreeRegister(i);
      continue;
    }
    register_values_[i] = node;
    node->reg = MapIndexToRegister(i);
  }
}

void StraightForwardRegisterAllocator::EnsureInRegister(
    RegisterState* target_state, LiveNodeInfo* incoming) {
#ifdef DEBUG
  int i;
  for (i = 0; i < kAllocatableGeneralRegisterCount; i++) {
    LiveNodeInfo* node;
    RegisterMerge* merge;
    LoadMergeState(target_state[i], &node, &merge);
    if (node == incoming) break;
  }
  CHECK_NE(kAllocatableGeneralRegisterCount, i);
#endif
}

void StraightForwardRegisterAllocator::InitializeBranchTargetRegisterValues(
    ControlNode* source, BasicBlock* target) {
  RegisterState* target_state = target->state()->register_state();
  DCHECK(!target_state[0].GetPayload().is_initialized);
  for (int i = 0; i < kAllocatableGeneralRegisterCount; i++) {
    LiveNodeInfo* info = register_values_[i];
    if (!IsLiveAtTarget(info, source, target)) info = nullptr;
    target_state[i] = {info, initialized_node};
  }
}

void StraightForwardRegisterAllocator::MergeRegisterValues(ControlNode* control,
                                                           BasicBlock* target,
                                                           int predecessor_id) {
  RegisterState* target_state = target->state()->register_state();
  if (!target_state[0].GetPayload().is_initialized) {
    // This is the first block we're merging, initialize the values.
    return InitializeBranchTargetRegisterValues(control, target);
  }

  int predecessor_count = target->state()->predecessor_count();
  for (int i = 0; i < kAllocatableGeneralRegisterCount; i++) {
    LiveNodeInfo* node;
    RegisterMerge* merge;
    LoadMergeState(target_state[i], &node, &merge);

    compiler::AllocatedOperand register_info = {
        compiler::LocationOperand::REGISTER, MachineRepresentation::kTagged,
        MapIndexToRegister(i).code()};

    LiveNodeInfo* incoming = register_values_[i];
    if (!IsLiveAtTarget(incoming, control, target)) incoming = nullptr;

    if (incoming == node) {
      // We're using the same register as the target already has. If registers
      // are merged, add input information.
      if (merge) merge->operand(predecessor_id) = register_info;
      continue;
    }

    if (merge) {
      // The register is already occupied with a different node. Figure out
      // where that node is allocated on the incoming branch.
      merge->operand(predecessor_id) = node->allocation();

      // If there's a value in the incoming state, that value is either
      // already spilled or in another place in the merge state.
      if (incoming != nullptr && incoming->stack_slot.IsAnyStackSlot()) {
        EnsureInRegister(target_state, incoming);
      }
      continue;
    }

    DCHECK_IMPLIES(node == nullptr, incoming != nullptr);
    if (node == nullptr && !incoming->stack_slot.IsAnyStackSlot()) {
      // If the register is unallocated at the merge point, and the incoming
      // value isn't spilled, that means we must have seen it already in a
      // different register.
      EnsureInRegister(target_state, incoming);
      continue;
    }

    const size_t size = sizeof(RegisterMerge) +
                        predecessor_count * sizeof(compiler::AllocatedOperand);
    void* buffer = compilation_unit_->zone()->Allocate<void*>(size);
    merge = new (buffer) RegisterMerge();
    merge->node = node == nullptr ? incoming : node;

    // If the register is unallocated at the merge point, allocation so far
    // is the spill slot for the incoming value. Otherwise all incoming
    // branches agree that the current node is in the register info.
    compiler::AllocatedOperand info_so_far =
        node == nullptr ? compiler::AllocatedOperand::cast(incoming->stack_slot)
                        : register_info;

    // Initialize the entire array with info_so_far since we don't know in
    // which order we've seen the predecessors so far. Predecessors we
    // haven't seen yet will simply overwrite their entry later.
    for (int j = 0; j < predecessor_count; j++) {
      merge->operand(j) = info_so_far;
    }
    // If the register is unallocated at the merge point, fill in the
    // incoming value. Otherwise find the merge-point node in the incoming
    // state.
    if (node == nullptr) {
      merge->operand(predecessor_id) = register_info;
    } else {
      merge->operand(predecessor_id) = node->allocation();
    }
    target_state[i] = {merge, initialized_merge};
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
