// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_ARM64_LIFTOFF_ASSEMBLER_ARM64_H_
#define V8_WASM_BASELINE_ARM64_LIFTOFF_ASSEMBLER_ARM64_H_

#include "src/wasm/baseline/liftoff-assembler.h"

#define BAILOUT(reason) bailout("arm64 " reason)

namespace v8 {
namespace internal {
namespace wasm {

namespace liftoff {

// Liftoff Frames.
//
//  slot      Frame
//       +--------------------+---------------------------
//  n+4  | optional padding slot to keep the stack 16 byte aligned.
//  n+3  |   parameter n      |
//  ...  |       ...          |
//   4   |   parameter 1      | or parameter 2
//   3   |   parameter 0      | or parameter 1
//   2   |  (result address)  | or parameter 0
//  -----+--------------------+---------------------------
//   1   | return addr (lr)   |
//   0   | previous frame (fp)|
//  -----+--------------------+  <-- frame ptr (fp)
//  -1   | 0xa: WASM_COMPILED |
//  -2   |     instance       |
//  -----+--------------------+---------------------------
//  -3   |     slot 0         |   ^
//  -4   |     slot 1         |   |
//       |                    | Frame slots
//       |                    |   |
//       |                    |   v
//       | optional padding slot to keep the stack 16 byte aligned.
//  -----+--------------------+  <-- stack ptr (sp)
//

constexpr int32_t kInstanceOffset = 2 * kPointerSize;
constexpr int32_t kFirstStackSlotOffset = kInstanceOffset + kPointerSize;
constexpr int32_t kConstantStackSpace = 0;

inline MemOperand GetStackSlot(uint32_t index) {
  int32_t offset =
      kFirstStackSlotOffset + index * LiftoffAssembler::kStackSlotSize;
  return MemOperand(fp, -offset);
}

inline MemOperand GetInstanceOperand() {
  return MemOperand(fp, -kInstanceOffset);
}

inline CPURegister GetRegFromType(const LiftoffRegister& reg, ValueType type) {
  switch (type) {
    case kWasmI32:
      return reg.gp().W();
    case kWasmI64:
      return reg.gp().X();
    case kWasmF32:
      return reg.fp().S();
    case kWasmF64:
      return reg.fp().D();
    default:
      UNREACHABLE();
  }
}

inline CPURegList PadRegList(RegList list) {
  if ((base::bits::CountPopulation(list) & 1) != 0) list |= padreg.bit();
  return CPURegList(CPURegister::kRegister, kXRegSizeInBits, list);
}

inline CPURegList PadVRegList(RegList list) {
  if ((base::bits::CountPopulation(list) & 1) != 0) list |= fp_scratch.bit();
  return CPURegList(CPURegister::kVRegister, kDRegSizeInBits, list);
}

inline CPURegister AcquireByType(UseScratchRegisterScope* temps,
                                 ValueType type) {
  switch (type) {
    case kWasmI32:
      return temps->AcquireW();
    case kWasmI64:
      return temps->AcquireX();
    case kWasmF32:
      return temps->AcquireS();
    case kWasmF64:
      return temps->AcquireD();
    default:
      UNREACHABLE();
  }
}

}  // namespace liftoff

uint32_t LiftoffAssembler::PrepareStackFrame() {
  uint32_t offset = static_cast<uint32_t>(pc_offset());
  InstructionAccurateScope scope(this, 1);
  sub(sp, sp, 0);
  return offset;
}

void LiftoffAssembler::PatchPrepareStackFrame(uint32_t offset,
                                              uint32_t stack_slots) {
  static_assert(kStackSlotSize == kXRegSize,
                "kStackSlotSize must equal kXRegSize");
  uint32_t bytes = liftoff::kConstantStackSpace + kStackSlotSize * stack_slots;
  // The stack pointer is required to be quadword aligned.
  // Misalignment will cause a stack alignment fault.
  bytes = RoundUp(bytes, kQuadWordSizeInBytes);
  if (!IsImmAddSub(bytes)) {
    // Round the stack to a page to try to fit a add/sub immediate.
    bytes = RoundUp(bytes, 0x1000);
    if (!IsImmAddSub(bytes)) {
      // Stack greater than 4M! Because this is a quite improbable case, we
      // just fallback to Turbofan.
      BAILOUT("Stack too big");
      return;
    }
  }
#ifdef USE_SIMULATOR
  // When using the simulator, deal with Liftoff which allocates the stack
  // before checking it.
  // TODO(arm): Remove this when the stack check mechanism will be updated.
  if (bytes > KB / 2) {
    BAILOUT("Stack limited to 512 bytes to avoid a bug in StackCheck");
    return;
  }
#endif
  PatchingAssembler patching_assembler(IsolateData(isolate()), buffer_ + offset,
                                       1);
  patching_assembler.PatchSubSp(bytes);
}

void LiftoffAssembler::FinishCode() { CheckConstPool(true, false); }

void LiftoffAssembler::LoadConstant(LiftoffRegister reg, WasmValue value,
                                    RelocInfo::Mode rmode) {
  BAILOUT("LoadConstant");
}

void LiftoffAssembler::LoadFromInstance(Register dst, uint32_t offset,
                                        int size) {
  BAILOUT("LoadFromInstance");
}

void LiftoffAssembler::SpillInstance(Register instance) {
  Str(instance, liftoff::GetInstanceOperand());
}

void LiftoffAssembler::FillInstanceInto(Register dst) {
  BAILOUT("FillInstanceInto");
}

void LiftoffAssembler::Load(LiftoffRegister dst, Register src_addr,
                            Register offset_reg, uint32_t offset_imm,
                            LoadType type, LiftoffRegList pinned,
                            uint32_t* protected_load_pc, bool is_load_mem) {
  BAILOUT("Load");
}

void LiftoffAssembler::Store(Register dst_addr, Register offset_reg,
                             uint32_t offset_imm, LiftoffRegister src,
                             StoreType type, LiftoffRegList pinned,
                             uint32_t* protected_store_pc, bool is_store_mem) {
  BAILOUT("Store");
}

void LiftoffAssembler::ChangeEndiannessLoad(LiftoffRegister dst, LoadType type,
                                            LiftoffRegList pinned) {
  BAILOUT("ChangeEndiannessLoad");
}

void LiftoffAssembler::ChangeEndiannessStore(LiftoffRegister src,
                                             StoreType type,
                                             LiftoffRegList pinned) {
  BAILOUT("ChangeEndiannessStore");
}

void LiftoffAssembler::LoadCallerFrameSlot(LiftoffRegister dst,
                                           uint32_t caller_slot_idx,
                                           ValueType type) {
  BAILOUT("LoadCallerFrameSlot");
}

void LiftoffAssembler::MoveStackValue(uint32_t dst_index, uint32_t src_index,
                                      ValueType type) {
  BAILOUT("MoveStackValue");
}

void LiftoffAssembler::MoveToReturnRegister(LiftoffRegister reg,
                                            ValueType type) {
  if (reg.is_gp()) {
    Move(x0, reg.gp(), type);
  } else {
    Move(d0, reg.fp(), type);
  }
}

void LiftoffAssembler::Move(Register dst, Register src, ValueType type) {
  if (type == kWasmI32) {
    Mov(dst.W(), src.W());
  } else {
    DCHECK_EQ(kWasmI64, type);
    Mov(dst.X(), src.X());
  }
}

void LiftoffAssembler::Move(DoubleRegister dst, DoubleRegister src,
                            ValueType type) {
  if (type == kWasmF32) {
    Fmov(dst.S(), src.S());
  } else {
    DCHECK_EQ(kWasmF64, type);
    Fmov(dst.D(), src.D());
  }
}

void LiftoffAssembler::Spill(uint32_t index, LiftoffRegister reg,
                             ValueType type) {
  RecordUsedSpillSlot(index);
  MemOperand dst = liftoff::GetStackSlot(index);
  Str(liftoff::GetRegFromType(reg, type), dst);
}

void LiftoffAssembler::Spill(uint32_t index, WasmValue value) {
  BAILOUT("Spill value");
}

void LiftoffAssembler::Fill(LiftoffRegister reg, uint32_t index,
                            ValueType type) {
  BAILOUT("Fill");
}

void LiftoffAssembler::FillI64Half(Register, uint32_t half_index) {
  BAILOUT("FillI64Half");
}

#define UNIMPLEMENTED_GP_BINOP(name)                             \
  void LiftoffAssembler::emit_##name(Register dst, Register lhs, \
                                     Register rhs) {             \
    BAILOUT("gp binop: " #name);                                 \
  }
#define UNIMPLEMENTED_I64_BINOP(name)                                          \
  void LiftoffAssembler::emit_##name(LiftoffRegister dst, LiftoffRegister lhs, \
                                     LiftoffRegister rhs) {                    \
    BAILOUT("i64 binop: " #name);                                              \
  }
#define UNIMPLEMENTED_GP_UNOP(name)                                \
  bool LiftoffAssembler::emit_##name(Register dst, Register src) { \
    BAILOUT("gp unop: " #name);                                    \
    return true;                                                   \
  }
#define UNIMPLEMENTED_FP_BINOP(name)                                         \
  void LiftoffAssembler::emit_##name(DoubleRegister dst, DoubleRegister lhs, \
                                     DoubleRegister rhs) {                   \
    BAILOUT("fp binop: " #name);                                             \
  }
#define UNIMPLEMENTED_FP_UNOP(name)                                            \
  void LiftoffAssembler::emit_##name(DoubleRegister dst, DoubleRegister src) { \
    BAILOUT("fp unop: " #name);                                                \
  }
#define UNIMPLEMENTED_I32_SHIFTOP(name)                                        \
  void LiftoffAssembler::emit_##name(Register dst, Register src,               \
                                     Register amount, LiftoffRegList pinned) { \
    BAILOUT("i32 shiftop: " #name);                                            \
  }
#define UNIMPLEMENTED_I64_SHIFTOP(name)                                        \
  void LiftoffAssembler::emit_##name(LiftoffRegister dst, LiftoffRegister src, \
                                     Register amount, LiftoffRegList pinned) { \
    BAILOUT("i64 shiftop: " #name);                                            \
  }

UNIMPLEMENTED_GP_BINOP(i32_add)
UNIMPLEMENTED_GP_BINOP(i32_sub)
UNIMPLEMENTED_GP_BINOP(i32_mul)
UNIMPLEMENTED_GP_BINOP(i32_and)
UNIMPLEMENTED_GP_BINOP(i32_or)
UNIMPLEMENTED_GP_BINOP(i32_xor)
UNIMPLEMENTED_I32_SHIFTOP(i32_shl)
UNIMPLEMENTED_I32_SHIFTOP(i32_sar)
UNIMPLEMENTED_I32_SHIFTOP(i32_shr)
UNIMPLEMENTED_I64_BINOP(i64_add)
UNIMPLEMENTED_I64_BINOP(i64_sub)
UNIMPLEMENTED_I64_BINOP(i64_mul)
UNIMPLEMENTED_I64_BINOP(i64_and)
UNIMPLEMENTED_I64_BINOP(i64_or)
UNIMPLEMENTED_I64_BINOP(i64_xor)
UNIMPLEMENTED_I64_SHIFTOP(i64_shl)
UNIMPLEMENTED_I64_SHIFTOP(i64_sar)
UNIMPLEMENTED_I64_SHIFTOP(i64_shr)
UNIMPLEMENTED_GP_UNOP(i32_clz)
UNIMPLEMENTED_GP_UNOP(i32_ctz)
UNIMPLEMENTED_GP_UNOP(i32_popcnt)
UNIMPLEMENTED_FP_BINOP(f32_add)
UNIMPLEMENTED_FP_BINOP(f32_sub)
UNIMPLEMENTED_FP_BINOP(f32_mul)
UNIMPLEMENTED_FP_BINOP(f32_div)
UNIMPLEMENTED_FP_UNOP(f32_abs)
UNIMPLEMENTED_FP_UNOP(f32_neg)
UNIMPLEMENTED_FP_UNOP(f32_ceil)
UNIMPLEMENTED_FP_UNOP(f32_floor)
UNIMPLEMENTED_FP_UNOP(f32_trunc)
UNIMPLEMENTED_FP_UNOP(f32_nearest_int)
UNIMPLEMENTED_FP_UNOP(f32_sqrt)
UNIMPLEMENTED_FP_BINOP(f64_add)
UNIMPLEMENTED_FP_BINOP(f64_sub)
UNIMPLEMENTED_FP_BINOP(f64_mul)
UNIMPLEMENTED_FP_BINOP(f64_div)
UNIMPLEMENTED_FP_UNOP(f64_abs)
UNIMPLEMENTED_FP_UNOP(f64_neg)
UNIMPLEMENTED_FP_UNOP(f64_ceil)
UNIMPLEMENTED_FP_UNOP(f64_floor)
UNIMPLEMENTED_FP_UNOP(f64_trunc)
UNIMPLEMENTED_FP_UNOP(f64_nearest_int)
UNIMPLEMENTED_FP_UNOP(f64_sqrt)

#undef UNIMPLEMENTED_GP_BINOP
#undef UNIMPLEMENTED_I64_BINOP
#undef UNIMPLEMENTED_GP_UNOP
#undef UNIMPLEMENTED_FP_BINOP
#undef UNIMPLEMENTED_FP_UNOP
#undef UNIMPLEMENTED_I32_SHIFTOP
#undef UNIMPLEMENTED_I64_SHIFTOP

void LiftoffAssembler::emit_i32_divs(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  BAILOUT("i32_divs");
}

void LiftoffAssembler::emit_i32_divu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  BAILOUT("i32_divu");
}

void LiftoffAssembler::emit_i32_rems(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  BAILOUT("i32_rems");
}

void LiftoffAssembler::emit_i32_remu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  BAILOUT("i32_remu");
}

bool LiftoffAssembler::emit_type_conversion(WasmOpcode opcode,
                                            LiftoffRegister dst,
                                            LiftoffRegister src, Label* trap) {
  BAILOUT("emit_type_conversion");
  return true;
}

void LiftoffAssembler::emit_jump(Label* label) { B(label); }

void LiftoffAssembler::emit_jump(Register target) { BAILOUT("emit_jump"); }

void LiftoffAssembler::emit_cond_jump(Condition cond, Label* label,
                                      ValueType type, Register lhs,
                                      Register rhs) {
  BAILOUT("emit_cond_jump");
}

void LiftoffAssembler::emit_i32_eqz(Register dst, Register src) {
  Cmp(src.W(), wzr);
  Cset(dst.W(), eq);
}

void LiftoffAssembler::emit_i32_set_cond(Condition cond, Register dst,
                                         Register lhs, Register rhs) {
  Cmp(lhs.W(), rhs.W());
  Cset(dst.W(), cond);
}

void LiftoffAssembler::emit_i64_eqz(Register dst, LiftoffRegister src) {
  BAILOUT("emit_i64_eqz");
}

void LiftoffAssembler::emit_i64_set_cond(Condition cond, Register dst,
                                         LiftoffRegister lhs,
                                         LiftoffRegister rhs) {
  BAILOUT("emit_i64_set_cond");
}

void LiftoffAssembler::emit_f32_set_cond(Condition cond, Register dst,
                                         DoubleRegister lhs,
                                         DoubleRegister rhs) {
  BAILOUT("emit_f32_set_cond");
}

void LiftoffAssembler::emit_f64_set_cond(Condition cond, Register dst,
                                         DoubleRegister lhs,
                                         DoubleRegister rhs) {
  BAILOUT("emit_f64_set_cond");
}

void LiftoffAssembler::StackCheck(Label* ool_code) {
  ExternalReference stack_limit =
      ExternalReference::address_of_stack_limit(isolate());
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  Mov(scratch, Operand(stack_limit));
  Ldr(scratch, MemOperand(scratch));
  Cmp(sp, scratch);
  B(ool_code, ls);
}

void LiftoffAssembler::CallTrapCallbackForTesting() {
  BAILOUT("CallTrapCallbackForTesting");
}

void LiftoffAssembler::AssertUnreachable(AbortReason reason) {
  BAILOUT("AssertUnreachable");
}

void LiftoffAssembler::PushRegisters(LiftoffRegList regs) {
  PushCPURegList(liftoff::PadRegList(regs.GetGpList()));
  PushCPURegList(liftoff::PadVRegList(regs.GetFpList()));
}

void LiftoffAssembler::PopRegisters(LiftoffRegList regs) {
  PopCPURegList(liftoff::PadVRegList(regs.GetFpList()));
  PopCPURegList(liftoff::PadRegList(regs.GetGpList()));
}

void LiftoffAssembler::DropStackSlotsAndRet(uint32_t num_stack_slots) {
  DropSlots(num_stack_slots);
  Ret();
}

void LiftoffAssembler::CallC(wasm::FunctionSig* sig,
                             const LiftoffRegister* args,
                             const LiftoffRegister* rets,
                             ValueType out_argument_type, int stack_bytes,
                             ExternalReference ext_ref) {
  BAILOUT("CallC");
}

void LiftoffAssembler::CallNativeWasmCode(Address addr) {
  BAILOUT("CallNativeWasmCode");
}

void LiftoffAssembler::CallRuntime(Zone* zone, Runtime::FunctionId fid) {
  BAILOUT("CallRuntime");
}

void LiftoffAssembler::CallIndirect(wasm::FunctionSig* sig,
                                    compiler::CallDescriptor* call_descriptor,
                                    Register target) {
  BAILOUT("CallIndirect");
}

void LiftoffAssembler::AllocateStackSlot(Register addr, uint32_t size) {
  BAILOUT("AllocateStackSlot");
}

void LiftoffAssembler::DeallocateStackSlot(uint32_t size) {
  BAILOUT("DeallocateStackSlot");
}

void LiftoffStackSlots::Construct() {
  size_t slot_count = slots_.size();
  // The stack pointer is required to be quadword aligned.
  asm_->Claim(RoundUp(slot_count, 2));
  size_t slot_index = 0;
  for (auto& slot : slots_) {
    size_t poke_offset = (slot_count - slot_index - 1) * kXRegSize;
    switch (slot.src_.loc()) {
      case LiftoffAssembler::VarState::kStack: {
        UseScratchRegisterScope temps(asm_);
        CPURegister scratch = liftoff::AcquireByType(&temps, slot.src_.type());
        asm_->Ldr(scratch, liftoff::GetStackSlot(slot.src_index_));
        asm_->Poke(scratch, poke_offset);
        break;
      }
      case LiftoffAssembler::VarState::kRegister:
        asm_->Poke(liftoff::GetRegFromType(slot.src_.reg(), slot.src_.type()),
                   poke_offset);
        break;
      case LiftoffAssembler::VarState::KIntConst: {
        UseScratchRegisterScope temps(asm_);
        Register scratch = temps.AcquireW();
        asm_->Mov(scratch, slot.src_.i32_const());
        asm_->Poke(scratch, poke_offset);
        break;
      }
    }
    slot_index++;
  }
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#undef BAILOUT

#endif  // V8_WASM_BASELINE_ARM64_LIFTOFF_ASSEMBLER_ARM64_H_
