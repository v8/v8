// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_VREG_ALLOCATOR_H_
#define V8_MAGLEV_MAGLEV_VREG_ALLOCATOR_H_

#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

class ProcessingState;

class MaglevVregAllocationState {
 public:
  int AllocateVirtualRegister() { return next_virtual_register_++; }
  int num_allocated_registers() const { return next_virtual_register_; }

 private:
  int next_virtual_register_ = 0;
};

class MaglevVregAllocator {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {
    for (BasicBlock* block : *graph) {
      if (!block->has_phi()) continue;
      for (Phi* phi : *block->phis()) {
        phi->AllocateVregInPostProcess(&state_);
      }
    }
  }
  void PreProcessBasicBlock(BasicBlock* block) {}

#define DEF_PROCESS_NODE(NAME)                             \
  void Process(NAME* node, const ProcessingState& state) { \
    node->AllocateVreg(&state_);                           \
  }
  NODE_BASE_LIST(DEF_PROCESS_NODE)
#undef DEF_PROCESS_NODE

 private:
  MaglevVregAllocationState state_;
};

// ---
// Vreg allocation helpers.
// ---

inline int GetVirtualRegister(Node* node) {
  return compiler::UnallocatedOperand::cast(node->result().operand())
      .virtual_register();
}

inline void DefineAsRegister(MaglevVregAllocationState* vreg_state,
                             Node* node) {
  node->result().SetUnallocated(
      compiler::UnallocatedOperand::MUST_HAVE_REGISTER,
      vreg_state->AllocateVirtualRegister());
}
inline void DefineAsConstant(MaglevVregAllocationState* vreg_state,
                             Node* node) {
  node->result().SetUnallocated(compiler::UnallocatedOperand::NONE,
                                vreg_state->AllocateVirtualRegister());
}

inline void DefineAsFixed(MaglevVregAllocationState* vreg_state, Node* node,
                          Register reg) {
  node->result().SetUnallocated(compiler::UnallocatedOperand::FIXED_REGISTER,
                                reg.code(),
                                vreg_state->AllocateVirtualRegister());
}

inline void DefineSameAsFirst(MaglevVregAllocationState* vreg_state,
                              Node* node) {
  node->result().SetUnallocated(vreg_state->AllocateVirtualRegister(), 0);
}

inline void UseRegister(Input& input) {
  input.SetUnallocated(compiler::UnallocatedOperand::MUST_HAVE_REGISTER,
                       compiler::UnallocatedOperand::USED_AT_END,
                       GetVirtualRegister(input.node()));
}
inline void UseAndClobberRegister(Input& input) {
  input.SetUnallocated(compiler::UnallocatedOperand::MUST_HAVE_REGISTER,
                       compiler::UnallocatedOperand::USED_AT_START,
                       GetVirtualRegister(input.node()));
}
inline void UseAny(Input& input) {
  input.SetUnallocated(
      compiler::UnallocatedOperand::REGISTER_OR_SLOT_OR_CONSTANT,
      compiler::UnallocatedOperand::USED_AT_END,
      GetVirtualRegister(input.node()));
}
inline void UseFixed(Input& input, Register reg) {
  input.SetUnallocated(compiler::UnallocatedOperand::FIXED_REGISTER, reg.code(),
                       GetVirtualRegister(input.node()));
}
inline void UseFixed(Input& input, DoubleRegister reg) {
  input.SetUnallocated(compiler::UnallocatedOperand::FIXED_FP_REGISTER,
                       reg.code(), GetVirtualRegister(input.node()));
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_VREG_ALLOCATOR_H_
