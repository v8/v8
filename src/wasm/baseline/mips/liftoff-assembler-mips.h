// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_MIPS_LIFTOFF_ASSEMBLER_MIPS_H_
#define V8_WASM_BASELINE_MIPS_LIFTOFF_ASSEMBLER_MIPS_H_

#include "src/wasm/baseline/liftoff-assembler.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace liftoff {

// sp-8 holds the stack marker, sp-16 is the wasm context, first stack slot
// is located at sp-24.
constexpr int32_t kConstantStackSpace = 16;

inline MemOperand GetContextOperand() { return MemOperand(sp, -16); }

}  // namespace liftoff

void LiftoffAssembler::ReserveStackSpace(uint32_t stack_slots) {
  uint32_t bytes = liftoff::kConstantStackSpace + kStackSlotSize * stack_slots;
  DCHECK_LE(bytes, kMaxInt);
  addiu(sp, sp, -bytes);
}

void LiftoffAssembler::LoadConstant(LiftoffRegister reg, WasmValue value,
                                    RelocInfo::Mode rmode) {
  switch (value.type()) {
    case kWasmI32:
      TurboAssembler::li(reg.gp(), Operand(value.to_i32(), rmode));
      break;
    case kWasmI64: {
      DCHECK(RelocInfo::IsNone(rmode));
      int32_t low_word = value.to_i64();
      int32_t high_word = value.to_i64() >> 32;
      TurboAssembler::li(reg.low_gp(), Operand(low_word));
      TurboAssembler::li(reg.high_gp(), Operand(high_word));
      break;
    }
    case kWasmF32:
      TurboAssembler::Move(reg.fp(), value.to_f32_boxed().get_scalar());
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::LoadFromContext(Register dst, uint32_t offset,
                                       int size) {
  DCHECK_LE(offset, kMaxInt);
  lw(dst, liftoff::GetContextOperand());
  DCHECK_EQ(4, size);
  lw(dst, MemOperand(dst, offset));
}

void LiftoffAssembler::SpillContext(Register context) {
  sw(context, liftoff::GetContextOperand());
}

void LiftoffAssembler::FillContextInto(Register dst) {
  lw(dst, liftoff::GetContextOperand());
}

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
                                           uint32_t caller_slot_idx,
                                           ValueType type) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::MoveStackValue(uint32_t dst_index, uint32_t src_index,
                                      ValueType type) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::MoveToReturnRegister(LiftoffRegister reg,
                                            ValueType type) {
  // TODO(wasm): Extract the destination register from the CallDescriptor.
  // TODO(wasm): Add multi-return support.
  LiftoffRegister dst =
      reg.is_pair()
          ? LiftoffRegister::ForPair(LiftoffRegister(v0), LiftoffRegister(v1))
          : reg.is_gp() ? LiftoffRegister(v0) : LiftoffRegister(f0);
  if (reg != dst) Move(dst, reg, type);
}

void LiftoffAssembler::Move(Register dst, Register src, ValueType type) {
  DCHECK_NE(dst, src);
  TurboAssembler::mov(dst, src);
}

void LiftoffAssembler::Move(DoubleRegister dst, DoubleRegister src,
                            ValueType type) {
  DCHECK_NE(dst, src);
  TurboAssembler::Move(dst, src);
}

void LiftoffAssembler::Spill(uint32_t index, LiftoffRegister reg,
                             ValueType type) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::Spill(uint32_t index, WasmValue value) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::Fill(LiftoffRegister reg, uint32_t index,
                            ValueType type) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::FillI64Half(Register, uint32_t half_index) {
  UNREACHABLE();
}

#define UNIMPLEMENTED_GP_BINOP(name)                             \
  void LiftoffAssembler::emit_##name(Register dst, Register lhs, \
                                     Register rhs) {             \
    UNIMPLEMENTED();                                             \
  }
#define UNIMPLEMENTED_GP_UNOP(name)                                \
  bool LiftoffAssembler::emit_##name(Register dst, Register src) { \
    UNIMPLEMENTED();                                               \
  }
#define UNIMPLEMENTED_FP_BINOP(name)                                         \
  void LiftoffAssembler::emit_##name(DoubleRegister dst, DoubleRegister lhs, \
                                     DoubleRegister rhs) {                   \
    UNIMPLEMENTED();                                                         \
  }
#define UNIMPLEMENTED_SHIFTOP(name)                                            \
  void LiftoffAssembler::emit_##name(Register dst, Register lhs, Register rhs, \
                                     LiftoffRegList pinned) {                  \
    UNIMPLEMENTED();                                                           \
  }

UNIMPLEMENTED_GP_BINOP(i32_add)
UNIMPLEMENTED_GP_BINOP(i32_sub)
UNIMPLEMENTED_GP_BINOP(i32_mul)
UNIMPLEMENTED_GP_BINOP(i32_and)
UNIMPLEMENTED_GP_BINOP(i32_or)
UNIMPLEMENTED_GP_BINOP(i32_xor)
UNIMPLEMENTED_SHIFTOP(i32_shl)
UNIMPLEMENTED_SHIFTOP(i32_sar)
UNIMPLEMENTED_SHIFTOP(i32_shr)
UNIMPLEMENTED_GP_UNOP(i32_clz)
UNIMPLEMENTED_GP_UNOP(i32_ctz)
UNIMPLEMENTED_GP_UNOP(i32_popcnt)
UNIMPLEMENTED_GP_BINOP(ptrsize_add)
UNIMPLEMENTED_FP_BINOP(f32_add)
UNIMPLEMENTED_FP_BINOP(f32_sub)
UNIMPLEMENTED_FP_BINOP(f32_mul)

#undef UNIMPLEMENTED_GP_BINOP
#undef UNIMPLEMENTED_GP_UNOP
#undef UNIMPLEMENTED_FP_BINOP
#undef UNIMPLEMENTED_SHIFTOP

void LiftoffAssembler::emit_i32_test(Register reg) { UNIMPLEMENTED(); }

void LiftoffAssembler::emit_i32_compare(Register lhs, Register rhs) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::emit_ptrsize_compare(Register lhs, Register rhs) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::emit_jump(Label* label) { UNIMPLEMENTED(); }

void LiftoffAssembler::emit_cond_jump(Condition cond, Label* label) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::emit_i32_set_cond(Condition cond, Register dst) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::StackCheck(Label* ool_code) { UNIMPLEMENTED(); }

void LiftoffAssembler::CallTrapCallbackForTesting() { UNIMPLEMENTED(); }

void LiftoffAssembler::AssertUnreachable(AbortReason reason) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::PushCallerFrameSlot(const VarState& src,
                                           uint32_t src_index,
                                           RegPairHalf half) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::PushCallerFrameSlot(LiftoffRegister reg) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::PushRegisters(LiftoffRegList regs) { UNIMPLEMENTED(); }

void LiftoffAssembler::PopRegisters(LiftoffRegList regs) { UNIMPLEMENTED(); }

void LiftoffAssembler::DropStackSlotsAndRet(uint32_t num_stack_slots) {
  DCHECK_LT(num_stack_slots, (1 << 16) / kPointerSize);  // 16 bit immediate
  TurboAssembler::DropAndRet(static_cast<int>(num_stack_slots * kPointerSize));
}

void LiftoffAssembler::PrepareCCall(uint32_t num_params, const Register* args) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::SetCCallRegParamAddr(Register dst, uint32_t param_idx,
                                            uint32_t num_params) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::SetCCallStackParamAddr(uint32_t stack_param_idx,
                                              uint32_t param_idx,
                                              uint32_t num_params) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::CallC(ExternalReference ext_ref, uint32_t num_params) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::CallNativeWasmCode(Address addr) { UNIMPLEMENTED(); }

void LiftoffAssembler::CallRuntime(Zone* zone, Runtime::FunctionId fid) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::CallIndirect(wasm::FunctionSig* sig,
                                    compiler::CallDescriptor* call_desc,
                                    Register target,
                                    uint32_t* max_used_spill_slot) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::AllocateStackSlot(Register addr, uint32_t size) {
  UNIMPLEMENTED();
}

void LiftoffAssembler::DeallocateStackSlot(uint32_t size) { UNIMPLEMENTED(); }

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_BASELINE_MIPS_LIFTOFF_ASSEMBLER_MIPS_H_
