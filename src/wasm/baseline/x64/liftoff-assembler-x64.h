// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_X64_H_
#define V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_X64_H_

#include "src/wasm/baseline/liftoff-assembler.h"

#include "src/assembler.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace liftoff {

inline Operand GetStackSlot(uint32_t index) {
  // rbp-8 holds the stack marker, first stack slot is located at rbp-16.
  return Operand(rbp, -16 - 8 * index);
}

}  // namespace liftoff

void LiftoffAssembler::ReserveStackSpace(uint32_t space) {
  stack_space_ = space;
  subl(rsp, Immediate(space));
}

void LiftoffAssembler::LoadConstant(Register reg, WasmValue value) {
  switch (value.type()) {
    case kWasmI32:
      if (value.to_i32() == 0) {
        xorl(reg, reg);
      } else {
        movl(reg, Immediate(value.to_i32()));
      }
      break;
    default:
      UNIMPLEMENTED();
  }
}

void LiftoffAssembler::Load(Register dst, Address addr,
                            RelocInfo::Mode reloc_mode) {
  movp(dst, bit_cast<void*>(addr), reloc_mode);
  movl(dst, Operand(dst, 0));
}

void LiftoffAssembler::Store(Address addr, Register reg,
                             PinnedRegisterScope pinned_regs,
                             RelocInfo::Mode reloc_mode) {
  // TODO(clemensh): Change this to kPointerSizeT or something.
  Register global_addr_reg = GetUnusedRegister(kWasmI32, pinned_regs);
  DCHECK_NE(reg, global_addr_reg);
  movp(global_addr_reg, static_cast<void*>(addr), reloc_mode);
  movl(Operand(global_addr_reg, 0), reg);
}

void LiftoffAssembler::LoadCallerFrameSlot(Register dst,
                                           uint32_t caller_slot_idx) {
  movl(dst, Operand(rbp, 8 + 8 * caller_slot_idx));
}

void LiftoffAssembler::MoveStackValue(uint32_t dst_index, uint32_t src_index,
                                      wasm::ValueType type) {
  DCHECK_NE(dst_index, src_index);
  DCHECK_EQ(kWasmI32, type);
  if (cache_state_.has_unused_register()) {
    Register reg = GetUnusedRegister(type);
    Fill(reg, src_index);
    Spill(dst_index, reg);
  } else {
    pushq(liftoff::GetStackSlot(src_index));
    popq(liftoff::GetStackSlot(dst_index));
  }
}

void LiftoffAssembler::MoveToReturnRegister(Register reg) {
  // TODO(clemensh): Handle different types here.
  if (reg != rax) movl(rax, reg);
}

void LiftoffAssembler::Spill(uint32_t index, Register reg) {
  // TODO(clemensh): Handle different types here.
  movl(liftoff::GetStackSlot(index), reg);
}

void LiftoffAssembler::Spill(uint32_t index, WasmValue value) {
  // TODO(clemensh): Handle different types here.
  movl(liftoff::GetStackSlot(index), Immediate(value.to_i32()));
}

void LiftoffAssembler::Fill(Register reg, uint32_t index) {
  // TODO(clemensh): Handle different types here.
  movl(reg, liftoff::GetStackSlot(index));
}

void LiftoffAssembler::emit_i32_add(Register dst, Register lhs, Register rhs) {
  if (lhs != dst) {
    leal(dst, Operand(lhs, rhs, times_1, 0));
  } else {
    addl(dst, rhs);
  }
}

void LiftoffAssembler::emit_i32_sub(Register dst, Register lhs, Register rhs) {
  if (dst == rhs) {
    negl(dst);
    addl(dst, lhs);
  } else {
    if (dst != lhs) movl(dst, lhs);
    subl(dst, rhs);
  }
}

#define COMMUTATIVE_I32_BINOP(name, instruction)                     \
  void LiftoffAssembler::emit_i32_##name(Register dst, Register lhs, \
                                         Register rhs) {             \
    if (dst == rhs) {                                                \
      instruction##l(dst, lhs);                                      \
    } else {                                                         \
      if (dst != lhs) movl(dst, lhs);                                \
      instruction##l(dst, rhs);                                      \
    }                                                                \
  }

// clang-format off
COMMUTATIVE_I32_BINOP(mul, imul)
COMMUTATIVE_I32_BINOP(and, and)
COMMUTATIVE_I32_BINOP(or, or)
COMMUTATIVE_I32_BINOP(xor, xor)
// clang-format on

#undef DEFAULT_I32_BINOP

void LiftoffAssembler::JumpIfZero(Register reg, Label* label) {
  testl(reg, reg);
  j(zero, label);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_X64_H_
