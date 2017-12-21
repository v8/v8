// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_MIPS64_H_
#define V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_MIPS64_H_

#include "src/wasm/baseline/liftoff-assembler.h"

namespace v8 {
namespace internal {
namespace wasm {

void LiftoffAssembler::ReserveStackSpace(uint32_t bytes) {}

void LiftoffAssembler::LoadConstant(LiftoffRegister reg, WasmValue value) {}

void LiftoffAssembler::LoadFromContext(Register dst, uint32_t offset,
                                       int size) {}

void LiftoffAssembler::SpillContext(Register context) {}

void LiftoffAssembler::Load(LiftoffRegister dst, Register src_addr,
                            Register offset_reg, uint32_t offset_imm,
                            LoadType type, LiftoffRegList pinned) {}

void LiftoffAssembler::Store(Register dst_addr, Register offset_reg,
                             uint32_t offset_imm, LiftoffRegister src,
                             StoreType type, LiftoffRegList pinned) {}

void LiftoffAssembler::LoadCallerFrameSlot(LiftoffRegister dst,
                                           uint32_t caller_slot_idx) {}

void LiftoffAssembler::MoveStackValue(uint32_t dst_index, uint32_t src_index) {}

void LiftoffAssembler::MoveToReturnRegister(LiftoffRegister reg) {}

void LiftoffAssembler::Move(LiftoffRegister dst, LiftoffRegister src) {}

void LiftoffAssembler::Spill(uint32_t index, LiftoffRegister reg) {}

void LiftoffAssembler::Spill(uint32_t index, WasmValue value) {}

void LiftoffAssembler::Fill(LiftoffRegister reg, uint32_t index) {}

#define DEFAULT_I32_BINOP(name, internal_name)                       \
  void LiftoffAssembler::emit_i32_##name(Register dst, Register lhs, \
                                         Register rhs) {}

// clang-format off
DEFAULT_I32_BINOP(add, add)
DEFAULT_I32_BINOP(sub, sub)
DEFAULT_I32_BINOP(mul, imul)
DEFAULT_I32_BINOP(and, and)
DEFAULT_I32_BINOP(or, or)
DEFAULT_I32_BINOP(xor, xor)
// clang-format on

#undef DEFAULT_I32_BINOP

void LiftoffAssembler::emit_f32_add(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {}
void LiftoffAssembler::emit_f32_sub(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {}
void LiftoffAssembler::emit_f32_mul(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {}

void LiftoffAssembler::emit_i32_test(Register reg) {}

void LiftoffAssembler::emit_i32_compare(Register lhs, Register rhs) {}

void LiftoffAssembler::emit_jump(Label* label) {}

void LiftoffAssembler::emit_cond_jump(Condition cond, Label* label) {}

void LiftoffAssembler::StackCheck(Label* ool_code) {}

void LiftoffAssembler::CallTrapCallbackForTesting() {}

void LiftoffAssembler::AssertUnreachable(BailoutReason reason) {}

void LiftoffAssembler::PushRegisters(LiftoffRegList regs) {}

void LiftoffAssembler::PopRegisters(LiftoffRegList regs) {}

void LiftoffAssembler::DropStackSlotsAndRet(uint32_t num_stack_slots) {}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_MIPS64_H_
