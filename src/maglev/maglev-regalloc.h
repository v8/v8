// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_REGALLOC_H_
#define V8_MAGLEV_MAGLEV_REGALLOC_H_

#include "src/maglev/maglev-compilation-data.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir.h"
#include "src/maglev/maglev-regalloc-data.h"

namespace v8 {
namespace internal {
namespace maglev {

class MaglevPrintingVisitor;

struct StackSlot {
  StackSlot(MachineRepresentation representation, int index)
      : slot(compiler::LocationOperand::STACK_SLOT, representation, index) {}
  compiler::AllocatedOperand slot;
  StackSlot* next_ = nullptr;
  StackSlot** next() { return &next_; }
};

struct LiveNodeInfo {
  ValueNode* node;
  uint32_t last_use = 0;
  uint32_t next_use = 0;
  StackSlot* stack_slot = nullptr;
  Register reg = Register::no_reg();

  compiler::AllocatedOperand allocation() const {
    if (reg.is_valid()) {
      return compiler::AllocatedOperand(compiler::LocationOperand::REGISTER,
                                        MachineRepresentation::kTagged,
                                        reg.code());
    }
    DCHECK_NOT_NULL(stack_slot);
    return stack_slot->slot;
  }
};

class StraightForwardRegisterAllocator {
 public:
  StraightForwardRegisterAllocator(MaglevCompilationUnit* compilation_unit,
                                   Graph* graph);
  ~StraightForwardRegisterAllocator();

  int stack_slots() const { return top_of_stack_; }

 private:
  std::vector<int> future_register_uses_[kAllocatableGeneralRegisterCount];

  // Currently live values.
  using LiveNodeInfoMap = std::map<ValueNode*, LiveNodeInfo>;
  LiveNodeInfoMap values_;

  base::ThreadedList<StackSlot> free_slots_;
  int top_of_stack_ = 0;
#define N(V) nullptr,
  LiveNodeInfo* register_values_[kAllocatableGeneralRegisterCount] = {
      ALWAYS_ALLOCATABLE_GENERAL_REGISTERS(N)};
#undef N

  LiveNodeInfo* MakeLive(ValueNode* node) {
    uint32_t last_use = node->live_range().end;
    // TODO(verwaest): We don't currently have next_use info...
    uint32_t next_use = node->next_use();
    return &(values_[node] = {node, last_use, next_use});
  }

  void ComputePostDominatingHoles(Graph* graph);
  void AllocateRegisters(Graph* graph);

  void PrintLiveRegs() const;

  class InputsUpdater;

  void AllocateControlNode(ControlNode* node, BasicBlock* block);
  void AllocateNode(Node* node);
  void AllocateNodeResult(ValueNode* node);
  void AssignInput(Input& input);
  void AssignTemporaries(NodeBase* node);
  void TryAllocateToInput(LiveNodeInfo* info, Phi* phi);

  RegList GetFreeRegisters(int count);
  void AddMoveBeforeCurrentNode(compiler::AllocatedOperand source,
                                compiler::AllocatedOperand target);

  void AllocateSpillSlot(LiveNodeInfo* info);
  void Spill(LiveNodeInfo* info);
  void SpillAndClearRegisters();
  void SpillRegisters();

  compiler::AllocatedOperand AllocateRegister(LiveNodeInfo* info);
  compiler::AllocatedOperand ForceAllocate(const Register& reg,
                                           LiveNodeInfo* info,
                                           bool try_move = true);
  compiler::AllocatedOperand DoAllocate(const Register& reg,
                                        LiveNodeInfo* info);
  void SetRegister(Register reg, LiveNodeInfo* info);
  void Free(const Register& reg, bool try_move);
  compiler::InstructionOperand TryAllocateRegister(LiveNodeInfo* info);

  void InitializeRegisterValues(RegisterState* target_state);
  void EnsureInRegister(RegisterState* target_state, LiveNodeInfo* incoming);

  void InitializeBranchTargetRegisterValues(ControlNode* source,
                                            BasicBlock* target);
  void InitializeConditionalBranchRegisters(ConditionalControlNode* source,
                                            BasicBlock* target);
  void MergeRegisterValues(ControlNode* control, BasicBlock* target,
                           int predecessor_id);

  MaglevGraphLabeller* graph_labeller() const {
    return compilation_unit_->graph_labeller();
  }

  MaglevCompilationUnit* compilation_unit_;
  std::unique_ptr<MaglevPrintingVisitor> printing_visitor_;
  BlockConstIterator block_it_;
  NodeIterator node_it_;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_REGALLOC_H_
