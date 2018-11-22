// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_ARM_LIFTOFF_ASSEMBLER_ARM_H_
#define V8_WASM_BASELINE_ARM_LIFTOFF_ASSEMBLER_ARM_H_

#include "src/wasm/baseline/liftoff-assembler.h"

#define BAILOUT(reason) bailout("arm " reason)

namespace v8 {
namespace internal {
namespace wasm {

namespace liftoff {

//  half
//  slot        Frame
//  -----+--------------------+---------------------------
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
//  -3   |    slot 0 (high)   |   ^
//  -4   |    slot 0 (low)    |   |
//  -5   |    slot 1 (high)   | Frame slots
//  -6   |    slot 1 (low)    |   |
//       |                    |   v
//  -----+--------------------+  <-- stack ptr (sp)
//
static_assert(2 * kPointerSize == LiftoffAssembler::kStackSlotSize,
              "Slot size should be twice the size of the 32 bit pointer.");
constexpr int32_t kInstanceOffset = 2 * kPointerSize;
constexpr int32_t kFirstStackSlotOffset = kInstanceOffset + 2 * kPointerSize;
constexpr int32_t kConstantStackSpace = kPointerSize;
// kPatchInstructionsRequired sets a maximum limit of how many instructions that
// PatchPrepareStackFrame will use in order to increase the stack appropriately.
// Three instructions are required to sub a large constant, movw + movt + sub.
constexpr int32_t kPatchInstructionsRequired = 3;

inline MemOperand GetStackSlot(uint32_t index) {
  int32_t offset =
      kFirstStackSlotOffset + index * LiftoffAssembler::kStackSlotSize;
  return MemOperand(fp, -offset);
}

inline MemOperand GetHalfStackSlot(uint32_t half_index) {
  int32_t offset = kFirstStackSlotOffset +
                   half_index * (LiftoffAssembler::kStackSlotSize / 2);
  return MemOperand(fp, -offset);
}

inline MemOperand GetHalfStackSlot(uint32_t index, RegPairHalf half) {
  if (half == kLowWord) {
    return GetHalfStackSlot(2 * index);
  } else {
    return GetHalfStackSlot(2 * index - 1);
  }
}

inline MemOperand GetInstanceOperand() {
  return MemOperand(fp, -kInstanceOffset);
}

inline MemOperand GetMemOp(LiftoffAssembler* assm,
                           UseScratchRegisterScope* temps, Register addr,
                           Register offset, int32_t offset_imm) {
  if (offset != no_reg) {
    if (offset_imm == 0) return MemOperand(addr, offset);
    Register tmp = temps->Acquire();
    assm->add(tmp, offset, Operand(offset_imm));
    return MemOperand(addr, tmp);
  }
  return MemOperand(addr, offset_imm);
}

inline Register CalculateActualAddress(LiftoffAssembler* assm,
                                       UseScratchRegisterScope* temps,
                                       Register addr_reg, Register offset_reg,
                                       int32_t offset_imm) {
  if (offset_reg == no_reg && offset_imm == 0) {
    return addr_reg;
  }
  Register actual_addr_reg = temps->Acquire();
  if (offset_reg == no_reg) {
    assm->add(actual_addr_reg, addr_reg, Operand(offset_imm));
  } else {
    assm->add(actual_addr_reg, addr_reg, Operand(offset_reg));
    if (offset_imm != 0) {
      assm->add(actual_addr_reg, actual_addr_reg, Operand(offset_imm));
    }
  }
  return actual_addr_reg;
}

}  // namespace liftoff

int LiftoffAssembler::PrepareStackFrame() {
  if (!CpuFeatures::IsSupported(ARMv7)) {
    BAILOUT("Armv6 not supported");
    return 0;
  }
  uint32_t offset = static_cast<uint32_t>(pc_offset());
  // PatchPrepareStackFrame will patch this in order to increase the stack
  // appropriately. Additional nops are required as the bytes operand might
  // require extra moves to encode.
  for (int i = 0; i < liftoff::kPatchInstructionsRequired; i++) {
    nop();
  }
  DCHECK_EQ(offset + liftoff::kPatchInstructionsRequired * kInstrSize,
            pc_offset());
  return offset;
}

void LiftoffAssembler::PatchPrepareStackFrame(int offset,
                                              uint32_t stack_slots) {
  // Allocate space for instance plus what is needed for the frame slots.
  uint32_t bytes = liftoff::kConstantStackSpace + kStackSlotSize * stack_slots;
#ifdef USE_SIMULATOR
  // When using the simulator, deal with Liftoff which allocates the stack
  // before checking it.
  // TODO(arm): Remove this when the stack check mechanism will be updated.
  if (bytes > KB / 2) {
    BAILOUT("Stack limited to 512 bytes to avoid a bug in StackCheck");
    return;
  }
#endif
  PatchingAssembler patching_assembler(AssemblerOptions{}, buffer_ + offset,
                                       liftoff::kPatchInstructionsRequired);
  patching_assembler.sub(sp, sp, Operand(bytes));
  patching_assembler.PadWithNops();
}

void LiftoffAssembler::FinishCode() { CheckConstPool(true, false); }

void LiftoffAssembler::AbortCompilation() { AbortedCodeGeneration(); }

void LiftoffAssembler::LoadConstant(LiftoffRegister reg, WasmValue value,
                                    RelocInfo::Mode rmode) {
  switch (value.type()) {
    case kWasmI32:
      TurboAssembler::Move(reg.gp(), Operand(value.to_i32(), rmode));
      break;
    case kWasmI64: {
      DCHECK(RelocInfo::IsNone(rmode));
      int32_t low_word = value.to_i64();
      int32_t high_word = value.to_i64() >> 32;
      TurboAssembler::Move(reg.low_gp(), Operand(low_word));
      TurboAssembler::Move(reg.high_gp(), Operand(high_word));
      break;
    }
    case kWasmF32:
      BAILOUT("Load f32 Constant");
      break;
    case kWasmF64: {
      Register extra_scratch = GetUnusedRegister(kGpReg).gp();
      vmov(reg.fp(), Double(value.to_f64_boxed().get_scalar()), extra_scratch);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::LoadFromInstance(Register dst, uint32_t offset,
                                        int size) {
  DCHECK_LE(offset, kMaxInt);
  DCHECK_EQ(4, size);
  ldr(dst, liftoff::GetInstanceOperand());
  ldr(dst, MemOperand(dst, offset));
}

void LiftoffAssembler::SpillInstance(Register instance) {
  str(instance, liftoff::GetInstanceOperand());
}

void LiftoffAssembler::FillInstanceInto(Register dst) {
  ldr(dst, liftoff::GetInstanceOperand());
}

void LiftoffAssembler::Load(LiftoffRegister dst, Register src_addr,
                            Register offset_reg, uint32_t offset_imm,
                            LoadType type, LiftoffRegList pinned,
                            uint32_t* protected_load_pc, bool is_load_mem) {
  DCHECK_IMPLIES(type.value_type() == kWasmI64, dst.is_pair());
  // If offset_imm cannot be converted to int32 safely, we abort as a separate
  // check should cause this code to never be executed.
  // TODO(7881): Support when >2GB is required.
  if (!is_uint31(offset_imm)) {
    TurboAssembler::Abort(AbortReason::kOffsetOutOfRange);
    return;
  }
  UseScratchRegisterScope temps(this);
  if (type.value() == LoadType::kF64Load) {
    // Armv6 is not supported so Neon can be used to avoid alignment issues.
    CpuFeatureScope scope(this, NEON);
    Register actual_src_addr = liftoff::CalculateActualAddress(
        this, &temps, src_addr, offset_reg, offset_imm);
    vld1(Neon64, NeonListOperand(dst.fp()), NeonMemOperand(actual_src_addr));
  } else {
    MemOperand src_op =
        liftoff::GetMemOp(this, &temps, src_addr, offset_reg, offset_imm);
    if (protected_load_pc) *protected_load_pc = pc_offset();
    switch (type.value()) {
      case LoadType::kI32Load8U:
        ldrb(dst.gp(), src_op);
        break;
      case LoadType::kI64Load8U:
        ldrb(dst.low_gp(), src_op);
        mov(dst.high_gp(), Operand(0));
        break;
      case LoadType::kI32Load8S:
        ldrsb(dst.gp(), src_op);
        break;
      case LoadType::kI64Load8S:
        ldrsb(dst.low_gp(), src_op);
        asr(dst.high_gp(), dst.low_gp(), Operand(31));
        break;
      case LoadType::kI32Load16U:
        ldrh(dst.gp(), src_op);
        break;
      case LoadType::kI64Load16U:
        ldrh(dst.low_gp(), src_op);
        mov(dst.high_gp(), Operand(0));
        break;
      case LoadType::kI32Load16S:
        ldrsh(dst.gp(), src_op);
        break;
      case LoadType::kI32Load:
        ldr(dst.gp(), src_op);
        break;
      case LoadType::kI64Load16S:
        ldrsh(dst.low_gp(), src_op);
        asr(dst.high_gp(), dst.low_gp(), Operand(31));
        break;
      case LoadType::kI64Load32U:
        ldr(dst.low_gp(), src_op);
        mov(dst.high_gp(), Operand(0));
        break;
      case LoadType::kI64Load32S:
        ldr(dst.low_gp(), src_op);
        asr(dst.high_gp(), dst.low_gp(), Operand(31));
        break;
      case LoadType::kI64Load:
        ldr(dst.low_gp(), src_op);
        // GetMemOp may use a scratch register as the offset register, in which
        // case, calling GetMemOp again will fail due to the assembler having
        // ran out of scratch registers.
        if (temps.CanAcquire()) {
          src_op = liftoff::GetMemOp(this, &temps, src_addr, offset_reg,
                                     offset_imm + kRegisterSize);
        } else {
          add(src_op.rm(), src_op.rm(), Operand(kRegisterSize));
        }
        ldr(dst.high_gp(), src_op);
        break;
      case LoadType::kF32Load:
        BAILOUT("Load f32");
        break;
      default:
        UNREACHABLE();
    }
  }
}

void LiftoffAssembler::Store(Register dst_addr, Register offset_reg,
                             uint32_t offset_imm, LiftoffRegister src,
                             StoreType type, LiftoffRegList pinned,
                             uint32_t* protected_store_pc, bool is_store_mem) {
  // If offset_imm cannot be converted to int32 safely, we abort as a separate
  // check should cause this code to never be executed.
  // TODO(7881): Support when >2GB is required.
  if (!is_uint31(offset_imm)) {
    TurboAssembler::Abort(AbortReason::kOffsetOutOfRange);
    return;
  }
  UseScratchRegisterScope temps(this);
  if (type.value() == StoreType::kF64Store) {
    // Armv6 is not supported so Neon can be used to avoid alignment issues.
    CpuFeatureScope scope(this, NEON);
    Register actual_dst_addr = liftoff::CalculateActualAddress(
        this, &temps, dst_addr, offset_reg, offset_imm);
    vst1(Neon64, NeonListOperand(src.fp()), NeonMemOperand(actual_dst_addr));
  } else {
    MemOperand dst_op =
        liftoff::GetMemOp(this, &temps, dst_addr, offset_reg, offset_imm);
    if (protected_store_pc) *protected_store_pc = pc_offset();
    switch (type.value()) {
      case StoreType::kI64Store8:
        src = src.low();
        V8_FALLTHROUGH;
      case StoreType::kI32Store8:
        strb(src.gp(), dst_op);
        break;
      case StoreType::kI64Store16:
        src = src.low();
        V8_FALLTHROUGH;
      case StoreType::kI32Store16:
        strh(src.gp(), dst_op);
        break;
      case StoreType::kI64Store32:
        src = src.low();
        V8_FALLTHROUGH;
      case StoreType::kI32Store:
        str(src.gp(), dst_op);
        break;
      case StoreType::kI64Store:
        str(src.low_gp(), dst_op);
        // GetMemOp may use a scratch register as the offset register, in which
        // case, calling GetMemOp again will fail due to the assembler having
        // ran out of scratch registers.
        if (temps.CanAcquire()) {
          dst_op = liftoff::GetMemOp(this, &temps, dst_addr, offset_reg,
                                     offset_imm + kRegisterSize);
        } else {
          add(dst_op.rm(), dst_op.rm(), Operand(kRegisterSize));
        }
        str(src.high_gp(), dst_op);
        break;
      case StoreType::kF32Store:
        BAILOUT("Store f32");
        break;
      default:
        UNREACHABLE();
    }
  }
}

void LiftoffAssembler::LoadCallerFrameSlot(LiftoffRegister dst,
                                           uint32_t caller_slot_idx,
                                           ValueType type) {
  int32_t offset = (caller_slot_idx + 1) * kRegisterSize;
  MemOperand src(fp, offset);
  switch (type) {
    case kWasmI32:
      ldr(dst.gp(), src);
      break;
    case kWasmI64:
      ldr(dst.low_gp(), src);
      ldr(dst.high_gp(), MemOperand(fp, offset + kRegisterSize));
      break;
    case kWasmF32:
      BAILOUT("Load Caller Frame Slot for f32");
      break;
    case kWasmF64:
      vldr(dst.fp(), src);
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::MoveStackValue(uint32_t dst_index, uint32_t src_index,
                                      ValueType type) {
  DCHECK_NE(dst_index, src_index);
  LiftoffRegister reg = GetUnusedRegister(kGpReg);
  Fill(reg, src_index, type);
  Spill(dst_index, reg, type);
}

void LiftoffAssembler::Move(Register dst, Register src, ValueType type) {
  DCHECK_NE(dst, src);
  DCHECK_EQ(type, kWasmI32);
  TurboAssembler::Move(dst, src);
}

void LiftoffAssembler::Move(DoubleRegister dst, DoubleRegister src,
                            ValueType type) {
  BAILOUT("Move DoubleRegister");
}

void LiftoffAssembler::Spill(uint32_t index, LiftoffRegister reg,
                             ValueType type) {
  RecordUsedSpillSlot(index);
  MemOperand dst = liftoff::GetStackSlot(index);
  switch (type) {
    case kWasmI32:
      str(reg.gp(), dst);
      break;
    case kWasmI64:
      str(reg.low_gp(), liftoff::GetHalfStackSlot(index, kLowWord));
      str(reg.high_gp(), liftoff::GetHalfStackSlot(index, kHighWord));
      break;
    case kWasmF32:
      BAILOUT("Spill Register f32");
      break;
    case kWasmF64:
      vstr(reg.fp(), dst);
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::Spill(uint32_t index, WasmValue value) {
  RecordUsedSpillSlot(index);
  MemOperand dst = liftoff::GetStackSlot(index);
  UseScratchRegisterScope temps(this);
  Register src = no_reg;
  // The scratch register will be required by str if multiple instructions
  // are required to encode the offset, and so we cannot use it in that case.
  if (!ImmediateFitsAddrMode2Instruction(dst.offset())) {
    src = GetUnusedRegister(kGpReg).gp();
  } else {
    src = temps.Acquire();
  }
  switch (value.type()) {
    case kWasmI32:
      mov(src, Operand(value.to_i32()));
      str(src, dst);
      break;
    case kWasmI64: {
      int32_t low_word = value.to_i64();
      mov(src, Operand(low_word));
      str(src, liftoff::GetHalfStackSlot(index, kLowWord));
      int32_t high_word = value.to_i64() >> 32;
      mov(src, Operand(high_word));
      str(src, liftoff::GetHalfStackSlot(index, kHighWord));
      break;
    }
    default:
      // We do not track f32 and f64 constants, hence they are unreachable.
      UNREACHABLE();
  }
}

void LiftoffAssembler::Fill(LiftoffRegister reg, uint32_t index,
                            ValueType type) {
  switch (type) {
    case kWasmI32:
      ldr(reg.gp(), liftoff::GetStackSlot(index));
      break;
    case kWasmI64:
      ldr(reg.low_gp(), liftoff::GetHalfStackSlot(index, kLowWord));
      ldr(reg.high_gp(), liftoff::GetHalfStackSlot(index, kHighWord));
      break;
    case kWasmF32:
      BAILOUT("Fill Register");
      break;
    case kWasmF64:
      vldr(reg.fp(), liftoff::GetStackSlot(index));
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::FillI64Half(Register reg, uint32_t half_index) {
  ldr(reg, liftoff::GetHalfStackSlot(half_index));
}

#define I32_SHIFTOP(name, instruction)                                         \
  void LiftoffAssembler::emit_##name(Register dst, Register src,               \
                                     Register amount, LiftoffRegList pinned) { \
    UseScratchRegisterScope temps(this);                                       \
    Register scratch = temps.Acquire();                                        \
    and_(scratch, amount, Operand(0x1f));                                      \
    instruction(dst, src, Operand(scratch));                                   \
  }
#define I32_SHIFTOP_I(name, instruction)                                       \
  void LiftoffAssembler::emit_##name(Register dst, Register src, int amount) { \
    DCHECK(is_uint5(amount));                                                  \
    instruction(dst, src, Operand(amount));                                    \
  }
#define I32_BINOP(name, instruction)                             \
  void LiftoffAssembler::emit_##name(Register dst, Register lhs, \
                                     Register rhs) {             \
    instruction(dst, lhs, rhs);                                  \
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
#define UNIMPLEMENTED_FP_UNOP_RETURN_TRUE(name)                                \
  bool LiftoffAssembler::emit_##name(DoubleRegister dst, DoubleRegister src) { \
    BAILOUT("fp unop: " #name);                                                \
    return true;                                                               \
  }
#define UNIMPLEMENTED_I64_SHIFTOP(name)                                        \
  void LiftoffAssembler::emit_##name(LiftoffRegister dst, LiftoffRegister src, \
                                     Register amount, LiftoffRegList pinned) { \
    BAILOUT("i64 shiftop: " #name);                                            \
  }

I32_BINOP(i32_add, add)
I32_BINOP(i32_sub, sub)
I32_BINOP(i32_mul, mul)
I32_BINOP(i32_and, and_)
I32_BINOP(i32_or, orr)
I32_BINOP(i32_xor, eor)
I32_SHIFTOP(i32_shl, lsl)
I32_SHIFTOP(i32_sar, asr)
I32_SHIFTOP(i32_shr, lsr)
I32_SHIFTOP_I(i32_shr, lsr)
UNIMPLEMENTED_I64_BINOP(i64_add)
UNIMPLEMENTED_I64_BINOP(i64_sub)
UNIMPLEMENTED_I64_BINOP(i64_mul)
UNIMPLEMENTED_I64_SHIFTOP(i64_shl)
UNIMPLEMENTED_I64_SHIFTOP(i64_sar)
UNIMPLEMENTED_I64_SHIFTOP(i64_shr)
UNIMPLEMENTED_GP_UNOP(i32_popcnt)
UNIMPLEMENTED_FP_BINOP(f32_add)
UNIMPLEMENTED_FP_BINOP(f32_sub)
UNIMPLEMENTED_FP_BINOP(f32_mul)
UNIMPLEMENTED_FP_BINOP(f32_div)
UNIMPLEMENTED_FP_BINOP(f32_min)
UNIMPLEMENTED_FP_BINOP(f32_max)
UNIMPLEMENTED_FP_BINOP(f32_copysign)
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
UNIMPLEMENTED_FP_BINOP(f64_min)
UNIMPLEMENTED_FP_BINOP(f64_max)
UNIMPLEMENTED_FP_BINOP(f64_copysign)
UNIMPLEMENTED_FP_UNOP(f64_abs)
UNIMPLEMENTED_FP_UNOP(f64_neg)
UNIMPLEMENTED_FP_UNOP_RETURN_TRUE(f64_ceil)
UNIMPLEMENTED_FP_UNOP_RETURN_TRUE(f64_floor)
UNIMPLEMENTED_FP_UNOP_RETURN_TRUE(f64_trunc)
UNIMPLEMENTED_FP_UNOP_RETURN_TRUE(f64_nearest_int)
UNIMPLEMENTED_FP_UNOP(f64_sqrt)

#undef I32_BINOP
#undef I32_SHIFTOP
#undef I32_SHIFTOP_I
#undef UNIMPLEMENTED_GP_BINOP
#undef UNIMPLEMENTED_I64_BINOP
#undef UNIMPLEMENTED_GP_UNOP
#undef UNIMPLEMENTED_FP_BINOP
#undef UNIMPLEMENTED_FP_UNOP
#undef UNIMPLEMENTED_FP_UNOP_RETURN_TRUE
#undef UNIMPLEMENTED_I64_SHIFTOP

bool LiftoffAssembler::emit_i32_clz(Register dst, Register src) {
  clz(dst, src);
  return true;
}

bool LiftoffAssembler::emit_i32_ctz(Register dst, Register src) {
  rbit(dst, src);
  clz(dst, dst);
  return true;
}

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

bool LiftoffAssembler::emit_i64_divs(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  return false;
}

bool LiftoffAssembler::emit_i64_divu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  return false;
}

bool LiftoffAssembler::emit_i64_rems(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  return false;
}

bool LiftoffAssembler::emit_i64_remu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  return false;
}

void LiftoffAssembler::emit_i64_shr(LiftoffRegister dst, LiftoffRegister lhs,
                                    int amount) {
  BAILOUT("i64_shr");
}

void LiftoffAssembler::emit_i32_to_intptr(Register dst, Register src) {
  // This is a nop on arm.
}

bool LiftoffAssembler::emit_type_conversion(WasmOpcode opcode,
                                            LiftoffRegister dst,
                                            LiftoffRegister src, Label* trap) {
  BAILOUT("emit_type_conversion");
  return true;
}

void LiftoffAssembler::emit_i32_signextend_i8(Register dst, Register src) {
  BAILOUT("emit_i32_signextend_i8");
}

void LiftoffAssembler::emit_i32_signextend_i16(Register dst, Register src) {
  BAILOUT("emit_i32_signextend_i16");
}

void LiftoffAssembler::emit_i64_signextend_i8(LiftoffRegister dst,
                                              LiftoffRegister src) {
  BAILOUT("emit_i64_signextend_i8");
}

void LiftoffAssembler::emit_i64_signextend_i16(LiftoffRegister dst,
                                               LiftoffRegister src) {
  BAILOUT("emit_i64_signextend_i16");
}

void LiftoffAssembler::emit_i64_signextend_i32(LiftoffRegister dst,
                                               LiftoffRegister src) {
  BAILOUT("emit_i64_signextend_i32");
}

void LiftoffAssembler::emit_jump(Label* label) { b(label); }

void LiftoffAssembler::emit_jump(Register target) { bx(target); }

void LiftoffAssembler::emit_cond_jump(Condition cond, Label* label,
                                      ValueType type, Register lhs,
                                      Register rhs) {
  DCHECK_EQ(type, kWasmI32);
  if (rhs == no_reg) {
    cmp(lhs, Operand(0));
  } else {
    cmp(lhs, rhs);
  }
  b(label, cond);
}

void LiftoffAssembler::emit_i32_eqz(Register dst, Register src) {
  clz(dst, src);
  mov(dst, Operand(dst, LSR, kRegSizeInBitsLog2));
}

void LiftoffAssembler::emit_i32_set_cond(Condition cond, Register dst,
                                         Register lhs, Register rhs) {
  cmp(lhs, rhs);
  mov(dst, Operand(0), LeaveCC);
  mov(dst, Operand(1), LeaveCC, cond);
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

void LiftoffAssembler::StackCheck(Label* ool_code, Register limit_address) {
  ldr(limit_address, MemOperand(limit_address));
  cmp(sp, limit_address);
  b(ool_code, ls);
}

void LiftoffAssembler::CallTrapCallbackForTesting() {
  PrepareCallCFunction(0, 0);
  CallCFunction(ExternalReference::wasm_call_trap_callback_for_testing(), 0);
}

void LiftoffAssembler::AssertUnreachable(AbortReason reason) {
  BAILOUT("AssertUnreachable");
}

void LiftoffAssembler::PushRegisters(LiftoffRegList regs) {
  RegList core_regs = regs.GetGpList();
  if (core_regs != 0) {
    stm(db_w, sp, core_regs);
  }
  LiftoffRegList fp_regs = regs & kFpCacheRegList;
  while (!fp_regs.is_empty()) {
    LiftoffRegister reg = fp_regs.GetFirstRegSet();
    DoubleRegister first = reg.fp();
    DoubleRegister last = first;
    fp_regs.clear(reg);
    while (!fp_regs.is_empty()) {
      LiftoffRegister reg = fp_regs.GetFirstRegSet();
      int code = reg.fp().code();
      // vstm can not push more than 16 registers. We have to make sure the
      // condition is met.
      if ((code != last.code() + 1) || ((code - first.code() + 1) > 16)) break;
      last = reg.fp();
      fp_regs.clear(reg);
    }
    vstm(db_w, sp, first, last);
  }
}

void LiftoffAssembler::PopRegisters(LiftoffRegList regs) {
  LiftoffRegList fp_regs = regs & kFpCacheRegList;
  while (!fp_regs.is_empty()) {
    LiftoffRegister reg = fp_regs.GetLastRegSet();
    DoubleRegister last = reg.fp();
    DoubleRegister first = last;
    fp_regs.clear(reg);
    while (!fp_regs.is_empty()) {
      LiftoffRegister reg = fp_regs.GetLastRegSet();
      int code = reg.fp().code();
      if ((code != first.code() - 1) || ((last.code() - code + 1) > 16)) break;
      first = reg.fp();
      fp_regs.clear(reg);
    }
    vldm(ia_w, sp, first, last);
  }
  RegList core_regs = regs.GetGpList();
  if (core_regs != 0) {
    ldm(ia_w, sp, core_regs);
  }
}

void LiftoffAssembler::DropStackSlotsAndRet(uint32_t num_stack_slots) {
  Drop(num_stack_slots);
  Ret();
}

void LiftoffAssembler::CallC(wasm::FunctionSig* sig,
                             const LiftoffRegister* args,
                             const LiftoffRegister* rets,
                             ValueType out_argument_type, int stack_bytes,
                             ExternalReference ext_ref) {
  // Arguments are passed by pushing them all to the stack and then passing
  // a pointer to them.
  DCHECK_EQ(stack_bytes % kPointerSize, 0);
  // Reserve space in the stack.
  sub(sp, sp, Operand(stack_bytes));

  int arg_bytes = 0;
  for (ValueType param_type : sig->parameters()) {
    switch (param_type) {
      case kWasmI32:
        str(args->gp(), MemOperand(sp, arg_bytes));
        break;
      case kWasmI64:
        str(args->low_gp(), MemOperand(sp, arg_bytes));
        str(args->high_gp(), MemOperand(sp, arg_bytes + kRegisterSize));
        break;
      case kWasmF32:
        BAILOUT("Call C for f32 parameter");
        break;
      case kWasmF64:
        vstr(args->fp(), MemOperand(sp, arg_bytes));
        break;
      default:
        UNREACHABLE();
    }
    args++;
    arg_bytes += ValueTypes::MemSize(param_type);
  }
  DCHECK_LE(arg_bytes, stack_bytes);

  // Pass a pointer to the buffer with the arguments to the C function.
  mov(r0, sp);

  // Now call the C function.
  constexpr int kNumCCallArgs = 1;
  PrepareCallCFunction(kNumCCallArgs);
  CallCFunction(ext_ref, kNumCCallArgs);

  // Move return value to the right register.
  const LiftoffRegister* result_reg = rets;
  if (sig->return_count() > 0) {
    DCHECK_EQ(1, sig->return_count());
    constexpr Register kReturnReg = r0;
    if (kReturnReg != rets->gp()) {
      Move(*rets, LiftoffRegister(kReturnReg), sig->GetReturn(0));
    }
    result_reg++;
  }

  // Load potential output value from the buffer on the stack.
  if (out_argument_type != kWasmStmt) {
    switch (out_argument_type) {
      case kWasmI32:
        ldr(result_reg->gp(), MemOperand(sp));
        break;
      case kWasmI64:
        ldr(result_reg->low_gp(), MemOperand(sp));
        ldr(result_reg->high_gp(), MemOperand(sp, kPointerSize));
        break;
      case kWasmF32:
        BAILOUT("Call C for f32 parameter");
        break;
      case kWasmF64:
        vldr(result_reg->fp(), MemOperand(sp));
        break;
      default:
        UNREACHABLE();
    }
  }
  add(sp, sp, Operand(stack_bytes));
}

void LiftoffAssembler::CallNativeWasmCode(Address addr) {
  Call(addr, RelocInfo::WASM_CALL);
}

void LiftoffAssembler::CallIndirect(wasm::FunctionSig* sig,
                                    compiler::CallDescriptor* call_descriptor,
                                    Register target) {
  DCHECK(target != no_reg);
  Call(target);
}

void LiftoffAssembler::CallRuntimeStub(WasmCode::RuntimeStubId sid) {
  // A direct call to a wasm runtime stub defined in this module.
  // Just encode the stub index. This will be patched at relocation.
  Call(static_cast<Address>(sid), RelocInfo::WASM_STUB_CALL);
}

void LiftoffAssembler::AllocateStackSlot(Register addr, uint32_t size) {
  sub(sp, sp, Operand(size));
  mov(addr, sp);
}

void LiftoffAssembler::DeallocateStackSlot(uint32_t size) {
  add(sp, sp, Operand(size));
}

void LiftoffStackSlots::Construct() {
  for (auto& slot : slots_) {
    const LiftoffAssembler::VarState& src = slot.src_;
    switch (src.loc()) {
      case LiftoffAssembler::VarState::kStack: {
        switch (src.type()) {
          // i32 and i64 can be treated as similar cases, i64 being previously
          // split into two i32 registers
          case kWasmI32:
          case kWasmI64: {
            UseScratchRegisterScope temps(asm_);
            Register scratch = temps.Acquire();
            asm_->ldr(scratch,
                      liftoff::GetHalfStackSlot(slot.src_index_, slot.half_));
            asm_->Push(scratch);
          } break;
          case kWasmF32:
            asm_->BAILOUT("Construct f32 from kStack");
            break;
          case kWasmF64: {
            UseScratchRegisterScope temps(asm_);
            DwVfpRegister scratch = temps.AcquireD();
            asm_->vldr(scratch, liftoff::GetStackSlot(slot.src_index_));
            asm_->vpush(scratch);
          } break;
          default:
            UNREACHABLE();
        }
        break;
      }
      case LiftoffAssembler::VarState::kRegister:
        switch (src.type()) {
          case kWasmI64: {
            LiftoffRegister reg =
                slot.half_ == kLowWord ? src.reg().low() : src.reg().high();
            asm_->push(reg.gp());
          } break;
          case kWasmI32:
            asm_->push(src.reg().gp());
            break;
          case kWasmF32:
            asm_->BAILOUT("Construct f32 from kRegister");
            break;
          case kWasmF64:
            asm_->vpush(src.reg().fp());
            break;
          default:
            UNREACHABLE();
        }
        break;
      case LiftoffAssembler::VarState::KIntConst: {
        DCHECK(src.type() == kWasmI32 || src.type() == kWasmI64);
        UseScratchRegisterScope temps(asm_);
        Register scratch = temps.Acquire();
        // The high word is the sign extension of the low word.
        asm_->mov(scratch,
                  Operand(slot.half_ == kLowWord ? src.i32_const()
                                                 : src.i32_const() >> 31));
        asm_->push(scratch);
        break;
      }
    }
  }
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#undef BAILOUT

#endif  // V8_WASM_BASELINE_ARM_LIFTOFF_ASSEMBLER_ARM_H_
