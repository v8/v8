// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_ASSEMBLER_INL_H_
#define V8_MAGLEV_MAGLEV_ASSEMBLER_INL_H_

#include "src/codegen/macro-assembler-inl.h"
#include "src/maglev/maglev-assembler.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-code-gen-state.h"

namespace v8 {
namespace internal {
namespace maglev {

void MaglevAssembler::Branch(Condition condition, BasicBlock* if_true,
                             BasicBlock* if_false, BasicBlock* next_block) {
  // We don't have any branch probability information, so try to jump
  // over whatever the next block emitted is.
  if (if_false == next_block) {
    // Jump over the false block if true, otherwise fall through into it.
    j(condition, if_true->label());
  } else {
    // Jump to the false block if true.
    j(NegateCondition(condition), if_false->label());
    // Jump to the true block if it's not the next block.
    if (if_true != next_block) {
      jmp(if_true->label());
    }
  }
}

void MaglevAssembler::PushInput(const Input& input) {
  if (input.operand().IsConstant()) {
    input.node()->LoadToRegister(this, kScratchRegister);
    Push(kScratchRegister);
  } else {
    // TODO(leszeks): Consider special casing the value. (Toon: could possibly
    // be done through Input directly?)
    const compiler::AllocatedOperand& operand =
        compiler::AllocatedOperand::cast(input.operand());

    if (operand.IsRegister()) {
      Push(operand.GetRegister());
    } else {
      DCHECK(operand.IsStackSlot());
      Push(GetStackSlot(operand));
    }
  }
}

Register MaglevAssembler::FromAnyToRegister(const Input& input,
                                            Register scratch) {
  if (input.operand().IsConstant()) {
    input.node()->LoadToRegister(this, scratch);
    return scratch;
  }
  const compiler::AllocatedOperand& operand =
      compiler::AllocatedOperand::cast(input.operand());
  if (operand.IsRegister()) {
    return ToRegister(input);
  } else {
    DCHECK(operand.IsStackSlot());
    movq(scratch, ToMemOperand(input));
    return scratch;
  }
}

inline void MaglevAssembler::DefineLazyDeoptPoint(LazyDeoptInfo* info) {
  info->deopting_call_return_pc = pc_offset_for_safepoint();
  code_gen_state()->PushLazyDeopt(info);
  safepoint_table_builder()->DefineSafepoint(this);
}

inline void MaglevAssembler::DefineExceptionHandlerPoint(NodeBase* node) {
  ExceptionHandlerInfo* info = node->exception_handler_info();
  if (!info->HasExceptionHandler()) return;
  info->pc_offset = pc_offset_for_safepoint();
  code_gen_state()->PushHandlerInfo(node);
}

inline void MaglevAssembler::DefineExceptionHandlerAndLazyDeoptPoint(
    NodeBase* node) {
  DefineExceptionHandlerPoint(node);
  DefineLazyDeoptPoint(node->lazy_deopt_info());
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_ASSEMBLER_INL_H_
