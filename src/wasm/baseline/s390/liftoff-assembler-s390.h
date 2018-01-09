// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_S390_H_
#define V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_S390_H_

#include "src/wasm/baseline/liftoff-assembler.h"

namespace v8 {
namespace internal {
namespace wasm {

void LiftoffAssembler::ReserveStackSpace(uint32_t bytes) { UNIMPLEMENTED(); }

void LiftoffAssembler::LoadConstant(LiftoffRegister reg, WasmValue value) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::LoadFromContext(Register dst, uint32_t offset,
                                       int size) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::SpillContext(Register context) { UNIMPLEMENTED(); }

void LiftoffAssembler::Load(LiftoffRegister dst, Register src_addr,
                            Register offset_reg, uint32_t offset_imm,
                            LoadType type, LiftoffRegList pinned,
                            uint32_t* protected_load_pc) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::Store(Register dst_addr, Register offset_reg,
                             uint32_t offset_imm, LiftoffRegister src,
                             StoreType type, LiftoffRegList pinned,
                             uint32_t* protected_store_pc) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::LoadCallerFrameSlot(LiftoffRegister dst,
                                           uint32_t caller_slot_idx) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::MoveStackValue(uint32_t dst_index, uint32_t src_index) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::MoveToReturnRegister(LiftoffRegister reg) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::Move(LiftoffRegister dst, LiftoffRegister src) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::Spill(uint32_t index, LiftoffRegister reg) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::Spill(uint32_t index, WasmValue value) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::Fill(LiftoffRegister reg, uint32_t index) {
  UNIMPLEMENTED();
}

#define DEFAULT_I32_BINOP(name, internal_name)                       \
  void LiftoffAssembler::emit_i32_##name(Register dst, Register lhs, \
                                         Register rhs) {             \
    UNIMPLEMENTED();                                                 \
  }

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
                                    DoubleRegister rhs) {
  UNIMPLEMENTED();
}
void LiftoffAssembler::emit_f32_sub(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  UNIMPLEMENTED();
}
void LiftoffAssembler::emit_f32_mul(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::emit_i32_test(Register reg) { UNIMPLEMENTED(); }

void LiftoffAssembler::emit_i32_compare(Register lhs, Register rhs) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::emit_jump(Label* label) { UNIMPLEMENTED(); }

void LiftoffAssembler::emit_cond_jump(Condition cond, Label* label) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::StackCheck(Label* ool_code) { UNIMPLEMENTED(); }

void LiftoffAssembler::CallTrapCallbackForTesting() { UNIMPLEMENTED(); }

void LiftoffAssembler::AssertUnreachable(AbortReason reason) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::PushRegisters(LiftoffRegList regs) { UNIMPLEMENTED(); }

void LiftoffAssembler::PopRegisters(LiftoffRegList regs) { UNIMPLEMENTED(); }

void LiftoffAssembler::DropStackSlotsAndRet(uint32_t num_stack_slots) {
  UNIMPLEMENTED();
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_S390_H_
