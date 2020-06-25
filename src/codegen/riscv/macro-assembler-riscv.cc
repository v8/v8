// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>  // For LONG_MIN, LONG_MAX.

#if V8_TARGET_ARCH_RISCV

#include "src/base/bits.h"
#include "src/base/division-by-constant.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/callable.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/external-reference-table.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/register-configuration.h"
#include "src/debug/debug.h"
#include "src/execution/frames-inl.h"
#include "src/heap/heap-inl.h"  // For MemoryChunk.
#include "src/init/bootstrapper.h"
#include "src/logging/counters.h"
#include "src/objects/heap-number.h"
#include "src/runtime/runtime.h"
#include "src/snapshot/embedded/embedded-data.h"
#include "src/snapshot/snapshot.h"
#include "src/wasm/wasm-code-manager.h"

// Satisfy cpplint check, but don't include platform-specific header. It is
// included recursively via macro-assembler.h.
#if 0
#include "src/codegen/riscv/macro-assembler-riscv.h"
#endif

namespace v8 {
namespace internal {

static inline bool IsZero(const Operand& rt) {
  if (rt.is_reg()) {
    return rt.rm() == zero_reg;
  } else {
    return rt.immediate() == 0;
  }
}

int TurboAssembler::RequiredStackSizeForCallerSaved(SaveFPRegsMode fp_mode,
                                                    Register exclusion1,
                                                    Register exclusion2,
                                                    Register exclusion3) const {
  int bytes = 0;
  RegList exclusions = 0;
  if (exclusion1 != no_reg) {
    exclusions |= exclusion1.bit();
    if (exclusion2 != no_reg) {
      exclusions |= exclusion2.bit();
      if (exclusion3 != no_reg) {
        exclusions |= exclusion3.bit();
      }
    }
  }

  RegList list = kJSCallerSaved & ~exclusions;
  bytes += NumRegs(list) * kPointerSize;

  if (fp_mode == kSaveFPRegs) {
    bytes += NumRegs(kCallerSavedFPU) * kDoubleSize;
  }

  return bytes;
}

int TurboAssembler::PushCallerSaved(SaveFPRegsMode fp_mode, Register exclusion1,
                                    Register exclusion2, Register exclusion3) {
  int bytes = 0;
  RegList exclusions = 0;
  if (exclusion1 != no_reg) {
    exclusions |= exclusion1.bit();
    if (exclusion2 != no_reg) {
      exclusions |= exclusion2.bit();
      if (exclusion3 != no_reg) {
        exclusions |= exclusion3.bit();
      }
    }
  }

  RegList list = kJSCallerSaved & ~exclusions;
  MultiPush(list);
  bytes += NumRegs(list) * kPointerSize;

  if (fp_mode == kSaveFPRegs) {
    MultiPushFPU(kCallerSavedFPU);
    bytes += NumRegs(kCallerSavedFPU) * kDoubleSize;
  }

  return bytes;
}

int TurboAssembler::PopCallerSaved(SaveFPRegsMode fp_mode, Register exclusion1,
                                   Register exclusion2, Register exclusion3) {
  int bytes = 0;
  if (fp_mode == kSaveFPRegs) {
    MultiPopFPU(kCallerSavedFPU);
    bytes += NumRegs(kCallerSavedFPU) * kDoubleSize;
  }

  RegList exclusions = 0;
  if (exclusion1 != no_reg) {
    exclusions |= exclusion1.bit();
    if (exclusion2 != no_reg) {
      exclusions |= exclusion2.bit();
      if (exclusion3 != no_reg) {
        exclusions |= exclusion3.bit();
      }
    }
  }

  RegList list = kJSCallerSaved & ~exclusions;
  MultiPop(list);
  bytes += NumRegs(list) * kPointerSize;

  return bytes;
}

void TurboAssembler::LoadRoot(Register destination, RootIndex index) {
  Ld(destination, MemOperand(s6, RootRegisterOffsetForRootIndex(index)));
}

void TurboAssembler::LoadRoot(Register destination, RootIndex index,
                              Condition cond, Register src1,
                              const Operand& src2) {
  Label skip;
  Branch(&skip, NegateCondition(cond), src1, src2);
  Ld(destination, MemOperand(s6, RootRegisterOffsetForRootIndex(index)));
  bind(&skip);
}

void TurboAssembler::PushCommonFrame(Register marker_reg) {
  if (marker_reg.is_valid()) {
    Push(ra, fp, marker_reg);
    Daddu(fp, sp, Operand(kPointerSize));
  } else {
    Push(ra, fp);
    RV_mv(fp, sp);
  }
}

void TurboAssembler::PushStandardFrame(Register function_reg) {
  int offset = -StandardFrameConstants::kContextOffset;
  if (function_reg.is_valid()) {
    Push(ra, fp, cp, function_reg);
    offset += kPointerSize;
  } else {
    Push(ra, fp, cp);
  }
  Daddu(fp, sp, Operand(offset));
}

int MacroAssembler::SafepointRegisterStackIndex(int reg_code) {
  // The registers are pushed starting with the highest encoding,
  // which means that lowest encodings are closest to the stack pointer.
  return kSafepointRegisterStackIndexMap[reg_code];
}

// Clobbers object, dst, value, and ra, if (ra_status == kRAHasBeenSaved)
// The register 'object' contains a heap object pointer.  The heap object
// tag is shifted away.
void MacroAssembler::RecordWriteField(Register object, int offset,
                                      Register value, Register dst,
                                      RAStatus ra_status,
                                      SaveFPRegsMode save_fp,
                                      RememberedSetAction remembered_set_action,
                                      SmiCheck smi_check) {
  DCHECK(!AreAliased(value, dst, t5, object));
  // First, check if a write barrier is even needed. The tests below
  // catch stores of Smis.
  Label done;

  // Skip barrier if writing a smi.
  if (smi_check == INLINE_SMI_CHECK) {
    JumpIfSmi(value, &done);
  }

  // Although the object register is tagged, the offset is relative to the start
  // of the object, so so offset must be a multiple of kPointerSize.
  DCHECK(IsAligned(offset, kPointerSize));

  Daddu(dst, object, Operand(offset - kHeapObjectTag));
  if (emit_debug_code()) {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Label ok;
    And(t5, dst, Operand(kPointerSize - 1));
    Branch(&ok, eq, t5, Operand(zero_reg));
    RV_ebreak();
    bind(&ok);
  }

  RecordWrite(object, dst, value, ra_status, save_fp, remembered_set_action,
              OMIT_SMI_CHECK);

  bind(&done);

  // Clobber clobbered input registers when running with the debug-code flag
  // turned on to provoke errors.
  if (emit_debug_code()) {
    li(value, Operand(bit_cast<int64_t>(kZapValue + 4)));
    li(dst, Operand(bit_cast<int64_t>(kZapValue + 8)));
  }
}

void TurboAssembler::SaveRegisters(RegList registers) {
  DCHECK_GT(NumRegs(registers), 0);
  RegList regs = 0;
  for (int i = 0; i < Register::kNumRegisters; ++i) {
    if ((registers >> i) & 1u) {
      regs |= Register::from_code(i).bit();
    }
  }
  MultiPush(regs);
}

void TurboAssembler::RestoreRegisters(RegList registers) {
  DCHECK_GT(NumRegs(registers), 0);
  RegList regs = 0;
  for (int i = 0; i < Register::kNumRegisters; ++i) {
    if ((registers >> i) & 1u) {
      regs |= Register::from_code(i).bit();
    }
  }
  MultiPop(regs);
}

void TurboAssembler::CallEphemeronKeyBarrier(Register object, Register address,
                                             SaveFPRegsMode fp_mode) {
  EphemeronKeyBarrierDescriptor descriptor;
  RegList registers = descriptor.allocatable_registers();

  SaveRegisters(registers);

  Register object_parameter(
      descriptor.GetRegisterParameter(EphemeronKeyBarrierDescriptor::kObject));
  Register slot_parameter(descriptor.GetRegisterParameter(
      EphemeronKeyBarrierDescriptor::kSlotAddress));
  Register fp_mode_parameter(
      descriptor.GetRegisterParameter(EphemeronKeyBarrierDescriptor::kFPMode));

  Push(object);
  Push(address);

  Pop(slot_parameter);
  Pop(object_parameter);

  Move(fp_mode_parameter, Smi::FromEnum(fp_mode));
  Call(isolate()->builtins()->builtin_handle(Builtins::kEphemeronKeyBarrier),
       RelocInfo::CODE_TARGET);
  RestoreRegisters(registers);
}

void TurboAssembler::CallRecordWriteStub(
    Register object, Register address,
    RememberedSetAction remembered_set_action, SaveFPRegsMode fp_mode) {
  CallRecordWriteStub(
      object, address, remembered_set_action, fp_mode,
      isolate()->builtins()->builtin_handle(Builtins::kRecordWrite),
      kNullAddress);
}

void TurboAssembler::CallRecordWriteStub(
    Register object, Register address,
    RememberedSetAction remembered_set_action, SaveFPRegsMode fp_mode,
    Address wasm_target) {
  CallRecordWriteStub(object, address, remembered_set_action, fp_mode,
                      Handle<Code>::null(), wasm_target);
}

void TurboAssembler::CallRecordWriteStub(
    Register object, Register address,
    RememberedSetAction remembered_set_action, SaveFPRegsMode fp_mode,
    Handle<Code> code_target, Address wasm_target) {
  DCHECK_NE(code_target.is_null(), wasm_target == kNullAddress);
  // TODO(albertnetymk): For now we ignore remembered_set_action and fp_mode,
  // i.e. always emit remember set and save FP registers in RecordWriteStub. If
  // large performance regression is observed, we should use these values to
  // avoid unnecessary work.

  RecordWriteDescriptor descriptor;
  RegList registers = descriptor.allocatable_registers();

  SaveRegisters(registers);
  Register object_parameter(
      descriptor.GetRegisterParameter(RecordWriteDescriptor::kObject));
  Register slot_parameter(
      descriptor.GetRegisterParameter(RecordWriteDescriptor::kSlot));
  Register remembered_set_parameter(
      descriptor.GetRegisterParameter(RecordWriteDescriptor::kRememberedSet));
  Register fp_mode_parameter(
      descriptor.GetRegisterParameter(RecordWriteDescriptor::kFPMode));

  Push(object);
  Push(address);

  Pop(slot_parameter);
  Pop(object_parameter);

  Move(remembered_set_parameter, Smi::FromEnum(remembered_set_action));
  Move(fp_mode_parameter, Smi::FromEnum(fp_mode));
  if (code_target.is_null()) {
    Call(wasm_target, RelocInfo::WASM_STUB_CALL);
  } else {
    Call(code_target, RelocInfo::CODE_TARGET);
  }

  RestoreRegisters(registers);
}

// Clobbers object, address, value, and ra, if (ra_status == kRAHasBeenSaved)
// The register 'object' contains a heap object pointer.  The heap object
// tag is shifted away.
void MacroAssembler::RecordWrite(Register object, Register address,
                                 Register value, RAStatus ra_status,
                                 SaveFPRegsMode fp_mode,
                                 RememberedSetAction remembered_set_action,
                                 SmiCheck smi_check) {
  DCHECK(!AreAliased(object, address, value, t5));
  DCHECK(!AreAliased(object, address, value, t6));

  if (emit_debug_code()) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Ld(scratch, MemOperand(address));
    Assert(eq, AbortReason::kWrongAddressOrValuePassedToRecordWrite, scratch,
           Operand(value));
  }

  if ((remembered_set_action == OMIT_REMEMBERED_SET &&
       !FLAG_incremental_marking) ||
      FLAG_disable_write_barriers) {
    return;
  }

  // First, check if a write barrier is even needed. The tests below
  // catch stores of smis and stores into the young generation.
  Label done;

  if (smi_check == INLINE_SMI_CHECK) {
    DCHECK_EQ(0, kSmiTag);
    JumpIfSmi(value, &done);
  }

  CheckPageFlag(value,
                value,  // Used as scratch.
                MemoryChunk::kPointersToHereAreInterestingMask, eq, &done);
  CheckPageFlag(object,
                value,  // Used as scratch.
                MemoryChunk::kPointersFromHereAreInterestingMask, eq, &done);

  // Record the actual write.
  if (ra_status == kRAHasNotBeenSaved) {
    push(ra);
  }
  CallRecordWriteStub(object, address, remembered_set_action, fp_mode);
  if (ra_status == kRAHasNotBeenSaved) {
    pop(ra);
  }

  bind(&done);

  // Clobber clobbered registers when running with the debug-code flag
  // turned on to provoke errors.
  if (emit_debug_code()) {
    li(address, Operand(bit_cast<int64_t>(kZapValue + 12)));
    li(value, Operand(bit_cast<int64_t>(kZapValue + 16)));
  }
}

// ---------------------------------------------------------------------------
// Instruction macros.

void TurboAssembler::Addu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_addw(rd, rs, rt.rm());
  } else {
    if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      RV_addiw(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      RV_li(scratch, rt.immediate());
      RV_addw(rd, rs, scratch);
    }
  }
}

void TurboAssembler::Daddu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_add(rd, rs, rt.rm());
  } else {
    if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      RV_addi(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      // Register scratch = temps.Acquire();
      BlockTrampolinePoolScope block_trampoline_pool(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
      DCHECK(rs != scratch);
      RV_li(scratch, rt.immediate());
      RV_add(rd, rs, scratch);
    }
  }
}

void TurboAssembler::Subu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_subw(rd, rs, rt.rm());
  } else {
    DCHECK(is_int32(rt.immediate()));
    if (is_int12(-rt.immediate()) && !MustUseReg(rt.rmode())) {
      RV_addiw(rd, rs,
               static_cast<int32_t>(
                   -rt.immediate()));  // No subiu instr, use addiu(x, y, -imm).
    } else {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      if (-rt.immediate() >> 12 == 0 && !MustUseReg(rt.rmode())) {
        // Use load -imm and addu when loading -imm generates one instruction.
        RV_li(scratch, -rt.immediate());
        RV_addw(rd, rs, scratch);
      } else {
        // li handles the relocation.
        RV_li(scratch, rt.immediate());
        RV_subw(rd, rs, scratch);
      }
    }
  }
}

void TurboAssembler::Dsubu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_sub(rd, rs, rt.rm());
  } else if (is_int12(-rt.immediate()) && !MustUseReg(rt.rmode())) {
    RV_addi(rd, rs,
            static_cast<int32_t>(
                -rt.immediate()));  // No subi instr, use addi(x, y, -imm).
  } else {
    DCHECK(rs != t3);
    int li_count = InstrCountForLi64Bit(rt.immediate());
    int li_neg_count = InstrCountForLi64Bit(-rt.immediate());
    if (li_neg_count < li_count && !MustUseReg(rt.rmode())) {
      // Use load -imm and RV_add when loading -imm generates one instruction.
      DCHECK(rt.immediate() != std::numeric_limits<int32_t>::min());
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      RV_li(scratch, -rt.immediate());
      RV_add(rd, rs, scratch);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      RV_li(scratch, rt.immediate());
      RV_sub(rd, rs, scratch);
    }
  }
}

void TurboAssembler::Mul(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_mulw(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_mulw(rd, rs, scratch);
  }
}

void TurboAssembler::Mulh(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    // Perform the 64 bit multiplication, then extract the top 32 bits
    RV_mulh(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_mulh(rd, rs, scratch);
  }
}

void TurboAssembler::Mulhu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_mulhu(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_mulhu(rd, rs, scratch);
  }
}

void TurboAssembler::Dmul(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_mul(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_mul(rd, rs, scratch);
  }
}

void TurboAssembler::Dmulh(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_mulh(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_mulh(rd, rs, scratch);
  }
}

void TurboAssembler::Div(Register res, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_divw(res, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_divw(res, rs, scratch);
  }
}

void TurboAssembler::Mod(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_remw(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_remw(rd, rs, scratch);
  }
}

void TurboAssembler::Modu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_remuw(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_remuw(rd, rs, scratch);
  }
}

void TurboAssembler::Ddiv(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_div(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_div(rd, rs, scratch);
  }
}

void TurboAssembler::Divu(Register res, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_divuw(res, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_divuw(res, rs, scratch);
  }
}

void TurboAssembler::Ddivu(Register res, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_divu(res, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_divu(res, rs, scratch);
  }
}

void TurboAssembler::Dmod(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_rem(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_rem(rd, rs, scratch);
  }
}

void TurboAssembler::Dmodu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_remu(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_remu(rd, rs, scratch);
  }
}

void TurboAssembler::And(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_and_(rd, rs, rt.rm());
  } else {
    if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      RV_andi(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      RV_li(scratch, rt.immediate());
      RV_and_(rd, rs, scratch);
    }
  }
}

void TurboAssembler::Or(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_or_(rd, rs, rt.rm());
  } else {
    if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      RV_ori(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      RV_li(scratch, rt.immediate());
      RV_or_(rd, rs, scratch);
    }
  }
}

void TurboAssembler::Xor(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_xor_(rd, rs, rt.rm());
  } else {
    if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      RV_xori(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      RV_li(scratch, rt.immediate());
      RV_xor_(rd, rs, scratch);
    }
  }
}

void TurboAssembler::Nor(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_or_(rd, rs, rt.rm());
    RV_not(rd, rd);
  } else {
    Or(rd, rs, rt);
    RV_not(rd, rd);
  }
}

void TurboAssembler::Neg(Register rs, const Operand& rt) {
  DCHECK(rt.is_reg());
  RV_neg(rs, rt.rm());
}

void TurboAssembler::Slt(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_slt(rd, rs, rt.rm());
  } else {
    if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      RV_slti(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      BlockTrampolinePoolScope block_trampoline_pool(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
      DCHECK(rs != scratch);
      RV_li(scratch, rt.immediate());
      RV_slt(rd, rs, scratch);
    }
  }
}

void TurboAssembler::Sltu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_sltu(rd, rs, rt.rm());
  } else {
    if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      RV_sltiu(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      BlockTrampolinePoolScope block_trampoline_pool(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
      DCHECK(rs != scratch);
      RV_li(scratch, rt.immediate());
      RV_sltu(rd, rs, scratch);
    }
  }
}

void TurboAssembler::Sle(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_slt(rd, rt.rm(), rs);
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    BlockTrampolinePoolScope block_trampoline_pool(this);
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_slt(rd, scratch, rs);
  }
  RV_xori(rd, rd, 1);
}

void TurboAssembler::Sleu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_sltu(rd, rt.rm(), rs);
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    BlockTrampolinePoolScope block_trampoline_pool(this);
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_sltu(rd, scratch, rs);
  }
  RV_xori(rd, rd, 1);
}

void TurboAssembler::Sge(Register rd, Register rs, const Operand& rt) {
  Slt(rd, rs, rt);
  RV_xori(rd, rd, 1);
}

void TurboAssembler::Sgeu(Register rd, Register rs, const Operand& rt) {
  Sltu(rd, rs, rt);
  RV_xori(rd, rd, 1);
}

void TurboAssembler::Sgt(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_slt(rd, rt.rm(), rs);
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    BlockTrampolinePoolScope block_trampoline_pool(this);
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_slt(rd, scratch, rs);
  }
}

void TurboAssembler::Sgtu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    RV_sltu(rd, rt.rm(), rs);
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    BlockTrampolinePoolScope block_trampoline_pool(this);
    DCHECK(rs != scratch);
    RV_li(scratch, rt.immediate());
    RV_sltu(rd, scratch, rs);
  }
}

void TurboAssembler::Sll(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg())
    RV_sllw(rd, rs, rt.rm());
  else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    RV_slliw(rd, rs, shamt);
  }
}

void TurboAssembler::Seb(Register rd, const Operand& rt) {
  DCHECK(rt.is_reg());
  RV_slli(rd, rt.rm(), 64 - 8);
  RV_srai(rd, rd, 64 - 8);
}

void TurboAssembler::Seh(Register rd, const Operand& rt) {
  DCHECK(rt.is_reg());
  RV_slli(rd, rt.rm(), 64 - 16);
  RV_srai(rd, rd, 64 - 16);
}

void TurboAssembler::Sra(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg())
    RV_sraw(rd, rs, rt.rm());
  else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    RV_sraiw(rd, rs, shamt);
  }
}

void TurboAssembler::Srl(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg())
    RV_srlw(rd, rs, rt.rm());
  else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    RV_srliw(rd, rs, shamt);
  }
}

void TurboAssembler::Dsra(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg())
    RV_sra(rd, rs, rt.rm());
  else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    RV_srai(rd, rs, shamt);
  }
}

void TurboAssembler::Dsrl(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg())
    RV_srl(rd, rs, rt.rm());
  else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    RV_srli(rd, rs, shamt);
  }
}

void TurboAssembler::Dsll(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg())
    RV_sll(rd, rs, rt.rm());
  else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    RV_slli(rd, rs, shamt);
  }
}

void TurboAssembler::Ror(Register rd, Register rs, const Operand& rt) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  DCHECK(rs != scratch);
  if (rt.is_reg()) {
    RV_negw(scratch, rt.rm());
    RV_sllw(scratch, rs, scratch);
    RV_srlw(rd, rs, rt.rm());
    RV_or_(rd, scratch, rd);
    RV_sext_w(rd, rd);
  } else {
    int64_t ror_value = rt.immediate() % 32;
    if (ror_value == 0) {
      RV_mv(rd, rs);
      return;
    } else if (ror_value < 0) {
      ror_value += 32;
    }
    RV_srliw(scratch, rs, ror_value);
    RV_slliw(rd, rs, 32 - ror_value);
    RV_or_(rd, scratch, rd);
    RV_sext_w(rd, rd);
  }
}

void TurboAssembler::Dror(Register rd, Register rs, const Operand& rt) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  DCHECK(rs != scratch);
  if (rt.is_reg()) {
    RV_negw(scratch, rt.rm());
    RV_sll(scratch, rs, scratch);
    RV_srl(rd, rs, rt.rm());
    RV_or_(rd, scratch, rd);
  } else {
    int64_t dror_value = rt.immediate() % 64;
    if (dror_value == 0) {
      RV_mv(rd, rs);
      return;
    } else if (dror_value < 0) {
      dror_value += 64;
    }
    RV_srli(scratch, rs, dror_value);
    RV_slli(rd, rs, 64 - dror_value);
    RV_or_(rd, scratch, rd);
  }
}

// rd <- rt != 0 ? rs : 0
void TurboAssembler::Selnez(Register rd, Register rs, const Operand& rt) {
  DCHECK(rt.is_reg());
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  RV_snez(scratch, rt.rm());  // scratch = 0 if rt is zero, 1 otherwise.
  RV_mul(rd, rs, scratch);    // scratch * rs = rs or zero
}

// rd <- rt == 0 ? rs : 0
void TurboAssembler::Seleqz(Register rd, Register rs, const Operand& rt) {
  DCHECK(rt.is_reg());
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  RV_seqz(scratch, rt.rm());  // scratch = 0 if rt is non-zero, 1 otherwise.
  RV_mul(rd, rs, scratch);    // scratch * rs = rs or zero
}

// void MacroAssembler::Pref(int32_t hint, const MemOperand& rs) {
//   pref(hint, rs);
// }

void TurboAssembler::Lsa(Register rd, Register rt, Register rs, uint8_t sa,
                         Register scratch) {
  DCHECK(sa >= 1 && sa <= 31);
  Register tmp = rd == rt ? scratch : rd;
  DCHECK(tmp != rt);
  RV_slliw(tmp, rs, sa);
  Addu(rd, rt, tmp);
}

void TurboAssembler::Dlsa(Register rd, Register rt, Register rs, uint8_t sa,
                          Register scratch) {
  DCHECK(sa >= 1 && sa <= 31);
  Register tmp = rd == rt ? scratch : rd;
  DCHECK(tmp != rt);
  RV_slli(tmp, rs, sa);
  Daddu(rd, rt, tmp);
}

// ------------Pseudo-instructions-------------
// Change endianness
void TurboAssembler::ByteSwap(Register rd, Register rs, int operand_size) {
  DCHECK(operand_size == 4 || operand_size == 8);
  DCHECK(rd != t5 && rd != t6);
  if (operand_size == 4) {
    // Uint32_t t5 = 0x00FF00FF;
    // x = (x << 16 | x >> 16);
    // x = (((x & t5) << 8)  | ((x & (t5 << 8)) >> 8));
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register x = temps.Acquire();
    li(t5, 0x00FF00FF);
    RV_slliw(x, rs, 16);
    RV_srliw(rd, rs, 16);
    RV_or_(x, rd, x);     // x <- x << 16 | x >> 16
    RV_and_(t6, x, t5);   // t <- x & 0x00FF00FF
    RV_slliw(t6, t6, 8);  // t <- (x & t5) << 8
    RV_slliw(t5, t5, 8);  // t5 <- 0xFF00FF00
    RV_and_(rd, x, t5);   // x & 0xFF00FF00
    RV_srliw(rd, rd, 8);
    RV_or_(rd, rd, t6);  // (((x & t5) << 8)  | ((x & (t5 << 8)) >> 8))
  } else {
    // uint64_t t5 = 0x0000FFFF0000FFFFl;
    // uint64_t t5 = 0x00FF00FF00FF00FFl;
    // x = (x << 32 | x >> 32);
    // x = (x & t5) << 16 | (x & (t5 << 16)) >> 16;
    // x = (x & t5) << 8  | (x & (t5 << 8)) >> 8;
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register x = temps.Acquire();
    li(t5, 0x0000FFFF0000FFFFl);
    RV_slli(x, rs, 32);
    RV_srli(rd, rs, 32);
    RV_or_(x, rd, x);     // x <- x << 32 | x >> 32
    RV_and_(t6, x, t5);   // t <- x & 0x0000FFFF0000FFFF
    RV_slli(t6, t6, 16);  // t <- (x & 0x0000FFFF0000FFFF) << 16
    RV_slli(t5, t5, 16);  // t5 <- 0xFFFF0000FFFF0000
    RV_and_(rd, x, t5);   // rd <- x & 0xFFFF0000FFFF0000
    RV_srli(rd, rd, 16);  // rd <- x & (t5 << 16)) >> 16
    RV_or_(x, rd, t6);    // (x & t5) << 16 | (x & (t5 << 16)) >> 16;
    li(t5, 0x00FF00FF00FF00FFl);
    RV_and_(t6, x, t5);  // t <- x & 0x00FF00FF00FF00FF
    RV_slli(t6, t6, 8);  // t <- (x & t5) << 8
    RV_slli(t5, t5, 8);  // t5 <- 0xFF00FF00FF00FF00
    RV_and_(rd, x, t5);
    RV_srli(rd, rd, 8);  // rd <- (x & (t5 << 8)) >> 8
    RV_or_(rd, rd, t6);  // (((x & t5) << 8)  | ((x & (t5 << 8)) >> 8))
  }
}

template <int NBYTES, bool LOAD_SIGNED>
void TurboAssembler::LoadNBytes(Register rd, const MemOperand& rs,
                                Register scratch) {
  DCHECK(rd != rs.rm() && rd != scratch);
  DCHECK(NBYTES <= 8);

  // load the most significant byte
  if (LOAD_SIGNED) {
    RV_lb(rd, rs.rm(), rs.offset() + (NBYTES - 1));
  } else {
    RV_lbu(rd, rs.rm(), rs.offset() + (NBYTES - 1));
  }

  // load remaining (nbytes-1) bytes from higher to lower
  RV_slli(rd, rd, 8 * (NBYTES - 1));
  for (int i = (NBYTES - 2); i >= 0; i--) {
    RV_lbu(scratch, rs.rm(), rs.offset() + i);
    if (i) RV_slli(scratch, scratch, i * 8);
    RV_or_(rd, rd, scratch);
  }
}

template <int NBYTES, bool LOAD_SIGNED>
void TurboAssembler::LoadNBytesOverwritingBaseReg(const MemOperand& rs,
                                                  Register scratch0,
                                                  Register scratch1) {
  // This function loads nbytes from memory specified by rs and into rs.rm()
  DCHECK(rs.rm() != scratch0 && rs.rm() != scratch1 && scratch0 != scratch1);
  DCHECK(NBYTES <= 8);

  // load the most significant byte
  if (LOAD_SIGNED) {
    RV_lb(scratch0, rs.rm(), rs.offset() + (NBYTES - 1));
  } else {
    RV_lbu(scratch0, rs.rm(), rs.offset() + (NBYTES - 1));
  }

  // load remaining (nbytes-1) bytes from higher to lower
  RV_slli(scratch0, scratch0, 8 * (NBYTES - 1));
  for (int i = (NBYTES - 2); i >= 0; i--) {
    RV_lbu(scratch1, rs.rm(), rs.offset() + i);
    if (i) {
      RV_slli(scratch1, scratch1, i * 8);
      RV_or_(scratch0, scratch0, scratch1);
    } else {
      // write to rs.rm() when processing the last byte
      RV_or_(rs.rm(), scratch0, scratch1);
    }
  }
}

template <int NBYTES, bool IS_SIGNED>
void TurboAssembler::UnalignedLoadHelper(Register rd, const MemOperand& rs) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  UseScratchRegisterScope temps(this);

  if (NeedAdjustBaseAndOffset(rs, OffsetAccessType::TWO_ACCESSES, NBYTES - 1)) {
    // Adjust offset for two accesses and check if offset + 3 fits into int12.
    MemOperand source = rs;
    Register scratch_base = temps.Acquire();
    DCHECK(scratch_base != rs.rm());
    AdjustBaseAndOffset(&source, scratch_base, OffsetAccessType::TWO_ACCESSES,
                        NBYTES - 1);

    // Since source.rm() is scratch_base, assume rd != source.rm()
    DCHECK(rd != source.rm());
    Register scratch_other = t5;
    LoadNBytes<NBYTES, IS_SIGNED>(rd, source, scratch_other);
  } else {
    // no need to adjust base-and-offset
    if (rd != rs.rm()) {
      Register scratch = temps.Acquire();
      LoadNBytes<NBYTES, IS_SIGNED>(rd, rs, scratch);
    } else {  // rd == rs.rm()
      Register scratch0 = temps.Acquire();
      Register scratch1 = t5;
      LoadNBytesOverwritingBaseReg<NBYTES, IS_SIGNED>(rs, scratch0, scratch1);
    }
  }
}

template <int NBYTES>
void TurboAssembler::UnalignedFLoadHelper(FPURegister frd, const MemOperand& rs,
                                          Register scratch) {
  DCHECK(scratch != rs.rm());
  DCHECK(NBYTES == 4 || NBYTES == 8);

  BlockTrampolinePoolScope block_trampoline_pool(this);
  UseScratchRegisterScope temps(this);
  MemOperand source = rs;
  if (NeedAdjustBaseAndOffset(rs, OffsetAccessType::TWO_ACCESSES, NBYTES - 1)) {
    // Adjust offset for two accesses and check if offset + 3 fits into int12.
    Register scratch_base = temps.Acquire();
    DCHECK(scratch_base != scratch && scratch_base != rs.rm());
    AdjustBaseAndOffset(&source, scratch_base, OffsetAccessType::TWO_ACCESSES,
                        NBYTES - 1);
  }

  Register scratch_other = temps.hasAvailable() ? temps.Acquire() : t5;
  DCHECK(scratch_other != scratch && scratch_other != rs.rm());
  LoadNBytes<NBYTES, true>(scratch, source, scratch_other);
  if (NBYTES == 4)
    RV_fmv_w_x(frd, scratch);
  else
    RV_fmv_d_x(frd, scratch);
}

template <int NBYTES>
void TurboAssembler::UnalignedStoreHelper(Register rd, const MemOperand& rs,
                                          Register scratch_other) {
  DCHECK(scratch_other != rs.rm());
  DCHECK(NBYTES <= 8);

  UseScratchRegisterScope temps(this);
  MemOperand source = rs;
  // Adjust offset for two accesses and check if offset + 3 fits into int12.
  if (NeedAdjustBaseAndOffset(rs, OffsetAccessType::TWO_ACCESSES, NBYTES - 1)) {
    Register scratch_base = temps.Acquire();
    DCHECK(scratch_base != rd && scratch_base != rs.rm());
    AdjustBaseAndOffset(&source, scratch_base, OffsetAccessType::TWO_ACCESSES,
                        NBYTES - 1);
  }

  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (scratch_other == no_reg)
    scratch_other = temps.hasAvailable() ? temps.Acquire() : t5;

  DCHECK(scratch_other != rd && scratch_other != rs.rm() &&
         scratch_other != source.rm());

  RV_sb(rd, source.rm(), source.offset());
  for (size_t i = 1; i <= (NBYTES - 1); i++) {
    RV_srli(scratch_other, rd, i * 8);
    RV_sb(scratch_other, source.rm(), source.offset() + i);
  }
}

template <int NBYTES>
void TurboAssembler::UnalignedFStoreHelper(FPURegister frd,
                                           const MemOperand& rs,
                                           Register scratch) {
  DCHECK(scratch != rs.rm());
  DCHECK(NBYTES == 8 || NBYTES == 4);

  if (NBYTES == 4) {
    RV_fmv_x_w(scratch, frd);
  } else {
    RV_fmv_x_d(scratch, frd);
  }
  UnalignedStoreHelper<NBYTES>(scratch, rs);
}

template <typename Reg_T, typename Func>
void TurboAssembler::AlignedLoadHelper(Reg_T target, const MemOperand& rs,
                                       Func generator) {
  MemOperand source = rs;
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (NeedAdjustBaseAndOffset(source)) {
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    DCHECK(scratch != rs.rm());
    AdjustBaseAndOffset(&source, scratch);
  }
  generator(target, source);
}

template <typename Reg_T, typename Func>
void TurboAssembler::AlignedStoreHelper(Reg_T value, const MemOperand& rs,
                                        Func generator) {
  MemOperand source = rs;
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (NeedAdjustBaseAndOffset(source)) {
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    // make sure scratch does not overwrite value
    if (std::is_same<Reg_T, Register>::value)
      DCHECK(scratch.code() != value.code());
    DCHECK(scratch != rs.rm());
    AdjustBaseAndOffset(&source, scratch);
  }
  generator(value, source);
}

void TurboAssembler::Ulw(Register rd, const MemOperand& rs) {
  UnalignedLoadHelper<4, true>(rd, rs);
}

void TurboAssembler::Ulwu(Register rd, const MemOperand& rs) {
  UnalignedLoadHelper<4, false>(rd, rs);
}

void TurboAssembler::Usw(Register rd, const MemOperand& rs) {
  UnalignedStoreHelper<4>(rd, rs);
}

void TurboAssembler::Ulh(Register rd, const MemOperand& rs) {
  UnalignedLoadHelper<2, true>(rd, rs);
}

void TurboAssembler::Ulhu(Register rd, const MemOperand& rs) {
  UnalignedLoadHelper<2, false>(rd, rs);
}

void TurboAssembler::Ush(Register rd, const MemOperand& rs) {
  UnalignedStoreHelper<2>(rd, rs);
}

void TurboAssembler::Uld(Register rd, const MemOperand& rs) {
  UnalignedLoadHelper<8, true>(rd, rs);
}

// Load consequent 32-bit word pair in 64-bit reg. and put first word in low
// bits,
// second word in high bits.
void MacroAssembler::LoadWordPair(Register rd, const MemOperand& rs,
                                  Register scratch) {
  Lwu(rd, rs);
  Lw(scratch, MemOperand(rs.rm(), rs.offset() + kPointerSize / 2));
  RV_slli(scratch, scratch, 32);
  Daddu(rd, rd, scratch);
}

void TurboAssembler::Usd(Register rd, const MemOperand& rs) {
  UnalignedStoreHelper<8>(rd, rs);
}

// Do 64-bit store as two consequent 32-bit stores to unaligned address.
void MacroAssembler::StoreWordPair(Register rd, const MemOperand& rs,
                                   Register scratch) {
  Sw(rd, rs);
  RV_srai(scratch, rd, 32);
  Sw(scratch, MemOperand(rs.rm(), rs.offset() + kPointerSize / 2));
}

void TurboAssembler::Ulwc1(FPURegister fd, const MemOperand& rs,
                           Register scratch) {
  UnalignedFLoadHelper<4>(fd, rs, scratch);
}

void TurboAssembler::Uswc1(FPURegister fd, const MemOperand& rs,
                           Register scratch) {
  UnalignedFStoreHelper<4>(fd, rs, scratch);
}

void TurboAssembler::Uldc1(FPURegister fd, const MemOperand& rs,
                           Register scratch) {
  UnalignedFLoadHelper<8>(fd, rs, scratch);
}

void TurboAssembler::Usdc1(FPURegister fd, const MemOperand& rs,
                           Register scratch) {
  UnalignedFStoreHelper<8>(fd, rs, scratch);
}

void TurboAssembler::Lb(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    this->RV_lb(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(rd, rs, fn);
}

void TurboAssembler::Lbu(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    this->RV_lbu(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(rd, rs, fn);
}

void TurboAssembler::Sb(Register rd, const MemOperand& rs) {
  auto fn = [this](Register value, const MemOperand& source) {
    this->RV_sb(value, source.rm(), source.offset());
  };
  AlignedStoreHelper(rd, rs, fn);
}

void TurboAssembler::Lh(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    this->RV_lh(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(rd, rs, fn);
}

void TurboAssembler::Lhu(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    this->RV_lhu(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(rd, rs, fn);
}

void TurboAssembler::Sh(Register rd, const MemOperand& rs) {
  auto fn = [this](Register value, const MemOperand& source) {
    this->RV_sh(value, source.rm(), source.offset());
  };
  AlignedStoreHelper(rd, rs, fn);
}

void TurboAssembler::Lw(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    this->RV_lw(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(rd, rs, fn);
}

void TurboAssembler::Lwu(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    this->RV_lwu(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(rd, rs, fn);
}

void TurboAssembler::Sw(Register rd, const MemOperand& rs) {
  auto fn = [this](Register value, const MemOperand& source) {
    this->RV_sw(value, source.rm(), source.offset());
  };
  AlignedStoreHelper(rd, rs, fn);
}

void TurboAssembler::Ld(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    this->RV_ld(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(rd, rs, fn);
}

void TurboAssembler::Sd(Register rd, const MemOperand& rs) {
  auto fn = [this](Register value, const MemOperand& source) {
    this->RV_sd(value, source.rm(), source.offset());
  };
  AlignedStoreHelper(rd, rs, fn);
}

void TurboAssembler::Lwc1(FPURegister fd, const MemOperand& src) {
  auto fn = [this](FPURegister target, const MemOperand& source) {
    this->RV_flw(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(fd, src, fn);
}

void TurboAssembler::Swc1(FPURegister fs, const MemOperand& src) {
  auto fn = [this](FPURegister value, const MemOperand& source) {
    this->RV_fsw(value, source.rm(), source.offset());
  };
  AlignedStoreHelper(fs, src, fn);
}

void TurboAssembler::Ldc1(FPURegister fd, const MemOperand& src) {
  auto fn = [this](FPURegister target, const MemOperand& source) {
    this->RV_fld(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(fd, src, fn);
}

void TurboAssembler::Sdc1(FPURegister fs, const MemOperand& src) {
  auto fn = [this](FPURegister value, const MemOperand& source) {
    this->RV_fsd(value, source.rm(), source.offset());
  };
  AlignedStoreHelper(fs, src, fn);
}

void TurboAssembler::Ll(Register rd, const MemOperand& rs) {
  bool is_one_instruction = rs.offset() == 0;
  if (is_one_instruction) {
    RV_lr_w(false, false, rd, rs.rm());
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Daddu(scratch, rs.rm(), rs.offset());
    RV_lr_w(false, false, rd, scratch);
  }
}

void TurboAssembler::Lld(Register rd, const MemOperand& rs) {
  bool is_one_instruction = rs.offset() == 0;
  if (is_one_instruction) {
    RV_lr_d(false, false, rd, rs.rm());
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Daddu(scratch, rs.rm(), rs.offset());
    RV_lr_d(false, false, rd, scratch);
  }
}

void TurboAssembler::Sc(Register rd, const MemOperand& rs) {
  bool is_one_instruction = rs.offset() == 0;
  if (is_one_instruction) {
    RV_sc_w(false, false, rd, rs.rm(), rd);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Daddu(scratch, rs.rm(), rs.offset());
    RV_sc_w(false, false, rd, scratch, rd);
  }
}

void TurboAssembler::Scd(Register rd, const MemOperand& rs) {
  bool is_one_instruction = rs.offset() == 0;
  if (is_one_instruction) {
    RV_sc_d(false, false, rd, rs.rm(), rd);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Daddu(scratch, rs.rm(), rs.offset());
    RV_sc_d(false, false, rd, scratch, rd);
  }
}

void TurboAssembler::li(Register dst, Handle<HeapObject> value, LiFlags mode) {
  // TODO(jgruber,v8:8887): Also consider a root-relative load when generating
  // non-isolate-independent code. In many cases it might be cheaper than
  // embedding the relocatable value.
  if (root_array_available_ && options().isolate_independent_code) {
    IndirectLoadConstant(dst, value);
    return;
  }
  li(dst, Operand(value), mode);
}

void TurboAssembler::li(Register dst, ExternalReference value, LiFlags mode) {
  // TODO(jgruber,v8:8887): Also consider a root-relative load when generating
  // non-isolate-independent code. In many cases it might be cheaper than
  // embedding the relocatable value.
  if (root_array_available_ && options().isolate_independent_code) {
    IndirectLoadExternalReference(dst, value);
    return;
  }
  li(dst, Operand(value), mode);
}

void TurboAssembler::li(Register dst, const StringConstantBase* string,
                        LiFlags mode) {
  li(dst, Operand::EmbeddedStringConstant(string), mode);
}

static inline int InstrCountForLiLower32Bit(int64_t value) {
  int64_t Hi20 = ((value + 0x800) >> 12);
  int64_t Lo12 = value << 52 >> 52;
  if (Hi20 == 0 || Lo12 == 0) {
    return 1;
  }
  return 2;
}

int TurboAssembler::InstrCountForLi64Bit(int64_t value) {
  if (is_int32(value)) {
    return InstrCountForLiLower32Bit(value);
  } else {
    return li_count(value);
  }
  UNREACHABLE();
  return INT_MAX;
}

void TurboAssembler::li_optimized(Register rd, Operand j, LiFlags mode) {
  DCHECK(!j.is_reg());
  DCHECK(!MustUseReg(j.rmode()));
  DCHECK(mode == OPTIMIZE_SIZE);
  RV_li(rd, j.immediate());
}

void TurboAssembler::li(Register rd, Operand j, LiFlags mode) {
  DCHECK(!j.is_reg());
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (!MustUseReg(j.rmode()) && mode == OPTIMIZE_SIZE) {
    RV_li(rd, j.immediate());
  } else if (MustUseReg(j.rmode())) {
    int64_t immediate;
    if (j.IsHeapObjectRequest()) {
      RequestHeapObject(j.heap_object_request());
      immediate = 0;
    } else {
      immediate = j.immediate();
    }

    RecordRelocInfo(j.rmode(), immediate);
    // FIXME(RISC_V): Does this case need to be constant size?
    RV_li_constant(rd, immediate);
  } else if (mode == ADDRESS_LOAD) {
    // We always need the same number of instructions as we may need to patch
    // this code to load another value which may need all 8 instructions.
    RV_li_constant(rd, j.immediate());
  } else {  // mode == CONSTANT_SIZE - always emit the same instruction
            // sequence.
    RV_li_constant(rd, j.immediate());
  }
}

static RegList t_regs = Register::ListOf(t0, t1, t2, t3, t4, t5, t6);
static RegList a_regs = Register::ListOf(a0, a1, a2, a3, a4, a5, a6, a7);
static RegList s_regs =
    Register::ListOf(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11);

void TurboAssembler::MultiPush(RegList regs) {
  int16_t num_to_push = base::bits::CountPopulation(regs);
  int16_t stack_offset = num_to_push * kPointerSize;

#define TEST_AND_PUSH_REG(reg)             \
  if ((regs & reg.bit()) != 0) {           \
    stack_offset -= kPointerSize;          \
    Sd(reg, MemOperand(sp, stack_offset)); \
    regs &= ~reg.bit();                    \
  }

#define T_REGS(V) V(t6) V(t5) V(t4) V(t3) V(t2) V(t1) V(t0)
#define A_REGS(V) V(a7) V(a6) V(a5) V(a4) V(a3) V(a2) V(a1) V(a0)
#define S_REGS(V) \
  V(s11) V(s10) V(s9) V(s8) V(s7) V(s6) V(s5) V(s4) V(s3) V(s2) V(s1)

  Dsubu(sp, sp, Operand(stack_offset));

  // Certain usage of MultiPush requires that registers are pushed onto the
  // stack in a particular: ra, fp, sp, gp, .... (basically in the decreasing
  // order of register numbers according to MIPS register numbers)
  TEST_AND_PUSH_REG(ra);
  TEST_AND_PUSH_REG(fp);
  TEST_AND_PUSH_REG(sp);
  TEST_AND_PUSH_REG(gp);
  TEST_AND_PUSH_REG(tp);
  if ((regs & s_regs) != 0) {
    S_REGS(TEST_AND_PUSH_REG)
  }
  if ((regs & a_regs) != 0) {
    A_REGS(TEST_AND_PUSH_REG)
  }
  if ((regs & t_regs) != 0) {
    T_REGS(TEST_AND_PUSH_REG)
  }

  DCHECK(regs == 0);

#undef TEST_AND_PUSH_REG
#undef T_REGS
#undef A_REGS
#undef S_REGS
}

void TurboAssembler::MultiPop(RegList regs) {
  int16_t stack_offset = 0;

#define TEST_AND_POP_REG(reg)              \
  if ((regs & reg.bit()) != 0) {           \
    Ld(reg, MemOperand(sp, stack_offset)); \
    stack_offset += kPointerSize;          \
    regs &= ~reg.bit();                    \
  }

#define T_REGS(V) V(t0) V(t1) V(t2) V(t3) V(t4) V(t5) V(t6)
#define A_REGS(V) V(a0) V(a1) V(a2) V(a3) V(a4) V(a5) V(a6) V(a7)
#define S_REGS(V) \
  V(s1) V(s2) V(s3) V(s4) V(s5) V(s6) V(s7) V(s8) V(s9) V(s10) V(s11)

  // MultiPop pops from the stack in reverse order as MultiPush
  if ((regs & t_regs) != 0) {
    T_REGS(TEST_AND_POP_REG)
  }
  if ((regs & a_regs) != 0) {
    A_REGS(TEST_AND_POP_REG)
  }
  if ((regs & s_regs) != 0) {
    S_REGS(TEST_AND_POP_REG)
  }
  TEST_AND_POP_REG(tp);
  TEST_AND_POP_REG(gp);
  TEST_AND_POP_REG(sp);
  TEST_AND_POP_REG(fp);
  TEST_AND_POP_REG(ra);

  DCHECK(regs == 0);

  RV_addi(sp, sp, stack_offset);

#undef TEST_AND_POP_REG
#undef T_REGS
#undef S_REGS
#undef A_REGS
}

void TurboAssembler::MultiPushFPU(RegList regs) {
  int16_t num_to_push = base::bits::CountPopulation(regs);
  int16_t stack_offset = num_to_push * kDoubleSize;

  Dsubu(sp, sp, Operand(stack_offset));
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs & (1 << i)) != 0) {
      stack_offset -= kDoubleSize;
      Sdc1(FPURegister::from_code(i), MemOperand(sp, stack_offset));
    }
  }
}

void TurboAssembler::MultiPopFPU(RegList regs) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs & (1 << i)) != 0) {
      Ldc1(FPURegister::from_code(i), MemOperand(sp, stack_offset));
      stack_offset += kDoubleSize;
    }
  }
  RV_addi(sp, sp, stack_offset);
}

void TurboAssembler::Ext(Register rt, Register rs, uint16_t pos,
                         uint16_t size) {
  DCHECK_LT(pos, 32);
  DCHECK_LT(pos + size, 33);
  // RISC-V does not have an extract-type instruction, so we need to use shifts
  RV_slliw(rt, rs, 32 - (pos + size));
  RV_srliw(rt, rt, 32 - size);
}

void TurboAssembler::Dext(Register rt, Register rs, uint16_t pos,
                          uint16_t size) {
  DCHECK(pos < 64 && 0 < size && size <= 64 && 0 < pos + size &&
         pos + size <= 64);
  // RISC-V does not have an extract-type instruction, so we need to use shifts
  RV_slli(rt, rs, 64 - (pos + size));
  RV_srli(rt, rt, 64 - size);
}

void TurboAssembler::Ins(Register rt, Register rs, uint16_t pos,
                         uint16_t size) {
  DCHECK_LT(pos, 32);
  DCHECK_LE(pos + size, 32);
  DCHECK_NE(size, 0);
  DCHECK(rt != t5 && rt != t6 && rs != t5 && rs != t6);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch1 = t5;

  uint32_t src_mask = (1 << size) - 1;
  uint32_t dest_mask = ~(src_mask << pos);

  And(scratch1, rs, Operand(src_mask));
  RV_slliw(scratch1, scratch1, pos);
  And(rt, rt, Operand(dest_mask));
  RV_or_(rt, rt, scratch1);
}

void TurboAssembler::Dins(Register rt, Register rs, uint16_t pos,
                          uint16_t size) {
  DCHECK(pos < 64 && 0 < size && size <= 64 && 0 < pos + size &&
         pos + size <= 64);
  DCHECK(rt != t5 && rt != t6 && rs != t5 && rs != t6);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch1 = t5;

  uint64_t src_mask = (1 << size) - 1;
  uint64_t dest_mask = ~(src_mask << pos);

  And(scratch1, rs, Operand(src_mask));
  RV_slli(scratch1, scratch1, pos);
  And(rt, rt, Operand(dest_mask));
  RV_or_(rt, rt, scratch1);
}

void TurboAssembler::ExtractBits(Register dest, Register source, Register pos,
                                 int size, bool sign_extend) {
  RV_sra(dest, source, pos);
  Dext(dest, dest, 0, size);
  if (sign_extend) {
    switch (size) {
      case 8:
        RV_slli(dest, dest, 56);
        RV_srai(dest, dest, 56);
        break;
      case 16:
        RV_slli(dest, dest, 48);
        RV_srai(dest, dest, 48);
        break;
      case 32:
        // sign-extend word
        RV_sext_w(dest, dest);
        break;
      default:
        UNREACHABLE();
    }
  }
}

void TurboAssembler::InsertBits(Register dest, Register source, Register pos,
                                int size) {
  Dror(dest, dest, pos);
  Dins(dest, source, 0, size);
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Dsubu(scratch, zero_reg, pos);
    Dror(dest, dest, scratch);
  }
}

void TurboAssembler::Neg_s(FPURegister fd, FPURegister fs) {
  RV_fneg_s(fd, fs);
}

void TurboAssembler::Neg_d(FPURegister fd, FPURegister fs) {
  RV_fneg_d(fd, fs);
}

void TurboAssembler::Cvt_d_uw(FPURegister fd, Register rs) {
  // Convert rs to a FP value in fd.
  RV_fcvt_d_wu(fd, rs);
}

void TurboAssembler::Cvt_d_w(FPURegister fd, Register rs) {
  // Convert rs to a FP value in fd.
  RV_fcvt_d_w(fd, rs);
}

void TurboAssembler::Cvt_d_ul(FPURegister fd, Register rs) {
  // Convert rs to a FP value in fd.
  RV_fcvt_d_lu(fd, rs);
}

void TurboAssembler::Cvt_s_uw(FPURegister fd, Register rs) {
  // Convert rs to a FP value in fd.
  RV_fcvt_s_wu(fd, rs);
}

void TurboAssembler::Cvt_s_w(FPURegister fd, Register rs) {
  // Convert rs to a FP value in fd.
  RV_fcvt_s_w(fd, rs);
}

void TurboAssembler::Cvt_s_ul(FPURegister fd, Register rs) {
  // Convert rs to a FP value in fd.
  RV_fcvt_s_lu(fd, rs);
}

template <typename CvtFunc>
void TurboAssembler::RoundFloatingPointToInteger(Register rd, FPURegister fs,
                                                 Register result,
                                                 CvtFunc fcvt_generator) {
  if (result.is_valid()) {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    UseScratchRegisterScope temps(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;

    // Save csr_fflags to scratch & clear exception flags
    int exception_flags = kInvalidOperation;
    RV_csrrci(scratch, csr_fflags, exception_flags);

    // actual conversion instruction
    fcvt_generator(this, rd, fs);

    // check kInvalidOperation flag (out-of-range, NaN)
    // set result to 1 if normal, otherwise set result to 0 for abnormal
    RV_frflags(result);
    RV_andi(result, result, exception_flags);
    RV_seqz(result, result);  // result <-- 1 (normal), result <-- 0 (abnormal)

    // restore csr_fflags
    RV_csrw(csr_fflags, scratch);
  } else {
    // actual conversion instruction
    fcvt_generator(this, rd, fs);
  }
}

void MacroAssembler::Round_l_d(FPURegister fd, FPURegister fs) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  RV_fcvt_l_d(scratch, fs, RNE);
  RV_fmv_d_x(fd, scratch);
}

void MacroAssembler::Floor_l_d(FPURegister fd, FPURegister fs) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  RV_fcvt_l_d(scratch, fs, RDN);
  RV_fmv_d_x(fd, scratch);
}

void MacroAssembler::Ceil_l_d(FPURegister fd, FPURegister fs) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  RV_fcvt_l_d(scratch, fs, RUP);
  RV_fmv_d_x(fd, scratch);
}

void MacroAssembler::Round_w_d(FPURegister fd, FPURegister fs) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  RV_fcvt_w_d(scratch, fs, RNE);
  RV_fmv_w_x(fd, scratch);
}

void MacroAssembler::Floor_w_d(FPURegister fd, FPURegister fs) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  RV_fcvt_w_d(scratch, fs, RDN);
  RV_fmv_w_x(fd, scratch);
}

void MacroAssembler::Ceil_w_d(FPURegister fd, FPURegister fs) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  RV_fcvt_w_d(scratch, fs, RUP);
  RV_fmv_w_x(fd, scratch);
}

void TurboAssembler::Trunc_uw_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_wu_d(dst, src, RTZ);
      });
}

void TurboAssembler::Trunc_w_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_w_d(dst, src, RTZ);
      });
}

void TurboAssembler::Trunc_uw_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_wu_s(dst, src, RTZ);
      });
}

void TurboAssembler::Trunc_w_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_w_s(dst, src, RTZ);
      });
}

void TurboAssembler::Trunc_ul_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_lu_d(dst, src, RTZ);
      });
}

void TurboAssembler::Trunc_l_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_l_d(dst, src, RTZ);
      });
}

void TurboAssembler::Trunc_ul_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_lu_s(dst, src, RTZ);
      });
}

void TurboAssembler::Trunc_l_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_l_s(dst, src, RTZ);
      });
}

void TurboAssembler::Round_w_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_w_s(dst, src, RNE);
      });
}

void TurboAssembler::Round_w_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_w_d(dst, src, RNE);
      });
}

void TurboAssembler::Ceil_w_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_w_s(dst, src, RUP);
      });
}

void TurboAssembler::Ceil_w_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_w_d(dst, src, RUP);
      });
}

void TurboAssembler::Floor_w_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_w_s(dst, src, RDN);
      });
}

void TurboAssembler::Floor_w_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](TurboAssembler* tasm, Register dst, FPURegister src) {
        tasm->RV_fcvt_w_d(dst, src, RDN);
      });
}

// According to JS ECMA specification, for floating-point round operations, if
// the input is NaN, +/-infinity, or +/-0, the same input is returned as the
// rounded result; this differs from behavior of RISCV fcvt instructions (which
// round out-of-range values to the nearest max or min value), therefore special
// handling is needed by NaN, +/-Infinity, +/-0
template <typename F>
void TurboAssembler::RoundHelper(FPURegister dst, FPURegister src,
                                 FPURegister fpu_scratch, RoundingMode frm) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;

  DCHECK((std::is_same<float, F>::value) || (std::is_same<double, F>::value));
  // Need at least two FPRs, so check against dst == src == fpu_scratch
  DCHECK(!(dst == src && dst == fpu_scratch));

  const int kFloat32ExponentBias = 127;
  const int kFloat32MantissaBits = 23;
  const int kFloat32ExponentBits = 8;
  const int kFloat64ExponentBias = 1023;
  const int kFloat64MantissaBits = 52;
  const int kFloat64ExponentBits = 11;
  const int kFloatMantissaBits =
      sizeof(F) == 4 ? kFloat32MantissaBits : kFloat64MantissaBits;
  const int kFloatExponentBits =
      sizeof(F) == 4 ? kFloat32ExponentBits : kFloat64ExponentBits;
  const int kFloatExponentBias =
      sizeof(F) == 4 ? kFloat32ExponentBias : kFloat64ExponentBias;

  Label done;

  // extract exponent value of the source floating-point to t6
  if (std::is_same<F, double>::value) {
    RV_fmv_x_d(scratch, src);
    Dext(t6, scratch, kFloatMantissaBits, kFloatExponentBits);
  } else {
    RV_fmv_x_w(scratch, src);
    Ext(t6, scratch, kFloatMantissaBits, kFloatExponentBits);
  }

  // if src is NaN/+-Infinity/+-Zero or if the exponent is larger than # of bits
  // in mantissa, the result is the same as src, so move src to dest  (to avoid
  // generating another branch)
  if (std::is_same<F, double>::value) {
    Move_d(dst, src);
  } else {
    Move_s(dst, src);
  }

  // If real exponent (i.e., t6 - kFloatExponentBias) is greater than
  // kFloat32MantissaBits, it means the floating-point value has no fractional
  // part, thus the input is already rounded, jump to done. Note that, NaN and
  // Infinity in floating-point representation sets maximal exponent value, so
  // they also satisfy (t6-kFloatExponentBias >= kFloatMantissaBits), and JS
  // round semantics specify that rounding of NaN (Infinity) returns NaN
  // (Infinity), so NaN and Infinity are considered rounded value too.
  Branch(&done, greater_equal, t6,
         Operand(kFloatExponentBias + kFloatMantissaBits));

  // Actual rounding is needed along this path

  // old_src holds the original input, needed for the case of src == dst
  FPURegister old_src = src;
  if (src == dst) {
    DCHECK(fpu_scratch != dst);
    Move(fpu_scratch, src);
    old_src = fpu_scratch;
  }

  // Since only input whose real exponent value is less than kMantissaBits
  // (i.e., 23 or 52-bits) falls into this path, the value range of the input
  // falls into that of 23- or 53-bit integers. So we round the input to integer
  // values, then convert them back to floating-point.
  if (std::is_same<F, double>::value) {
    RV_fcvt_l_d(scratch, src, frm);
    RV_fcvt_d_l(dst, scratch, frm);
  } else {
    RV_fcvt_w_s(scratch, src, frm);
    RV_fcvt_s_w(dst, scratch, frm);
  }

  // A special handling is needed if the input is a very small positive/negative
  // number that rounds to zero. JS semantics requires that the rounded result
  // retains the sign of the input, so a very small positive (negative)
  // floating-point number should be rounded to positive (negative) 0.
  // Therefore, we use sign-bit injection to produce +/-0 correctly. Instead of
  // testing for zero w/ a branch, we just insert sign-bit for everyone on this
  // path (this is where old_src is needed)
  if (std::is_same<F, double>::value) {
    RV_fsgnj_d(dst, dst, old_src);
  } else {
    RV_fsgnj_s(dst, dst, old_src);
  }

  bind(&done);
}

void TurboAssembler::Floor_d_d(FPURegister dst, FPURegister src,
                               FPURegister fpu_scratch) {
  RoundHelper<double>(dst, src, fpu_scratch, RDN);
}

void TurboAssembler::Ceil_d_d(FPURegister dst, FPURegister src,
                              FPURegister fpu_scratch) {
  RoundHelper<double>(dst, src, fpu_scratch, RUP);
}

void TurboAssembler::Trunc_d_d(FPURegister dst, FPURegister src,
                               FPURegister fpu_scratch) {
  RoundHelper<double>(dst, src, fpu_scratch, RTZ);
}

void TurboAssembler::Round_d_d(FPURegister dst, FPURegister src,
                               FPURegister fpu_scratch) {
  RoundHelper<double>(dst, src, fpu_scratch, RNE);
}

void TurboAssembler::Floor_s_s(FPURegister dst, FPURegister src,
                               FPURegister fpu_scratch) {
  RoundHelper<float>(dst, src, fpu_scratch, RDN);
}

void TurboAssembler::Ceil_s_s(FPURegister dst, FPURegister src,
                              FPURegister fpu_scratch) {
  RoundHelper<float>(dst, src, fpu_scratch, RUP);
}

void TurboAssembler::Trunc_s_s(FPURegister dst, FPURegister src,
                               FPURegister fpu_scratch) {
  RoundHelper<float>(dst, src, fpu_scratch, RTZ);
}

void TurboAssembler::Round_s_s(FPURegister dst, FPURegister src,
                               FPURegister fpu_scratch) {
  RoundHelper<float>(dst, src, fpu_scratch, RNE);
}

void MacroAssembler::Madd_s(FPURegister fd, FPURegister fr, FPURegister fs,
                            FPURegister ft) {
  RV_fmadd_s(fd, fs, ft, fr);
}

void MacroAssembler::Madd_d(FPURegister fd, FPURegister fr, FPURegister fs,
                            FPURegister ft) {
  RV_fmadd_d(fd, fs, ft, fr);
}

void MacroAssembler::Msub_s(FPURegister fd, FPURegister fr, FPURegister fs,
                            FPURegister ft) {
  RV_fmsub_s(fd, fs, ft, fr);
}

void MacroAssembler::Msub_d(FPURegister fd, FPURegister fr, FPURegister fs,
                            FPURegister ft) {
  RV_fmsub_d(fd, fs, ft, fr);
}

void TurboAssembler::CompareF32(Register rd, FPUCondition cc, FPURegister cmp1,
                                FPURegister cmp2) {
  switch (cc) {
    case EQ:  // Equal.
      RV_feq_s(rd, cmp1, cmp2);
      break;
    case LT:  // Ordered and Less Than
      RV_flt_s(rd, cmp1, cmp2);
      break;
    case LE:  // Ordered and Less Than or Equal
      RV_fle_s(rd, cmp1, cmp2);
      break;
    default:
      UNREACHABLE();
  }
}

void TurboAssembler::CompareF64(Register rd, FPUCondition cc, FPURegister cmp1,
                                FPURegister cmp2) {
  switch (cc) {
    case EQ:  // Equal.
      RV_feq_d(rd, cmp1, cmp2);
      break;
    case LT:  // Ordered or Less Than, on Mips release >= 6.
      RV_flt_d(rd, cmp1, cmp2);
      break;
    case LE:  // Ordered or Less Than or Equal, on Mips release >= 6.
      RV_fle_d(rd, cmp1, cmp2);
      break;
    default:
      UNREACHABLE();
  }
}

void TurboAssembler::CompareIsNanF32(Register rd, FPURegister cmp1,
                                     FPURegister cmp2) {
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;

  RV_feq_s(rd, cmp1, cmp1);       // rd <- !isNan(cmp1)
  RV_feq_s(scratch, cmp2, cmp2);  // scratch <- !isNaN(cmp2)
  And(rd, rd, scratch);           // rd <- !isNan(cmp1) && !isNan(cmp2)
  Xor(rd, rd, 1);                 // rd <- isNan(cmp1) || isNan(cmp2)
}

void TurboAssembler::CompareIsNanF64(Register rd, FPURegister cmp1,
                                     FPURegister cmp2) {
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;

  RV_feq_d(rd, cmp1, cmp1);       // rd <- !isNan(cmp1)
  RV_feq_d(scratch, cmp2, cmp2);  // scratch <- !isNaN(cmp2)
  And(rd, rd, scratch);           // rd <- !isNan(cmp1) && !isNan(cmp2)
  Xor(rd, rd, 1);                 // rd <- isNan(cmp1) || isNan(cmp2)
}

void TurboAssembler::BranchTrueShortF(Register rs, Label* target) {
  Branch(target, not_equal, rs, Operand(zero_reg));
}

void TurboAssembler::BranchFalseShortF(Register rs, Label* target) {
  Branch(target, equal, rs, Operand(zero_reg));
}

void TurboAssembler::BranchTrueF(Register rs, Label* target) {
  bool long_branch =
      target->is_bound() ? !is_near(target) : is_trampoline_emitted();
  if (long_branch) {
    Label skip;
    BranchFalseShortF(rs, &skip);
    BranchLong(target);
    bind(&skip);
  } else {
    BranchTrueShortF(rs, target);
  }
}

void TurboAssembler::BranchFalseF(Register rs, Label* target) {
  bool long_branch =
      target->is_bound() ? !is_near(target) : is_trampoline_emitted();
  if (long_branch) {
    Label skip;
    BranchTrueShortF(rs, &skip);
    BranchLong(target);
    bind(&skip);
  } else {
    BranchFalseShortF(rs, target);
  }
}

// move word (src_high) to high-half of dst
void TurboAssembler::FmoveHigh(FPURegister dst, Register src_high) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);

  DCHECK(src_high != t5 && src_high != scratch);

  RV_fmv_x_d(scratch, dst);
  RV_slli(t5, src_high, 32);
  RV_slli(scratch, scratch, 32);
  RV_srli(scratch, scratch, 32);
  RV_or_(scratch, scratch, t5);
  RV_fmv_d_x(dst, scratch);
}

void TurboAssembler::FmoveLow(FPURegister dst, Register src_low) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  UseScratchRegisterScope block_trampoline_pool(this);

  DCHECK(src_low != scratch && src_low != t5);
  RV_fmv_x_d(scratch, dst);
  RV_slli(t5, src_low, 32);
  RV_srli(t5, t5, 32);
  RV_srli(scratch, scratch, 32);
  RV_slli(scratch, scratch, 32);
  RV_or_(scratch, scratch, t5);
  RV_fmv_d_x(dst, scratch);
}

void TurboAssembler::Move(FPURegister dst, Register src_low,
                          Register src_high) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);

  DCHECK(src_high != t5 && src_high != scratch);
  RV_slli(scratch, src_low, 32);
  RV_slli(t5, src_high, 32);
  RV_srli(scratch, scratch, 32);
  RV_or_(scratch, scratch, t5);
  RV_fmv_d_x(dst, scratch);
}

void TurboAssembler::Move(FPURegister dst, uint32_t src) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(static_cast<int32_t>(src)));
  RV_fmv_w_x(dst, scratch);
}

void TurboAssembler::Move(FPURegister dst, uint64_t src) {
  // Handle special values first.
  if (src == bit_cast<uint64_t>(0.0) && has_double_zero_reg_set_) {
    Move_d(dst, kDoubleRegZero);
  } else if (src == bit_cast<uint64_t>(-0.0) && has_double_zero_reg_set_) {
    Neg_d(dst, kDoubleRegZero);
  } else {
    if (dst == kDoubleRegZero) {
      DCHECK(src == bit_cast<uint64_t>(0.0));
      RV_fmv_d_x(dst, zero_reg);
      has_double_zero_reg_set_ = true;
    } else {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, Operand(src));
      RV_fmv_d_x(dst, scratch);
    }
  }
}

void TurboAssembler::Movz(Register rd, Register rs, Register rt) {
  Label done;
  Branch(&done, ne, rt, Operand(zero_reg));
  RV_mv(rd, rs);
  bind(&done);
}

void TurboAssembler::Movn(Register rd, Register rs, Register rt) {
  Label done;
  Branch(&done, eq, rt, Operand(zero_reg));
  RV_mv(rd, rs);
  bind(&done);
}

void TurboAssembler::LoadZeroOnCondition(Register rd, Register rs,
                                         const Operand& rt, Condition cond) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  switch (cond) {
    case cc_always:
      RV_mv(rd, zero_reg);
      break;
    case eq:
      if (rs == zero_reg) {
        if (rt.is_reg()) {
          LoadZeroIfConditionZero(rd, rt.rm());
        } else {
          if (rt.immediate() == 0) {
            RV_mv(rd, zero_reg);
          } else {
            RV_nop();
          }
        }
      } else if (IsZero(rt)) {
        LoadZeroIfConditionZero(rd, rs);
      } else {
        Dsubu(t6, rs, rt);
        LoadZeroIfConditionZero(rd, t6);
      }
      break;
    case ne:
      if (rs == zero_reg) {
        if (rt.is_reg()) {
          LoadZeroIfConditionNotZero(rd, rt.rm());
        } else {
          if (rt.immediate() != 0) {
            RV_mv(rd, zero_reg);
          } else {
            RV_nop();
          }
        }
      } else if (IsZero(rt)) {
        LoadZeroIfConditionNotZero(rd, rs);
      } else {
        Dsubu(t6, rs, rt);
        LoadZeroIfConditionNotZero(rd, t6);
      }
      break;

    // Signed comparison.
    case greater:
      Sgt(t6, rs, rt);
      LoadZeroIfConditionNotZero(rd, t6);
      break;
    case greater_equal:
      Sge(t6, rs, rt);
      LoadZeroIfConditionNotZero(rd, t6);
      // rs >= rt
      break;
    case less:
      Slt(t6, rs, rt);
      LoadZeroIfConditionNotZero(rd, t6);
      // rs < rt
      break;
    case less_equal:
      Sle(t6, rs, rt);
      LoadZeroIfConditionNotZero(rd, t6);
      // rs <= rt
      break;

    // Unsigned comparison.
    case Ugreater:
      Sgtu(t6, rs, rt);
      LoadZeroIfConditionNotZero(rd, t6);
      // rs > rt
      break;

    case Ugreater_equal:
      Sgeu(t6, rs, rt);
      LoadZeroIfConditionNotZero(rd, t6);
      // rs >= rt
      break;
    case Uless:
      Sltu(t6, rs, rt);
      LoadZeroIfConditionNotZero(rd, t6);
      // rs < rt
      break;
    case Uless_equal:
      Sleu(t6, rs, rt);
      LoadZeroIfConditionNotZero(rd, t6);
      // rs <= rt
      break;
    default:
      UNREACHABLE();
  }
}

// dest <- (condition != 0 ? zero : dest), which is eqvuivalent to dest <-
// condition == 0 ? dest : zero
void TurboAssembler::LoadZeroIfConditionNotZero(Register dest,
                                                Register condition) {
  Seleqz(dest, dest, condition);
}

// dest <- (condition == 0 ? 0 : dest), which is equivalent to dest <-
// (condition != 0 ? dest, 0)
void TurboAssembler::LoadZeroIfConditionZero(Register dest,
                                             Register condition) {
  Selnez(dest, dest, condition);
}

void TurboAssembler::Clz(Register rd, Register xx) {
  // 32 bit unsigned in lower word: count number of leading zeros.
  /*
     int n = 32;
     unsigned y;

     y = x >>16; if (y != 0) { n = n -16; x = y; }
     y = x >> 8; if (y != 0) { n = n - 8; x = y; }
     y = x >> 4; if (y != 0) { n = n - 4; x = y; }
     y = x >> 2; if (y != 0) { n = n - 2; x = y; }
     y = x >> 1; if (y != 0) {rd = n - 2; return;}
     rd = n - x;
  */
  Label L0, L1, L2, L3, L4;
  DCHECK(xx != t5 && xx != t6);
  UseScratchRegisterScope temps(this);
  UseScratchRegisterScope block_trampoline_pool(this);
  Register x = rd;
  Register y = t5;
  Register n = t6;
  Move(x, xx);
  li(n, Operand(32));
  RV_srliw(y, x, 16);
  Branch(&L0, eq, y, Operand(zero_reg));
  Move(x, y);
  RV_addiw(n, n, -16);
  bind(&L0);
  RV_srliw(y, x, 8);
  Branch(&L1, eq, y, Operand(zero_reg));
  RV_addiw(n, n, -8);
  Move(x, y);
  bind(&L1);
  RV_srliw(y, x, 4);
  Branch(&L2, eq, y, Operand(zero_reg));
  RV_addiw(n, n, -4);
  Move(x, y);
  bind(&L2);
  RV_srliw(y, x, 2);
  Branch(&L3, eq, y, Operand(zero_reg));
  RV_addiw(n, n, -2);
  Move(x, y);
  bind(&L3);
  RV_srliw(y, x, 1);
  RV_subw(rd, n, x);
  Branch(&L4, eq, y, Operand(zero_reg));
  RV_addiw(rd, n, -2);
  bind(&L4);
}

void TurboAssembler::Dclz(Register rd, Register xx) {
  // 64 bit: count number of leading zeros.
  /*
     int n = 64;
     unsigned y;

     y = x >>32; if (y != 0) { n = n - 32; x = y; }
     y = x >>16; if (y != 0) { n = n - 16; x = y; }
     y = x >> 8; if (y != 0) { n = n - 8; x = y; }
     y = x >> 4; if (y != 0) { n = n - 4; x = y; }
     y = x >> 2; if (y != 0) { n = n - 2; x = y; }
     y = x >> 1; if (y != 0) {rd = n - 2; return;}
     rd = n - x;
  */
  DCHECK(xx != t5 && xx != t6);
  Label L0, L1, L2, L3, L4, L5;
  UseScratchRegisterScope temps(this);
  UseScratchRegisterScope block_trampoline_pool(this);
  Register x = rd;
  Register y = t5;
  Register n = t6;
  Move(x, xx);
  li(n, Operand(64));
  RV_srli(y, x, 32);
  Branch(&L0, eq, y, Operand(zero_reg));
  RV_addiw(n, n, -32);
  Move(x, y);
  bind(&L0);
  RV_srli(y, x, 16);
  Branch(&L1, eq, y, Operand(zero_reg));
  RV_addiw(n, n, -16);
  Move(x, y);
  bind(&L1);
  RV_srli(y, x, 8);
  Branch(&L2, eq, y, Operand(zero_reg));
  RV_addiw(n, n, -8);
  Move(x, y);
  bind(&L2);
  RV_srli(y, x, 4);
  Branch(&L3, eq, y, Operand(zero_reg));
  RV_addiw(n, n, -4);
  Move(x, y);
  bind(&L3);
  RV_srli(y, x, 2);
  Branch(&L4, eq, y, Operand(zero_reg));
  RV_addiw(n, n, -2);
  Move(x, y);
  bind(&L4);
  RV_srli(y, x, 1);
  RV_subw(rd, n, x);
  Branch(&L5, eq, y, Operand(zero_reg));
  RV_addiw(rd, n, -2);
  bind(&L5);
}

void TurboAssembler::Ctz(Register rd, Register rs) {
  // Convert trailing zeroes to trailing ones, and bits to their left
  // to zeroes.
  UseScratchRegisterScope temps(this);
  UseScratchRegisterScope block_trampoline_pool(this);
  Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
  Daddu(scratch, rs, -1);
  Xor(rd, scratch, rs);
  And(rd, rd, scratch);
  // Count number of leading zeroes.
  Clz(rd, rd);
  // Subtract number of leading zeroes from 32 to get number of trailing
  // ones. Remember that the trailing ones were formerly trailing zeroes.
  li(scratch, 32);
  Subu(rd, scratch, rd);
}

void TurboAssembler::Dctz(Register rd, Register rs) {
  // Convert trailing zeroes to trailing ones, and bits to their left
  // to zeroes.
  UseScratchRegisterScope temps(this);
  UseScratchRegisterScope block_trampoline_pool(this);
  Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
  Daddu(scratch, rs, -1);
  Xor(rd, scratch, rs);
  And(rd, rd, scratch);
  // Count number of leading zeroes.
  Dclz(rd, rd);
  // Subtract number of leading zeroes from 64 to get number of trailing
  // ones. Remember that the trailing ones were formerly trailing zeroes.
  li(scratch, 64);
  Dsubu(rd, scratch, rd);
}

void TurboAssembler::Popcnt(Register rd, Register rs) {
  // https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
  //
  // A generalization of the best bit counting method to integers of
  // bit-widths up to 128 (parameterized by type T) is this:
  //
  // v = v - ((v >> 1) & (T)~(T)0/3);                           // temp
  // v = (v & (T)~(T)0/15*3) + ((v >> 2) & (T)~(T)0/15*3);      // temp
  // v = (v + (v >> 4)) & (T)~(T)0/255*15;                      // temp
  // c = (T)(v * ((T)~(T)0/255)) >> (sizeof(T) - 1) * BITS_PER_BYTE; //count
  //
  // There are algorithms which are faster in the cases where very few
  // bits are set but the algorithm here attempts to minimize the total
  // number of instructions executed even when a large number of bits
  // are set.
  // The number of instruction is 20.
  // uint32_t B0 = 0x55555555;     // (T)~(T)0/3
  // uint32_t B1 = 0x33333333;     // (T)~(T)0/15*3
  // uint32_t B2 = 0x0F0F0F0F;     // (T)~(T)0/255*15
  // uint32_t value = 0x01010101;  // (T)~(T)0/255

  DCHECK(rd != t5 && rd != t6 && rs != t5 && rs != t6);
  uint32_t shift = 24;
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.Acquire();
  Register scratch2 = t5;
  Register value = t6;
  li(value, 0x01010101);     // value = 0x01010101;
  li(scratch2, 0x55555555);  // B0 = 0x55555555;
  Srl(scratch, rs, 1);
  And(scratch, scratch, scratch2);
  Subu(scratch, rs, scratch);
  li(scratch2, 0x33333333);  // B1 = 0x33333333;
  RV_slli(rd, scratch2, 4);
  RV_or_(scratch2, scratch2, rd);
  And(rd, scratch, scratch2);
  Srl(scratch, scratch, 2);
  And(scratch, scratch, scratch2);
  Addu(scratch, rd, scratch);
  srl(rd, scratch, 4);
  Addu(rd, rd, scratch);
  li(scratch2, 0xF);
  Mul(scratch2, value, scratch2);  // B2 = 0x0F0F0F0F;
  And(rd, rd, scratch2);
  Mul(rd, rd, value);
  Srl(rd, rd, shift);
}

void TurboAssembler::Dpopcnt(Register rd, Register rs) {
  /*
  uint64_t B0 = 0x5555555555555555l;     // (T)~(T)0/3
  uint64_t B1 = 0x3333333333333333l;     // (T)~(T)0/15*3
  uint64_t B2 = 0x0F0F0F0F0F0F0F0Fl;     // (T)~(T)0/255*15
  uint64_t value = 0x0101010101010101l;  // (T)~(T)0/255
  uint64_t shift = 24;                   // (sizeof(T) - 1) * BITS_PER_BYTE
  */
  DCHECK(rd != t5 && rd != t6 && rs != t5 && rs != t6);
  uint64_t shift = 24;
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.Acquire();
  Register scratch2 = t5;
  Register value = t6;
  li(value, 0x1111111111111111l);  // value = 0x1111111111111111l;
  li(scratch2, 5);
  Dmul(scratch2, value, scratch2);  // B0 = 0x5555555555555555l;
  Dsrl(scratch, rs, 1);
  And(scratch, scratch, scratch2);
  Dsubu(scratch, rs, scratch);
  li(scratch2, 3);
  Dmul(scratch2, value, scratch2);  // B1 = 0x3333333333333333l;
  And(rd, scratch, scratch2);
  Dsrl(scratch, scratch, 2);
  And(scratch, scratch, scratch2);
  Daddu(scratch, rd, scratch);
  Dsrl(rd, scratch, 4);
  Daddu(rd, rd, scratch);
  li(scratch2, 0xF);
  li(value, 0x0101010101010101l);   // value = 0x0101010101010101l;
  Dmul(scratch2, value, scratch2);  // B2 = 0x0F0F0F0F0F0F0F0Fl;
  And(rd, rd, scratch2);
  Dmul(rd, rd, value);
  dsrl32(rd, rd, shift);
}

void TurboAssembler::TryInlineTruncateDoubleToI(Register result,
                                                DoubleRegister double_input,
                                                Label* done) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  // if scratch == 1, exception happens during truncation
  Trunc_w_d(result, double_input, scratch);
  // If we had no exceptions (i.e., scratch==1) we are done.
  Branch(done, eq, scratch, Operand(1));
}

void TurboAssembler::TruncateDoubleToI(Isolate* isolate, Zone* zone,
                                       Register result,
                                       DoubleRegister double_input,
                                       StubCallMode stub_mode) {
  Label done;

  TryInlineTruncateDoubleToI(result, double_input, &done);

  // If we fell through then inline version didn't succeed - call stub instead.
  push(ra);
  Dsubu(sp, sp, Operand(kDoubleSize));  // Put input on stack.
  RV_fsd(double_input, sp, 0);

  if (stub_mode == StubCallMode::kCallWasmRuntimeStub) {
    Call(wasm::WasmCode::kDoubleToI, RelocInfo::WASM_STUB_CALL);
  } else {
    Call(BUILTIN_CODE(isolate, DoubleToI), RelocInfo::CODE_TARGET);
  }
  RV_ld(result, sp, 0);

  Daddu(sp, sp, Operand(kDoubleSize));
  pop(ra);

  bind(&done);
}

// BRANCH_ARGS_CHECK checks that conditional jump arguments are correct.
#define BRANCH_ARGS_CHECK(cond, rs, rt)                                  \
  DCHECK((cond == cc_always && rs == zero_reg && rt.rm() == zero_reg) || \
         (cond != cc_always && (rs != zero_reg || rt.rm() != zero_reg)))

void TurboAssembler::Branch(int32_t offset) {
  DCHECK(is_int21(offset));
  BranchShort(offset);
}

void TurboAssembler::Branch(int32_t offset, Condition cond, Register rs,
                            const Operand& rt) {
  bool is_near = BranchShortCheck(offset, nullptr, cond, rs, rt);
  DCHECK(is_near);
  USE(is_near);
}

void TurboAssembler::Branch(Label* L) {
  if (L->is_bound()) {
    if (is_near_branch(L)) {
      BranchShort(L);
    } else {
      BranchLong(L);
    }
  } else {
    if (is_trampoline_emitted()) {
      BranchLong(L);
    } else {
      BranchShort(L);
    }
  }
}

void TurboAssembler::Branch(Label* L, Condition cond, Register rs,
                            const Operand& rt) {
  if (L->is_bound()) {
    if (!BranchShortCheck(0, L, cond, rs, rt)) {
      if (cond != cc_always) {
        Label skip;
        Condition neg_cond = NegateCondition(cond);
        BranchShort(&skip, neg_cond, rs, rt);
        BranchLong(L);
        bind(&skip);
      } else {
        BranchLong(L);
      }
    }
  } else {
    if (is_trampoline_emitted()) {
      if (cond != cc_always) {
        Label skip;
        Condition neg_cond = NegateCondition(cond);
        BranchShort(&skip, neg_cond, rs, rt);
        BranchLong(L);
        bind(&skip);
      } else {
        BranchLong(L);
      }
    } else {
      BranchShort(L, cond, rs, rt);
    }
  }
}

void TurboAssembler::Branch(Label* L, Condition cond, Register rs,
                            RootIndex index) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  LoadRoot(scratch, index);
  Branch(L, cond, rs, Operand(scratch));
}

void TurboAssembler::BranchShortHelper(int32_t offset, Label* L) {
  DCHECK(L == nullptr || offset == 0);
  offset = GetOffset(offset, L, OffsetSize::kOffset21);
  RV_j(offset);
}

void TurboAssembler::BranchShort(int32_t offset) {
  DCHECK(is_int21(offset));
  BranchShortHelper(offset, nullptr);
}

void TurboAssembler::BranchShort(Label* L) { BranchShortHelper(0, L); }

int32_t TurboAssembler::GetOffset(int32_t offset, Label* L, OffsetSize bits) {
  if (L) {
    offset = branch_offset_helper(L, bits);
  } else {
    DCHECK(is_intn(offset, bits));
  }
  return offset;
}

Register TurboAssembler::GetRtAsRegisterHelper(const Operand& rt,
                                               Register scratch) {
  Register r2 = no_reg;
  if (rt.is_reg()) {
    r2 = rt.rm();
  } else {
    r2 = scratch;
    li(r2, rt);
  }

  return r2;
}

bool TurboAssembler::CalculateOffset(Label* L, int32_t* offset,
                                     OffsetSize bits) {
  if (!is_near(L, bits)) return false;
  *offset = GetOffset(*offset, L, bits);
  return true;
}

bool TurboAssembler::CalculateOffset(Label* L, int32_t* offset, OffsetSize bits,
                                     Register* scratch, const Operand& rt) {
  if (!is_near(L, bits)) return false;
  *scratch = GetRtAsRegisterHelper(rt, *scratch);
  *offset = GetOffset(*offset, L, bits);
  return true;
}

bool TurboAssembler::BranchShortHelper(int32_t offset, Label* L, Condition cond,
                                       Register rs, const Operand& rt) {
  DCHECK(L == nullptr || offset == 0);
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;

  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    switch (cond) {
      case cc_always:
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
        RV_j(offset);
        break;
      case eq:
        // rs == rt
        if (rt.is_reg() && rs == rt.rm()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          RV_j(offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          RV_beq(rs, scratch, offset);
        }
        break;
      case ne:
        // rs != rt
        if (rt.is_reg() && rs == rt.rm()) {
          break;  // No code needs to be emitted
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          RV_bne(rs, scratch, offset);
        }
        break;

      // Signed comparison.
      case greater:
        // rs > rt
        if (rt.is_reg() && rs == rt.rm()) {
          break;  // No code needs to be emitted.
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          RV_bgt(rs, scratch, offset);
        }
        break;
      case greater_equal:
        // rs >= rt
        if (rt.is_reg() && rs == rt.rm()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          RV_j(offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          RV_bge(rs, scratch, offset);
        }
        break;
      case less:
        // rs < rt
        if (rt.is_reg() && rs == rt.rm()) {
          break;  // No code needs to be emitted.
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          RV_blt(rs, scratch, offset);
        }
        break;
      case less_equal:
        // rs <= rt
        if (rt.is_reg() && rs == rt.rm()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          RV_j(offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          RV_ble(rs, scratch, offset);
        }
        break;

      // Unsigned comparison.
      case Ugreater:
        // rs > rt
        if (rt.is_reg() && rs == rt.rm()) {
          break;  // No code needs to be emitted.
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          RV_bgtu(rs, scratch, offset);
        }
        break;
      case Ugreater_equal:
        // rs >= rt
        if (rt.is_reg() && rs == rt.rm()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          RV_j(offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          RV_bgeu(rs, scratch, offset);
        }
        break;
      case Uless:
        // rs < rt
        if (rt.is_reg() && rs == rt.rm()) {
          break;  // No code needs to be emitted.
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          RV_bltu(rs, scratch, offset);
        }
        break;
      case Uless_equal:
        // rs <= rt
        if (rt.is_reg() && rs == rt.rm()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          RV_j(offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          RV_bleu(rs, scratch, offset);
        }
        break;
      default:
        UNREACHABLE();
    }
  }

  CheckTrampolinePoolQuick(1);
  return true;
}

bool TurboAssembler::BranchShortCheck(int32_t offset, Label* L, Condition cond,
                                      Register rs, const Operand& rt) {
  BRANCH_ARGS_CHECK(cond, rs, rt);

  if (!L) {
    DCHECK(is_int13(offset));
    return BranchShortHelper(offset, nullptr, cond, rs, rt);
  } else {
    DCHECK_EQ(offset, 0);
    return BranchShortHelper(0, L, cond, rs, rt);
  }
  return false;
}

void TurboAssembler::BranchShort(int32_t offset, Condition cond, Register rs,
                                 const Operand& rt) {
  BranchShortCheck(offset, nullptr, cond, rs, rt);
}

void TurboAssembler::BranchShort(Label* L, Condition cond, Register rs,
                                 const Operand& rt) {
  BranchShortCheck(0, L, cond, rs, rt);
}

void TurboAssembler::BranchAndLink(int32_t offset) {
  BranchAndLinkShort(offset);
}

void TurboAssembler::BranchAndLink(int32_t offset, Condition cond, Register rs,
                                   const Operand& rt) {
  bool is_near = BranchAndLinkShortCheck(offset, nullptr, cond, rs, rt);
  DCHECK(is_near);
  USE(is_near);
}

void TurboAssembler::BranchAndLink(Label* L) {
  if (L->is_bound()) {
    if (is_near_branch(L)) {
      BranchAndLinkShort(L);
    } else {
      BranchAndLinkLong(L);
    }
  } else {
    if (is_trampoline_emitted()) {
      BranchAndLinkLong(L);
    } else {
      BranchAndLinkShort(L);
    }
  }
}

void TurboAssembler::BranchAndLink(Label* L, Condition cond, Register rs,
                                   const Operand& rt) {
  if (L->is_bound()) {
    if (!BranchAndLinkShortCheck(0, L, cond, rs, rt)) {
      Label skip;
      Condition neg_cond = NegateCondition(cond);
      BranchShort(&skip, neg_cond, rs, rt);
      BranchAndLinkLong(L);
      bind(&skip);
    }
  } else {
    if (is_trampoline_emitted()) {
      Label skip;
      Condition neg_cond = NegateCondition(cond);
      BranchShort(&skip, neg_cond, rs, rt);
      BranchAndLinkLong(L);
      bind(&skip);
    } else {
      BranchAndLinkShortCheck(0, L, cond, rs, rt);
    }
  }
}

void TurboAssembler::BranchAndLinkShortHelper(int32_t offset, Label* L) {
  DCHECK(L == nullptr || offset == 0);
  offset = GetOffset(offset, L, OffsetSize::kOffset21);
  RV_jal(offset);
}

void TurboAssembler::BranchAndLinkShort(int32_t offset) {
  DCHECK(is_int21(offset));
  BranchAndLinkShortHelper(offset, nullptr);
}

void TurboAssembler::BranchAndLinkShort(Label* L) {
  BranchAndLinkShortHelper(0, L);
}

// Pre r6 we need to use a bgezal or bltzal, but they can't be used directly
// with the slt instructions. We could use sub or add instead but we would miss
// overflow cases, so we keep slt and add an intermediate third instruction.
bool TurboAssembler::BranchAndLinkShortHelper(int32_t offset, Label* L,
                                              Condition cond, Register rs,
                                              const Operand& rt) {
  DCHECK(L == nullptr || offset == 0);
  if (!is_near(L, OffsetSize::kOffset21)) return false;

  Register scratch = t5;
  BlockTrampolinePoolScope block_trampoline_pool(this);

  if (cond == cc_always) {
    offset = GetOffset(offset, L, OffsetSize::kOffset21);
    RV_jal(offset);
  } else {
    Branch(kInstrSize * 2, NegateCondition(cond), rs,
           Operand(GetRtAsRegisterHelper(rt, scratch)));
    offset = GetOffset(offset, L, OffsetSize::kOffset21);
    RV_jal(offset);
  }

  return true;
}

bool TurboAssembler::BranchAndLinkShortCheck(int32_t offset, Label* L,
                                             Condition cond, Register rs,
                                             const Operand& rt) {
  BRANCH_ARGS_CHECK(cond, rs, rt);

  if (!L) {
    DCHECK(is_int21(offset));
    return BranchAndLinkShortHelper(offset, nullptr, cond, rs, rt);
  } else {
    DCHECK_EQ(offset, 0);
    return BranchAndLinkShortHelper(0, L, cond, rs, rt);
  }
  return false;
}

void TurboAssembler::LoadFromConstantsTable(Register destination,
                                            int constant_index) {
  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kBuiltinsConstantsTable));
  LoadRoot(destination, RootIndex::kBuiltinsConstantsTable);
  Ld(destination,
     FieldMemOperand(destination,
                     FixedArray::kHeaderSize + constant_index * kPointerSize));
}

void TurboAssembler::LoadRootRelative(Register destination, int32_t offset) {
  Ld(destination, MemOperand(kRootRegister, offset));
}

void TurboAssembler::LoadRootRegisterOffset(Register destination,
                                            intptr_t offset) {
  if (offset == 0) {
    Move(destination, kRootRegister);
  } else {
    Daddu(destination, kRootRegister, Operand(offset));
  }
}

void TurboAssembler::Jump(Register target, Condition cond, Register rs,
                          const Operand& rt) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (cond == cc_always) {
    RV_jr(target);
  } else {
    BRANCH_ARGS_CHECK(cond, rs, rt);
    Branch(kInstrSize * 2, NegateCondition(cond), rs, rt);
    RV_jr(target);
  }
}

void TurboAssembler::Jump(intptr_t target, RelocInfo::Mode rmode,
                          Condition cond, Register rs, const Operand& rt) {
  Label skip;
  if (cond != cc_always) {
    Branch(&skip, NegateCondition(cond), rs, rt);
  }
  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    li(t6, Operand(target, rmode));
    Jump(t6, al, zero_reg, Operand(zero_reg));
    bind(&skip);
  }
}

void TurboAssembler::Jump(Address target, RelocInfo::Mode rmode, Condition cond,
                          Register rs, const Operand& rt) {
  DCHECK(!RelocInfo::IsCodeTarget(rmode));
  Jump(static_cast<intptr_t>(target), rmode, cond, rs, rt);
}

void TurboAssembler::Jump(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond, Register rs, const Operand& rt) {
  DCHECK(RelocInfo::IsCodeTarget(rmode));

  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (root_array_available_ && options().isolate_independent_code) {
    IndirectLoadConstant(t6, code);
    Daddu(t6, t6, Operand(Code::kHeaderSize - kHeapObjectTag));
    Jump(t6, cond, rs, rt);
    return;
  } else if (options().inline_offheap_trampolines) {
    int builtin_index = Builtins::kNoBuiltinId;
    if (isolate()->builtins()->IsBuiltinHandle(code, &builtin_index) &&
        Builtins::IsIsolateIndependent(builtin_index)) {
      // Inline the trampoline.
      RecordCommentForOffHeapTrampoline(builtin_index);
      CHECK_NE(builtin_index, Builtins::kNoBuiltinId);
      EmbeddedData d = EmbeddedData::FromBlob();
      Address entry = d.InstructionStartOfBuiltin(builtin_index);
      li(t6, Operand(entry, RelocInfo::OFF_HEAP_TARGET));
      Jump(t6, cond, rs, rt);
      return;
    }
  }

  Jump(static_cast<intptr_t>(code.address()), rmode, cond, rs, rt);
}

void TurboAssembler::Jump(const ExternalReference& reference) {
  li(t6, reference);
  Jump(t6);
}

// Note: To call gcc-compiled C code on riscv, you must call through t6.
void TurboAssembler::Call(Register target, Condition cond, Register rs,
                          const Operand& rt) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (cond == cc_always) {
    RV_jalr(ra, target, 0);
  } else {
    BRANCH_ARGS_CHECK(cond, rs, rt);
    Branch(kInstrSize * 2, NegateCondition(cond), rs, rt);
    RV_jalr(ra, target, 0);
  }
}

void MacroAssembler::JumpIfIsInRange(Register value, unsigned lower_limit,
                                     unsigned higher_limit,
                                     Label* on_in_range) {
  if (lower_limit != 0) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Dsubu(scratch, value, Operand(lower_limit));
    Branch(on_in_range, Uless_equal, scratch,
           Operand(higher_limit - lower_limit));
  } else {
    Branch(on_in_range, Uless_equal, value,
           Operand(higher_limit - lower_limit));
  }
}

void TurboAssembler::Call(Address target, RelocInfo::Mode rmode, Condition cond,
                          Register rs, const Operand& rt) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  li(t6, Operand(static_cast<int64_t>(target), rmode), ADDRESS_LOAD);
  Call(t6, cond, rs, rt);
}

void TurboAssembler::Call(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond, Register rs, const Operand& rt) {
  BlockTrampolinePoolScope block_trampoline_pool(this);

  if (root_array_available_ && options().isolate_independent_code) {
    IndirectLoadConstant(t6, code);
    Daddu(t6, t6, Operand(Code::kHeaderSize - kHeapObjectTag));
    Call(t6, cond, rs, rt);
    return;
  } else if (options().inline_offheap_trampolines) {
    int builtin_index = Builtins::kNoBuiltinId;
    if (isolate()->builtins()->IsBuiltinHandle(code, &builtin_index) &&
        Builtins::IsIsolateIndependent(builtin_index)) {
      // Inline the trampoline.
      RecordCommentForOffHeapTrampoline(builtin_index);
      CHECK_NE(builtin_index, Builtins::kNoBuiltinId);
      EmbeddedData d = EmbeddedData::FromBlob();
      Address entry = d.InstructionStartOfBuiltin(builtin_index);
      li(t6, Operand(entry, RelocInfo::OFF_HEAP_TARGET));
      Call(t6, cond, rs, rt);
      return;
    }
  }

  DCHECK(RelocInfo::IsCodeTarget(rmode));
  DCHECK(code->IsExecutable());
  Call(code.address(), rmode, cond, rs, rt);
}

void TurboAssembler::LoadEntryFromBuiltinIndex(Register builtin_index) {
  STATIC_ASSERT(kSystemPointerSize == 8);
  STATIC_ASSERT(kSmiTagSize == 1);
  STATIC_ASSERT(kSmiTag == 0);

  // The builtin_index register contains the builtin index as a Smi.
  SmiUntag(builtin_index, builtin_index);
  Dlsa(builtin_index, kRootRegister, builtin_index, kSystemPointerSizeLog2);
  Ld(builtin_index,
     MemOperand(builtin_index, IsolateData::builtin_entry_table_offset()));
}

void TurboAssembler::CallBuiltinByIndex(Register builtin_index) {
  LoadEntryFromBuiltinIndex(builtin_index);
  Call(builtin_index);
}

void TurboAssembler::PatchAndJump(Address target) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  RV_auipc(scratch, 0);  // Load PC into scratch
  Ld(t6, MemOperand(scratch, kInstrSize * 4));
  RV_jr(t6);
  RV_nop();  // For alignment
  DCHECK_EQ(reinterpret_cast<uint64_t>(pc_) % 8, 0);
  *reinterpret_cast<uint64_t*>(pc_) = target;  // pc_ should be align.
  pc_ += sizeof(uint64_t);
}

void TurboAssembler::StoreReturnAddressAndCall(Register target) {
  // This generates the final instruction sequence for calls to C functions
  // once an exit frame has been constructed.
  //
  // Note that this assumes the caller code (i.e. the Code object currently
  // being generated) is immovable or that the callee function cannot trigger
  // GC, since the callee function will return to it.

  // Compute the return address in lr to return to after the jump below. The
  // pc is already at '+ 8' from the current instruction; but return is after
  // three instructions, so add another 4 to pc to get the return address.

  Assembler::BlockTrampolinePoolScope block_trampoline_pool(this);
  static constexpr int kNumInstructionsToJump = 5;
  Label find_ra;
  // Adjust the value in ra to point to the correct return location, one
  // instruction past the real call into C code (the jalr(t6)), and push it.
  // This is the return address of the exit frame.
  RV_auipc(ra, 0);  // Set ra the current PC
  bind(&find_ra);
  RV_addi(ra, ra,
          (kNumInstructionsToJump + 1) *
              kInstrSize);  // Set ra to insn after the call

  // This spot was reserved in EnterExitFrame.
  Sd(ra, MemOperand(sp));
  RV_addi(sp, sp, -kCArgsSlotsSize);
  // Stack is still aligned.

  // Call the C routine.
  RV_mv(t6, target);  // Function pointer to t6 to conform to ABI for PIC.
  RV_jalr(t6);
  // Make sure the stored 'ra' points to this position.
  DCHECK_EQ(kNumInstructionsToJump, InstructionsGeneratedSince(&find_ra));
}

void TurboAssembler::Ret(Condition cond, Register rs, const Operand& rt) {
  Jump(ra, cond, rs, rt);
}

void TurboAssembler::BranchLong(Label* L) {
  if (!L->is_bound() || is_near(L)) {
    BranchShortHelper(0, L);
  } else {
    // Generate position independent long branch.
    BlockTrampolinePoolScope block_trampoline_pool(this);
    int64_t imm64;
    imm64 = branch_long_offset(L);
    DCHECK(is_int32(imm64));
    RV_auipc(t5, 0);  // Read PC into t5.
    RV_li(t6, imm64);
    RV_add(t6, t5, t6);
    RV_jr(t6);
  }
}

void TurboAssembler::BranchAndLinkLong(Label* L) {
  if (!L->is_bound() || is_near(L)) {
    BranchAndLinkShortHelper(0, L);
  } else {
    // Generate position independent long branch and link.
    BlockTrampolinePoolScope block_trampoline_pool(this);
    int64_t imm64;
    imm64 = branch_long_offset(L);
    DCHECK(is_int32(imm64));
    RV_auipc(ra, 0);  // Read PC into ra register.
    RV_li(t5, imm64);
    RV_add(t5, ra, t5);
    RV_jalr(t5);
  }
}

void TurboAssembler::DropAndRet(int drop) {
  DCHECK(is_int12(drop * kPointerSize));
  RV_addi(sp, sp, drop * kPointerSize);
  Ret();
}

void TurboAssembler::DropAndRet(int drop, Condition cond, Register r1,
                                const Operand& r2) {
  // Both Drop and Ret need to be conditional.
  Label skip;
  if (cond != cc_always) {
    Branch(&skip, NegateCondition(cond), r1, r2);
  }

  Drop(drop);
  Ret();

  if (cond != cc_always) {
    bind(&skip);
  }
}

void TurboAssembler::Drop(int count, Condition cond, Register reg,
                          const Operand& op) {
  if (count <= 0) {
    return;
  }

  Label skip;

  if (cond != al) {
    Branch(&skip, NegateCondition(cond), reg, op);
  }

  Daddu(sp, sp, Operand(count * kPointerSize));

  if (cond != al) {
    bind(&skip);
  }
}

void MacroAssembler::Swap(Register reg1, Register reg2, Register scratch) {
  if (scratch == no_reg) {
    Xor(reg1, reg1, Operand(reg2));
    Xor(reg2, reg2, Operand(reg1));
    Xor(reg1, reg1, Operand(reg2));
  } else {
    RV_mv(scratch, reg1);
    RV_mv(reg1, reg2);
    RV_mv(reg2, scratch);
  }
}

void TurboAssembler::Call(Label* target) { BranchAndLink(target); }

void TurboAssembler::LoadAddress(Register dst, Label* target) {
  uint64_t address = jump_address(target);
  li(dst, address);
}

void TurboAssembler::Push(Smi smi) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(smi));
  push(scratch);
}

void TurboAssembler::Push(Handle<HeapObject> handle) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(handle));
  push(scratch);
}

void MacroAssembler::MaybeDropFrames() {
  // Check whether we need to drop frames to restart a function on the stack.
  li(a1, ExternalReference::debug_restart_fp_address(isolate()));
  Ld(a1, MemOperand(a1));
  Jump(BUILTIN_CODE(isolate(), FrameDropperTrampoline), RelocInfo::CODE_TARGET,
       ne, a1, Operand(zero_reg));
}

// ---------------------------------------------------------------------------
// Exception handling.

void MacroAssembler::PushStackHandler() {
  // Adjust this code if not the case.
  STATIC_ASSERT(StackHandlerConstants::kSize == 2 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kNextOffset == 0 * kPointerSize);

  Push(Smi::zero());  // Padding.

  // Link the current handler as the next handler.
  li(t2,
     ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate()));
  Ld(t1, MemOperand(t2));
  push(t1);

  // Set this new handler as the current one.
  Sd(sp, MemOperand(t2));
}

void MacroAssembler::PopStackHandler() {
  STATIC_ASSERT(StackHandlerConstants::kNextOffset == 0);
  pop(a1);
  Daddu(sp, sp,
        Operand(
            static_cast<int64_t>(StackHandlerConstants::kSize - kPointerSize)));
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch,
     ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate()));
  Sd(a1, MemOperand(scratch));
}

void TurboAssembler::FPUCanonicalizeNaN(const DoubleRegister dst,
                                        const DoubleRegister src) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Label NotNaN;

  RV_fmv_d(dst, src);
  RV_feq_d(scratch, src, src);
  RV_bne(scratch, zero_reg, &NotNaN);
  RV_li(scratch, 0x7ff8000000000000ULL); // This is the canonical NaN
  RV_fmv_d_x(dst, scratch);
  bind(&NotNaN);
}

void TurboAssembler::MovFromFloatResult(const DoubleRegister dst) {
  Move(dst, fa0);  // Reg fa0 is FP return value.
}

void TurboAssembler::MovFromFloatParameter(const DoubleRegister dst) {
  Move(dst, fa0);  // Reg fa0 is FP first argument value.
}

void TurboAssembler::MovToFloatParameter(DoubleRegister src) { Move(fa0, src); }

void TurboAssembler::MovToFloatResult(DoubleRegister src) { Move(fa0, src); }

void TurboAssembler::MovToFloatParameters(DoubleRegister src1,
                                          DoubleRegister src2) {
  const DoubleRegister fparg2 = fa1;
  if (src2 == fa0) {
    DCHECK(src1 != fparg2);
    Move(fparg2, src2);
    Move(fa0, src1);
  } else {
    Move(fa0, src1);
    Move(fparg2, src2);
  }
}

// -----------------------------------------------------------------------------
// JavaScript invokes.

void TurboAssembler::PrepareForTailCall(Register callee_args_count,
                                        Register caller_args_count,
                                        Register scratch0, Register scratch1) {
  // Calculate the end of destination area where we will put the arguments
  // after we drop current frame. We add kPointerSize to count the receiver
  // argument which is not included into formal parameters count.
  Register dst_reg = scratch0;
  Dlsa(dst_reg, fp, caller_args_count, kPointerSizeLog2);
  Daddu(dst_reg, dst_reg,
        Operand(StandardFrameConstants::kCallerSPOffset + kPointerSize));

  Register src_reg = caller_args_count;
  // Calculate the end of source area. +kPointerSize is for the receiver.
  Dlsa(src_reg, sp, callee_args_count, kPointerSizeLog2);
  Daddu(src_reg, src_reg, Operand(kPointerSize));

  if (FLAG_debug_code) {
    Check(Uless, AbortReason::kStackAccessBelowStackPointer, src_reg,
          Operand(dst_reg));
  }

  // Restore caller's frame pointer and return address now as they will be
  // overwritten by the copying loop.
  Ld(ra, MemOperand(fp, StandardFrameConstants::kCallerPCOffset));
  Ld(fp, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));

  // Now copy callee arguments to the caller frame going backwards to avoid
  // callee arguments corruption (source and destination areas could overlap).

  // Both src_reg and dst_reg are pointing to the word after the one to copy,
  // so they must be pre-decremented in the loop.
  Register tmp_reg = scratch1;
  Label loop, entry;
  Branch(&entry);
  bind(&loop);
  Dsubu(src_reg, src_reg, Operand(kPointerSize));
  Dsubu(dst_reg, dst_reg, Operand(kPointerSize));
  Ld(tmp_reg, MemOperand(src_reg));
  Sd(tmp_reg, MemOperand(dst_reg));
  bind(&entry);
  Branch(&loop, ne, sp, Operand(src_reg));

  // Leave current frame.
  RV_mv(sp, dst_reg);
}

void MacroAssembler::InvokePrologue(Register expected_parameter_count,
                                    Register actual_parameter_count,
                                    Label* done, InvokeFlag flag) {
  Label regular_invoke;

  // Check whether the expected and actual arguments count match. The
  // registers are set up according to contract with
  // ArgumentsAdaptorTrampoline:
  //  a0: actual arguments count
  //  a1: function (passed through to callee)
  //  a2: expected arguments count

  // The code below is made a lot easier because the calling code already sets
  // up actual and expected registers according to the contract.

  DCHECK_EQ(actual_parameter_count, a0);
  DCHECK_EQ(expected_parameter_count, a2);

  Branch(&regular_invoke, eq, expected_parameter_count,
         Operand(actual_parameter_count));

  Handle<Code> adaptor = BUILTIN_CODE(isolate(), ArgumentsAdaptorTrampoline);
  if (flag == CALL_FUNCTION) {
    Call(adaptor);
    Branch(done);
  } else {
    Jump(adaptor, RelocInfo::CODE_TARGET);
  }

  bind(&regular_invoke);
}

void MacroAssembler::CheckDebugHook(Register fun, Register new_target,
                                    Register expected_parameter_count,
                                    Register actual_parameter_count) {
  Label skip_hook;

  li(t0, ExternalReference::debug_hook_on_function_call_address(isolate()));
  Lb(t0, MemOperand(t0));
  Branch(&skip_hook, eq, t0, Operand(zero_reg));

  {
    // Load receiver to pass it later to DebugOnFunctionCall hook.
    Dlsa(t0, sp, actual_parameter_count, kPointerSizeLog2);
    Ld(t0, MemOperand(t0));
    FrameScope frame(this,
                     has_frame() ? StackFrame::NONE : StackFrame::INTERNAL);
    SmiTag(expected_parameter_count);
    Push(expected_parameter_count);

    SmiTag(actual_parameter_count);
    Push(actual_parameter_count);

    if (new_target.is_valid()) {
      Push(new_target);
    }
    Push(fun);
    Push(fun);
    Push(t0);
    CallRuntime(Runtime::kDebugOnFunctionCall);
    Pop(fun);
    if (new_target.is_valid()) {
      Pop(new_target);
    }

    Pop(actual_parameter_count);
    SmiUntag(actual_parameter_count);

    Pop(expected_parameter_count);
    SmiUntag(expected_parameter_count);
  }
  bind(&skip_hook);
}

void MacroAssembler::InvokeFunctionCode(Register function, Register new_target,
                                        Register expected_parameter_count,
                                        Register actual_parameter_count,
                                        InvokeFlag flag) {
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(flag == CALL_FUNCTION, has_frame());
  DCHECK_EQ(function, a1);
  DCHECK_IMPLIES(new_target.is_valid(), new_target == a3);

  // On function call, call into the debugger if necessary.
  CheckDebugHook(function, new_target, expected_parameter_count,
                 actual_parameter_count);

  // Clear the new.target register if not given.
  if (!new_target.is_valid()) {
    LoadRoot(a3, RootIndex::kUndefinedValue);
  }

  Label done;
  InvokePrologue(expected_parameter_count, actual_parameter_count, &done, flag);
  // We call indirectly through the code field in the function to
  // allow recompilation to take effect without changing any of the
  // call sites.
  Register code = kJavaScriptCallCodeStartRegister;
  Ld(code, FieldMemOperand(function, JSFunction::kCodeOffset));
  if (flag == CALL_FUNCTION) {
    Daddu(code, code, Operand(Code::kHeaderSize - kHeapObjectTag));
    Call(code);
  } else {
    DCHECK(flag == JUMP_FUNCTION);
    Daddu(code, code, Operand(Code::kHeaderSize - kHeapObjectTag));
    Jump(code);
  }

  // Continue here if InvokePrologue does handle the invocation due to
  // mismatched parameter counts.
  bind(&done);
}

void MacroAssembler::InvokeFunctionWithNewTarget(
    Register function, Register new_target, Register actual_parameter_count,
    InvokeFlag flag) {
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(flag == CALL_FUNCTION, has_frame());

  // Contract with called JS functions requires that function is passed in a1.
  DCHECK_EQ(function, a1);
  Register expected_parameter_count = a2;
  Register temp_reg = t0;
  Ld(temp_reg, FieldMemOperand(a1, JSFunction::kSharedFunctionInfoOffset));
  Ld(cp, FieldMemOperand(a1, JSFunction::kContextOffset));
  // The argument count is stored as uint16_t
  Lhu(expected_parameter_count,
      FieldMemOperand(temp_reg,
                      SharedFunctionInfo::kFormalParameterCountOffset));

  InvokeFunctionCode(a1, new_target, expected_parameter_count,
                     actual_parameter_count, flag);
}

void MacroAssembler::InvokeFunction(Register function,
                                    Register expected_parameter_count,
                                    Register actual_parameter_count,
                                    InvokeFlag flag) {
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(flag == CALL_FUNCTION, has_frame());

  // Contract with called JS functions requires that function is passed in a1.
  DCHECK_EQ(function, a1);

  // Get the function and setup the context.
  Ld(cp, FieldMemOperand(a1, JSFunction::kContextOffset));

  InvokeFunctionCode(a1, no_reg, expected_parameter_count,
                     actual_parameter_count, flag);
}

// ---------------------------------------------------------------------------
// Support functions.

void MacroAssembler::GetObjectType(Register object, Register map,
                                   Register type_reg) {
  LoadMap(map, object);
  Lhu(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
}

// -----------------------------------------------------------------------------
// Runtime calls.

void TurboAssembler::DaddOverflow(Register dst, Register left,
                                  const Operand& right, Register overflow) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = t5;
  if (!right.is_reg()) {
    li(t3, Operand(right));
    right_reg = t3;
  } else {
    right_reg = right.rm();
  }
  DCHECK(left != scratch && right_reg != scratch && dst != scratch &&
         overflow != scratch);
  DCHECK(overflow != left && overflow != right_reg);
  if (dst == left || dst == right_reg) {
    RV_add(scratch, left, right_reg);
    RV_xor_(overflow, scratch, left);
    RV_xor_(t3, scratch, right_reg);
    RV_and_(overflow, overflow, t3);
    RV_mv(dst, scratch);
  } else {
    RV_add(dst, left, right_reg);
    RV_xor_(overflow, dst, left);
    RV_xor_(t3, dst, right_reg);
    RV_and_(overflow, overflow, t3);
  }
}

void TurboAssembler::DsubOverflow(Register dst, Register left,
                                  const Operand& right, Register overflow) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = t5;
  if (!right.is_reg()) {
    li(t3, Operand(right));
    right_reg = t3;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch && right_reg != scratch && dst != scratch &&
         overflow != scratch);
  DCHECK(overflow != left && overflow != right_reg);

  if (dst == left || dst == right_reg) {
    RV_sub(scratch, left, right_reg);
    RV_xor_(overflow, left, scratch);
    RV_xor_(t3, left, right_reg);
    RV_and_(overflow, overflow, t3);
    RV_mv(dst, scratch);
  } else {
    RV_sub(dst, left, right_reg);
    RV_xor_(overflow, left, dst);
    RV_xor_(t3, left, right_reg);
    RV_and_(overflow, overflow, t3);
  }
}

void TurboAssembler::MulOverflow(Register dst, Register left,
                                 const Operand& right, Register overflow) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = t5;
  if (!right.is_reg()) {
    li(t3, Operand(right));
    right_reg = t3;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch && right_reg != scratch && dst != scratch &&
         overflow != scratch);
  DCHECK(overflow != left && overflow != right_reg);

  if (dst == left || dst == right_reg) {
    Mul(scratch, left, right_reg);
    Mulh(overflow, left, right_reg);
    RV_mv(dst, scratch);
  } else {
    Mul(dst, left, right_reg);
    Mulh(overflow, left, right_reg);
  }

  RV_srai(scratch, dst, 32);
  RV_xor_(overflow, overflow, scratch);
}

void MacroAssembler::CallRuntime(const Runtime::Function* f, int num_arguments,
                                 SaveFPRegsMode save_doubles) {
  // All parameters are on the stack. a0 has the return value after call.

  // If the expected number of arguments of the runtime function is
  // constant, we check that the actual number of arguments match the
  // expectation.
  CHECK(f->nargs < 0 || f->nargs == num_arguments);

  // TODO(1236192): Most runtime routines don't need the number of
  // arguments passed in because it is constant. At some point we
  // should remove this need and make the runtime routine entry code
  // smarter.
  PrepareCEntryArgs(num_arguments);
  PrepareCEntryFunction(ExternalReference::Create(f));
  Handle<Code> code =
      CodeFactory::CEntry(isolate(), f->result_size, save_doubles);
  Call(code, RelocInfo::CODE_TARGET);
}

void MacroAssembler::TailCallRuntime(Runtime::FunctionId fid) {
  const Runtime::Function* function = Runtime::FunctionForId(fid);
  DCHECK_EQ(1, function->result_size);
  if (function->nargs >= 0) {
    PrepareCEntryArgs(function->nargs);
  }
  JumpToExternalReference(ExternalReference::Create(fid));
}

void MacroAssembler::JumpToExternalReference(const ExternalReference& builtin,
                                             bool builtin_exit_frame) {
  PrepareCEntryFunction(builtin);
  Handle<Code> code = CodeFactory::CEntry(isolate(), 1, kDontSaveFPRegs,
                                          kArgvOnStack, builtin_exit_frame);
  Jump(code, RelocInfo::CODE_TARGET, al, zero_reg, Operand(zero_reg));
}

void MacroAssembler::JumpToInstructionStream(Address entry) {
  li(kOffHeapTrampolineRegister, Operand(entry, RelocInfo::OFF_HEAP_TARGET));
  Jump(kOffHeapTrampolineRegister);
}

void MacroAssembler::LoadWeakValue(Register out, Register in,
                                   Label* target_if_cleared) {
  Branch(target_if_cleared, eq, in, Operand(kClearedWeakHeapObjectLower32));

  And(out, in, Operand(~kWeakHeapObjectMask));
}

void MacroAssembler::IncrementCounter(StatsCounter* counter, int value,
                                      Register scratch1, Register scratch2) {
  DCHECK_GT(value, 0);
  if (FLAG_native_code_counters && counter->Enabled()) {
    // This operation has to be exactly 32-bit wide in case the external
    // reference table redirects the counter to a uint32_t
    // dummy_stats_counter_ field.
    li(scratch2, ExternalReference::Create(counter));
    Lw(scratch1, MemOperand(scratch2));
    Addu(scratch1, scratch1, Operand(value));
    Sw(scratch1, MemOperand(scratch2));
  }
}

void MacroAssembler::DecrementCounter(StatsCounter* counter, int value,
                                      Register scratch1, Register scratch2) {
  DCHECK_GT(value, 0);
  if (FLAG_native_code_counters && counter->Enabled()) {
    // This operation has to be exactly 32-bit wide in case the external
    // reference table redirects the counter to a uint32_t
    // dummy_stats_counter_ field.
    li(scratch2, ExternalReference::Create(counter));
    Lw(scratch1, MemOperand(scratch2));
    Subu(scratch1, scratch1, Operand(value));
    Sw(scratch1, MemOperand(scratch2));
  }
}

// -----------------------------------------------------------------------------
// Debugging.

void TurboAssembler::Trap() { stop(); }

void TurboAssembler::Assert(Condition cc, AbortReason reason, Register rs,
                            Operand rt) {
  if (emit_debug_code()) Check(cc, reason, rs, rt);
}

void TurboAssembler::Check(Condition cc, AbortReason reason, Register rs,
                           Operand rt) {
  Label L;
  Branch(&L, cc, rs, rt);
  Abort(reason);
  // Will not return here.
  bind(&L);
}

void TurboAssembler::Abort(AbortReason reason) {
  Label abort_start;
  bind(&abort_start);
#ifdef DEBUG
  const char* msg = GetAbortReason(reason);
  RecordComment("Abort message: ");
  RecordComment(msg);
#endif

  // Avoid emitting call to builtin if requested.
  if (trap_on_abort()) {
    RV_ebreak();
    return;
  }

  if (should_abort_hard()) {
    // We don't care if we constructed a frame. Just pretend we did.
    FrameScope assume_frame(this, StackFrame::NONE);
    PrepareCallCFunction(0, a0);
    li(a0, Operand(static_cast<int>(reason)));
    CallCFunction(ExternalReference::abort_with_reason(), 1);
    return;
  }

  Move(a0, Smi::FromInt(static_cast<int>(reason)));

  // Disable stub call restrictions to always allow calls to abort.
  if (!has_frame()) {
    // We don't actually want to generate a pile of code for this, so just
    // claim there is a stack frame, without generating one.
    FrameScope scope(this, StackFrame::NONE);
    Call(BUILTIN_CODE(isolate(), Abort), RelocInfo::CODE_TARGET);
  } else {
    Call(BUILTIN_CODE(isolate(), Abort), RelocInfo::CODE_TARGET);
  }
  // Will not return here.
  if (is_trampoline_pool_blocked()) {
    // If the calling code cares about the exact number of
    // instructions generated, we insert padding here to keep the size
    // of the Abort macro constant.
    // Currently in debug mode with debug_code enabled the number of
    // generated instructions is 10, so we use this as a maximum value.
    static const int kExpectedAbortInstructions = 10;
    int abort_instructions = InstructionsGeneratedSince(&abort_start);
    DCHECK_LE(abort_instructions, kExpectedAbortInstructions);
    while (abort_instructions++ < kExpectedAbortInstructions) {
      RV_nop();
    }
  }
}

void MacroAssembler::LoadMap(Register destination, Register object) {
  Ld(destination, FieldMemOperand(object, HeapObject::kMapOffset));
}

void MacroAssembler::LoadNativeContextSlot(int index, Register dst) {
  LoadMap(dst, cp);
  Ld(dst,
     FieldMemOperand(dst, Map::kConstructorOrBackPointerOrNativeContextOffset));
  Ld(dst, MemOperand(dst, Context::SlotOffset(index)));
}

void TurboAssembler::StubPrologue(StackFrame::Type type) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(StackFrame::TypeToMarker(type)));
  PushCommonFrame(scratch);
}

void TurboAssembler::Prologue() { PushStandardFrame(a1); }

void TurboAssembler::EnterFrame(StackFrame::Type type) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  int stack_offset = -3 * kPointerSize;
  const int fp_offset = 1 * kPointerSize;
  RV_addi(sp, sp, stack_offset);
  stack_offset = -stack_offset - kPointerSize;
  Sd(ra, MemOperand(sp, stack_offset));
  stack_offset -= kPointerSize;
  Sd(fp, MemOperand(sp, stack_offset));
  stack_offset -= kPointerSize;
  li(t6, Operand(StackFrame::TypeToMarker(type)));
  Sd(t6, MemOperand(sp, stack_offset));
  // Adjust FP to point to saved FP.
  DCHECK_EQ(stack_offset, 0);
  Daddu(fp, sp, Operand(fp_offset));
}

void TurboAssembler::LeaveFrame(StackFrame::Type type) {
  RV_addi(sp, fp, 2 * kPointerSize);
  Ld(ra, MemOperand(fp, 1 * kPointerSize));
  Ld(fp, MemOperand(fp, 0 * kPointerSize));
}

void MacroAssembler::EnterExitFrame(bool save_doubles, int stack_space,
                                    StackFrame::Type frame_type) {
  DCHECK(frame_type == StackFrame::EXIT ||
         frame_type == StackFrame::BUILTIN_EXIT);

  // Set up the frame structure on the stack.
  STATIC_ASSERT(2 * kPointerSize == ExitFrameConstants::kCallerSPDisplacement);
  STATIC_ASSERT(1 * kPointerSize == ExitFrameConstants::kCallerPCOffset);
  STATIC_ASSERT(0 * kPointerSize == ExitFrameConstants::kCallerFPOffset);

  // This is how the stack will look:
  // fp + 2 (==kCallerSPDisplacement) - old stack's end
  // [fp + 1 (==kCallerPCOffset)] - saved old ra
  // [fp + 0 (==kCallerFPOffset)] - saved old fp
  // [fp - 1 StackFrame::EXIT Smi
  // [fp - 2 (==kSPOffset)] - sp of the called function
  // fp - (2 + stack_space + alignment) == sp == [fp - kSPOffset] - top of the
  //   new stack (will contain saved ra)

  // Save registers and reserve room for saved entry sp.
  RV_addi(sp, sp,
          -2 * kPointerSize - ExitFrameConstants::kFixedFrameSizeFromFp);
  Sd(ra, MemOperand(sp, 3 * kPointerSize));
  Sd(fp, MemOperand(sp, 2 * kPointerSize));
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    li(scratch, Operand(StackFrame::TypeToMarker(frame_type)));
    Sd(scratch, MemOperand(sp, 1 * kPointerSize));
  }
  // Set up new frame pointer.
  RV_addi(fp, sp, ExitFrameConstants::kFixedFrameSizeFromFp);

  if (emit_debug_code()) {
    Sd(zero_reg, MemOperand(fp, ExitFrameConstants::kSPOffset));
  }

  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    // Save the frame pointer and the context in top.
    li(t5, ExternalReference::Create(IsolateAddressId::kCEntryFPAddress,
                                     isolate()));
    Sd(fp, MemOperand(t5));
    li(t5,
       ExternalReference::Create(IsolateAddressId::kContextAddress, isolate()));
    Sd(cp, MemOperand(t5));
  }

  const int frame_alignment = MacroAssembler::ActivationFrameAlignment();
  if (save_doubles) {
    // The stack is already aligned to 0 modulo 8 for stores with sdc1.
    int kNumOfSavedRegisters = FPURegister::kNumRegisters;
    int space = kNumOfSavedRegisters * kDoubleSize;
    Dsubu(sp, sp, Operand(space));
    for (int i = 0; i < kNumOfSavedRegisters; i++) {
      FPURegister reg = FPURegister::from_code(i);
      Sdc1(reg, MemOperand(sp, i * kDoubleSize));
    }
  }

  // Reserve place for the return address, stack space and an optional slot
  // (used by DirectCEntry to hold the return value if a struct is
  // returned) and align the frame preparing for calling the runtime function.
  DCHECK_GE(stack_space, 0);
  Dsubu(sp, sp, Operand((stack_space + 2) * kPointerSize));
  if (frame_alignment > 0) {
    DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
    And(sp, sp, Operand(-frame_alignment));  // Align stack.
  }

  // Set the exit frame sp value to point just before the return address
  // location.
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  RV_addi(scratch, sp, kPointerSize);
  Sd(scratch, MemOperand(fp, ExitFrameConstants::kSPOffset));
}

void MacroAssembler::LeaveExitFrame(bool save_doubles, Register argument_count,
                                    bool do_return,
                                    bool argument_count_is_length) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  // Optionally restore all double registers.
  if (save_doubles) {
    // Remember: we only need to restore every 2nd double FPU value.
    int kNumOfSavedRegisters = FPURegister::kNumRegisters / 2;
    Dsubu(t5, fp,
          Operand(ExitFrameConstants::kFixedFrameSizeFromFp +
                  kNumOfSavedRegisters * kDoubleSize));
    for (int i = 0; i < kNumOfSavedRegisters; i++) {
      FPURegister reg = FPURegister::from_code(2 * i);
      Ldc1(reg, MemOperand(t5, i * kDoubleSize));
    }
  }

  // Clear top frame.
  li(t5,
     ExternalReference::Create(IsolateAddressId::kCEntryFPAddress, isolate()));
  Sd(zero_reg, MemOperand(t5));

  // Restore current context from top and clear it in debug mode.
  li(t5,
     ExternalReference::Create(IsolateAddressId::kContextAddress, isolate()));
  Ld(cp, MemOperand(t5));

#ifdef DEBUG
  li(t5,
     ExternalReference::Create(IsolateAddressId::kContextAddress, isolate()));
  Sd(a3, MemOperand(t5));
#endif

  // Pop the arguments, restore registers, and return.
  RV_mv(sp, fp);  // Respect ABI stack constraint.
  Ld(fp, MemOperand(sp, ExitFrameConstants::kCallerFPOffset));
  Ld(ra, MemOperand(sp, ExitFrameConstants::kCallerPCOffset));

  if (argument_count.is_valid()) {
    if (argument_count_is_length) {
      RV_add(sp, sp, argument_count);
    } else {
      Dlsa(sp, sp, argument_count, kPointerSizeLog2, t5);
    }
  }

  RV_addi(sp, sp, 2 * kPointerSize);

  if (do_return) {
    Ret();
  }
}

int TurboAssembler::ActivationFrameAlignment() {
#if V8_HOST_ARCH_RISCV
  // Running on the real platform. Use the alignment as mandated by the local
  // environment.
  // Note: This will break if we ever start generating snapshots on one RISC-V
  // platform for another RISC-V platform with a different alignment.
  return base::OS::ActivationFrameAlignment();
#else   // V8_HOST_ARCH_RISCV
  // If we are using the simulator then we should always align to the expected
  // alignment. As the simulator is used to generate snapshots we do not know
  // if the target platform will need alignment, so this is controlled from a
  // flag.
  return FLAG_sim_stack_alignment;
#endif  // V8_HOST_ARCH_RISCV
}

void MacroAssembler::AssertStackIsAligned() {
  if (emit_debug_code()) {
    const int frame_alignment = ActivationFrameAlignment();
    const int frame_alignment_mask = frame_alignment - 1;

    if (frame_alignment > kPointerSize) {
      Label alignment_as_expected;
      DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
      {
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();
        RV_andi(scratch, sp, frame_alignment_mask);
        Branch(&alignment_as_expected, eq, scratch, Operand(zero_reg));
      }
      // Don't use Check here, as it will call Runtime_Abort re-entering here.
      RV_ebreak();
      bind(&alignment_as_expected);
    }
  }
}

void TurboAssembler::SmiUntag(Register dst, const MemOperand& src) {
  if (SmiValuesAre32Bits()) {
    Lw(dst, MemOperand(src.rm(), SmiWordOffset(src.offset())));
  } else {
    DCHECK(SmiValuesAre31Bits());
    Lw(dst, src);
    SmiUntag(dst);
  }
}

void TurboAssembler::JumpIfSmi(Register value, Label* smi_label,
                               Register scratch) {
  DCHECK_EQ(0, kSmiTag);
  RV_andi(scratch, value, kSmiTagMask);
  Branch(smi_label, eq, scratch, Operand(zero_reg));
}

void MacroAssembler::JumpIfNotSmi(Register value, Label* not_smi_label,
                                  Register scratch) {
  DCHECK_EQ(0, kSmiTag);
  RV_andi(scratch, value, kSmiTagMask);
  Branch(not_smi_label, ne, scratch, Operand(zero_reg));
}

void MacroAssembler::AssertNotSmi(Register object) {
  if (emit_debug_code()) {
    STATIC_ASSERT(kSmiTag == 0);
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    RV_andi(scratch, object, kSmiTagMask);
    Check(ne, AbortReason::kOperandIsASmi, scratch, Operand(zero_reg));
  }
}

void MacroAssembler::AssertSmi(Register object) {
  if (emit_debug_code()) {
    STATIC_ASSERT(kSmiTag == 0);
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    RV_andi(scratch, object, kSmiTagMask);
    Check(eq, AbortReason::kOperandIsASmi, scratch, Operand(zero_reg));
  }
}

void MacroAssembler::AssertConstructor(Register object) {
  if (emit_debug_code()) {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    STATIC_ASSERT(kSmiTag == 0);
    SmiTst(object, t5);
    Check(ne, AbortReason::kOperandIsASmiAndNotAConstructor, t5,
          Operand(zero_reg));

    LoadMap(t5, object);
    Lbu(t5, FieldMemOperand(t5, Map::kBitFieldOffset));
    And(t5, t5, Operand(Map::Bits1::IsConstructorBit::kMask));
    Check(ne, AbortReason::kOperandIsNotAConstructor, t5, Operand(zero_reg));
  }
}

void MacroAssembler::AssertFunction(Register object) {
  if (emit_debug_code()) {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    STATIC_ASSERT(kSmiTag == 0);
    SmiTst(object, t5);
    Check(ne, AbortReason::kOperandIsASmiAndNotAFunction, t5,
          Operand(zero_reg));
    GetObjectType(object, t5, t5);
    Check(eq, AbortReason::kOperandIsNotAFunction, t5,
          Operand(JS_FUNCTION_TYPE));
  }
}

void MacroAssembler::AssertBoundFunction(Register object) {
  if (emit_debug_code()) {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    STATIC_ASSERT(kSmiTag == 0);
    SmiTst(object, t5);
    Check(ne, AbortReason::kOperandIsASmiAndNotABoundFunction, t5,
          Operand(zero_reg));
    GetObjectType(object, t5, t5);
    Check(eq, AbortReason::kOperandIsNotABoundFunction, t5,
          Operand(JS_BOUND_FUNCTION_TYPE));
  }
}

void MacroAssembler::AssertGeneratorObject(Register object) {
  if (!emit_debug_code()) return;
  BlockTrampolinePoolScope block_trampoline_pool(this);
  STATIC_ASSERT(kSmiTag == 0);
  SmiTst(object, t5);
  Check(ne, AbortReason::kOperandIsASmiAndNotAGeneratorObject, t5,
        Operand(zero_reg));

  GetObjectType(object, t5, t5);

  Label done;

  // Check if JSGeneratorObject
  Branch(&done, eq, t5, Operand(JS_GENERATOR_OBJECT_TYPE));

  // Check if JSAsyncFunctionObject (See MacroAssembler::CompareInstanceType)
  Branch(&done, eq, t5, Operand(JS_ASYNC_FUNCTION_OBJECT_TYPE));

  // Check if JSAsyncGeneratorObject
  Branch(&done, eq, t5, Operand(JS_ASYNC_GENERATOR_OBJECT_TYPE));

  Abort(AbortReason::kOperandIsNotAGeneratorObject);

  bind(&done);
}

void MacroAssembler::AssertUndefinedOrAllocationSite(Register object,
                                                     Register scratch) {
  if (emit_debug_code()) {
    Label done_checking;
    AssertNotSmi(object);
    LoadRoot(scratch, RootIndex::kUndefinedValue);
    Branch(&done_checking, eq, object, Operand(scratch));
    GetObjectType(object, scratch, scratch);
    Assert(eq, AbortReason::kExpectedUndefinedOrCell, scratch,
           Operand(ALLOCATION_SITE_TYPE));
    bind(&done_checking);
  }
}

template <typename F_TYPE>
void TurboAssembler::FloatMinMaxHelper(FPURegister dst, FPURegister src1,
                                       FPURegister src2, MaxMinKind kind) {
  DCHECK((std::is_same<F_TYPE, float>::value) ||
         (std::is_same<F_TYPE, double>::value));

  if (src1 == src2) {
    if (std::is_same<float, F_TYPE>::value) {
      Move_s(dst, src1);
    } else {
      Move_d(dst, src1);
    }
    return;
  }

  Label done, nan;

  // For RISCV, fmin_s returns the other non-NaN operand as result if only one
  // operand is NaN; but for JS, if any operand is NaN, result is Nan. The
  // following handles the discrepency between handling of NaN between ISA and
  // JS semantics
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  if (std::is_same<float, F_TYPE>::value) {
    CompareIsNanF32(scratch, src1, src2);
  } else {
    CompareIsNanF64(scratch, src1, src2);
  }
  BranchTrueF(scratch, &nan);

  if (kind == MaxMinKind::kMax) {
    if (std::is_same<float, F_TYPE>::value) {
      RV_fmax_s(dst, src1, src2);
    } else {
      RV_fmax_d(dst, src1, src2);
    }
  } else {
    if (std::is_same<float, F_TYPE>::value) {
      RV_fmin_s(dst, src1, src2);
    } else {
      RV_fmin_d(dst, src1, src2);
    }
  }
  RV_j(&done);

  bind(&nan);
  // if any operand is NaN, return NaN (fadd returns NaN if any operand is NaN)
  if (std::is_same<float, F_TYPE>::value) {
    RV_fadd_s(dst, src1, src2);
  } else {
    RV_fadd_d(dst, src1, src2);
  }

  bind(&done);
}

void TurboAssembler::Float32Max(FPURegister dst, FPURegister src1,
                                FPURegister src2) {
  FloatMinMaxHelper<float>(dst, src1, src2, MaxMinKind::kMax);
}

void TurboAssembler::Float32Min(FPURegister dst, FPURegister src1,
                                FPURegister src2) {
  FloatMinMaxHelper<float>(dst, src1, src2, MaxMinKind::kMin);
}

void TurboAssembler::Float64Max(FPURegister dst, FPURegister src1,
                                FPURegister src2) {
  FloatMinMaxHelper<double>(dst, src1, src2, MaxMinKind::kMax);
}

void TurboAssembler::Float64Min(FPURegister dst, FPURegister src1,
                                FPURegister src2) {
  FloatMinMaxHelper<double>(dst, src1, src2, MaxMinKind::kMin);
}

static const int kRegisterPassedArguments = 8;

int TurboAssembler::CalculateStackPassedDWords(int num_gp_arguments,
                                               int num_fp_arguments) {
  int stack_passed_dwords = 0;

  // Up to eight integer arguments are passed in registers a0..a7 and
  // up to eight floating point arguments are passed in registers fa0..fa7
  if (num_gp_arguments > kRegisterPassedArguments) {
    stack_passed_dwords += num_gp_arguments - kRegisterPassedArguments;
  }
  if (num_fp_arguments > kRegisterPassedArguments) {
    stack_passed_dwords += num_fp_arguments - kRegisterPassedArguments;
  }
  stack_passed_dwords += kCArgSlotCount;
  return stack_passed_dwords;
}

void TurboAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          int num_double_arguments,
                                          Register scratch) {
  int frame_alignment = ActivationFrameAlignment();

  // Up to eight simple arguments in a0..a7, fa0..fa7.
  // Remaining arguments are pushed on the stack (arg slot calculation handled
  // by CalculateStackPassedDWords()).
  int stack_passed_arguments =
      CalculateStackPassedDWords(num_reg_arguments, num_double_arguments);
  if (frame_alignment > kPointerSize) {
    // Make stack end at alignment and make room for stack arguments and the
    // original value of sp.
    RV_mv(scratch, sp);
    Dsubu(sp, sp, Operand((stack_passed_arguments + 1) * kPointerSize));
    DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
    And(sp, sp, Operand(-frame_alignment));
    Sd(scratch, MemOperand(sp, stack_passed_arguments * kPointerSize));
  } else {
    Dsubu(sp, sp, Operand(stack_passed_arguments * kPointerSize));
  }
}

void TurboAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          Register scratch) {
  PrepareCallCFunction(num_reg_arguments, 0, scratch);
}

void TurboAssembler::CallCFunction(ExternalReference function,
                                   int num_reg_arguments,
                                   int num_double_arguments) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  li(t6, function);
  CallCFunctionHelper(t6, num_reg_arguments, num_double_arguments);
}

void TurboAssembler::CallCFunction(Register function, int num_reg_arguments,
                                   int num_double_arguments) {
  CallCFunctionHelper(function, num_reg_arguments, num_double_arguments);
}

void TurboAssembler::CallCFunction(ExternalReference function,
                                   int num_arguments) {
  CallCFunction(function, num_arguments, 0);
}

void TurboAssembler::CallCFunction(Register function, int num_arguments) {
  CallCFunction(function, num_arguments, 0);
}

void TurboAssembler::CallCFunctionHelper(Register function,
                                         int num_reg_arguments,
                                         int num_double_arguments) {
  DCHECK_LE(num_reg_arguments + num_double_arguments, kMaxCParameters);
  DCHECK(has_frame());
  // Make sure that the stack is aligned before calling a C function unless
  // running in the simulator. The simulator has its own alignment check which
  // provides more information.
  // The argument stots are presumed to have been set up by
  // PrepareCallCFunction.
  // FIXME(RISC-V): The MIPS ABI requires a C function must be called via t9,
  //                does RISC-V have a similar requirement? We currently use t6

#if V8_HOST_ARCH_RISCV
  if (emit_debug_code()) {
    int frame_alignment = base::OS::ActivationFrameAlignment();
    int frame_alignment_mask = frame_alignment - 1;
    if (frame_alignment > kPointerSize) {
      DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
      Label alignment_as_expected;
      {
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();
        And(scratch, sp, Operand(frame_alignment_mask));
        Branch(&alignment_as_expected, eq, scratch, Operand(zero_reg));
      }
      // Don't use Check here, as it will call Runtime_Abort possibly
      // re-entering here.
      RV_ebreak();
      bind(&alignment_as_expected);
    }
  }
#endif  // V8_HOST_ARCH_RISCV

  // Just call directly. The function called cannot cause a GC, or
  // allow preemption, so the return address in the link register
  // stays correct.
  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    if (function != t6) {
      RV_mv(t6, function);
      function = t6;
    }

    // Save the frame pointer and PC so that the stack layout remains
    // iterable, even without an ExitFrame which normally exists between JS
    // and C frames.
    if (isolate() != nullptr) {
      // 't' registers are caller-saved so this is safe as a scratch register.
      Register scratch1 = t1;
      Register scratch2 = t2;
      DCHECK(!AreAliased(scratch1, scratch2, function));

      Label get_pc;
      RV_mv(scratch1, ra);
      Call(&get_pc);

      bind(&get_pc);
      RV_mv(scratch2, ra);
      RV_mv(ra, scratch1);

      li(scratch1, ExternalReference::fast_c_call_caller_pc_address(isolate()));
      Sd(scratch2, MemOperand(scratch1));
      li(scratch1, ExternalReference::fast_c_call_caller_fp_address(isolate()));
      Sd(fp, MemOperand(scratch1));
    }

    Call(function);

    if (isolate() != nullptr) {
      // We don't unset the PC; the FP is the source of truth.
      Register scratch = t1;
      li(scratch, ExternalReference::fast_c_call_caller_fp_address(isolate()));
      Sd(zero_reg, MemOperand(scratch));
    }
  }

  int stack_passed_arguments =
      CalculateStackPassedDWords(num_reg_arguments, num_double_arguments);

  if (base::OS::ActivationFrameAlignment() > kPointerSize) {
    Ld(sp, MemOperand(sp, stack_passed_arguments * kPointerSize));
  } else {
    Daddu(sp, sp, Operand(stack_passed_arguments * kPointerSize));
  }
}

#undef BRANCH_ARGS_CHECK

void TurboAssembler::CheckPageFlag(Register object, Register scratch, int mask,
                                   Condition cc, Label* condition_met) {
  And(scratch, object, Operand(~kPageAlignmentMask));
  Ld(scratch, MemOperand(scratch, MemoryChunk::kFlagsOffset));
  And(scratch, scratch, Operand(mask));
  Branch(condition_met, cc, scratch, Operand(zero_reg));
}

Register GetRegisterThatIsNotOneOf(Register reg1, Register reg2, Register reg3,
                                   Register reg4, Register reg5,
                                   Register reg6) {
  RegList regs = 0;
  if (reg1.is_valid()) regs |= reg1.bit();
  if (reg2.is_valid()) regs |= reg2.bit();
  if (reg3.is_valid()) regs |= reg3.bit();
  if (reg4.is_valid()) regs |= reg4.bit();
  if (reg5.is_valid()) regs |= reg5.bit();
  if (reg6.is_valid()) regs |= reg6.bit();

  const RegisterConfiguration* config = RegisterConfiguration::Default();
  for (int i = 0; i < config->num_allocatable_general_registers(); ++i) {
    int code = config->GetAllocatableGeneralCode(i);
    Register candidate = Register::from_code(code);
    if (regs & candidate.bit()) continue;
    return candidate;
  }
  UNREACHABLE();
}

void TurboAssembler::ComputeCodeStartAddress(Register dst) {
  // This push on ra and the pop below together ensure that we restore the
  // register ra, which is needed while computing the code start address.
  push(ra);

  RV_auipc(ra, 0);
  RV_addi(ra, ra, kInstrSize * 2);  // ra = address of li
  int pc = pc_offset();
  li(dst, Operand(pc));
  Dsubu(dst, ra, dst);

  pop(ra);  // Restore ra
}

void TurboAssembler::ResetSpeculationPoisonRegister() {
  li(kSpeculationPoisonRegister, -1);
}

void TurboAssembler::CallForDeoptimization(Address target, int deopt_id) {
  NoRootArrayScope no_root_array(this);

  // Save the deopt id in kRootRegister (we don't need the roots array from
  // now on).
  DCHECK_LE(deopt_id, 0xFFFF);
  li(kRootRegister, deopt_id);
  Call(target, RelocInfo::RUNTIME_ENTRY);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_RISCV
