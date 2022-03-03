// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_REGALLOC_H_
#define V8_MAGLEV_MAGLEV_REGALLOC_H_

#include "src/codegen/reglist.h"
#include "src/compiler/backend/instruction.h"
#include "src/maglev/maglev-compilation-data.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir.h"
#include "src/maglev/maglev-regalloc-data.h"

namespace v8 {
namespace internal {
namespace maglev {

class MaglevPrintingVisitor;

class StraightForwardRegisterAllocator {
 public:
  StraightForwardRegisterAllocator(MaglevCompilationUnit* compilation_unit,
                                   Graph* graph);
  ~StraightForwardRegisterAllocator();

  int stack_slots() const { return top_of_stack_; }

 private:
  std::vector<int> future_register_uses_[kAllocatableGeneralRegisterCount];

#define N(V) nullptr,
  ValueNode* register_values_[kAllocatableGeneralRegisterCount] = {
      ALLOCATABLE_GENERAL_REGISTERS(N)};
#undef N

  int top_of_stack_ = 0;
  RegList free_registers_ = kAllocatableGeneralRegisters;
  std::vector<uint32_t> free_slots_;

  void ComputePostDominatingHoles(Graph* graph);
  void AllocateRegisters(Graph* graph);

  void PrintLiveRegs() const;

  void UpdateInputUse(uint32_t use, const Input& input);

  void AllocateControlNode(ControlNode* node, BasicBlock* block);
  void AllocateNode(Node* node);
  void AllocateNodeResult(ValueNode* node);
  void AssignInput(Input& input);
  void AssignTemporaries(NodeBase* node);
  void TryAllocateToInput(Phi* phi);

  void FreeRegisters(ValueNode* node) {
    RegList list = node->ClearRegisters();
    while (list != kEmptyRegList) {
      Register reg = Register::TakeAny(&list);
      FreeRegister(MapRegisterToIndex(reg));
    }
  }
  void FreeRegister(int i);
  void FreeSomeRegister();
  void AddMoveBeforeCurrentNode(compiler::AllocatedOperand source,
                                compiler::AllocatedOperand target);

  void AllocateSpillSlot(ValueNode* node);
  void Spill(ValueNode* node);
  void SpillAndClearRegisters();
  void SpillRegisters();

  compiler::AllocatedOperand AllocateRegister(ValueNode* node);
  compiler::AllocatedOperand ForceAllocate(const Register& reg,
                                           ValueNode* node);
  void SetRegister(Register reg, ValueNode* node);
  void Free(const Register& reg);
  compiler::InstructionOperand TryAllocateRegister(ValueNode* node);

  void InitializeRegisterValues(RegisterState* target_state);
  void EnsureInRegister(RegisterState* target_state, ValueNode* incoming);

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
