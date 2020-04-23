// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2012 the V8 project authors. All rights reserved.

#include "src/codegen/riscv/assembler-riscv.h"

#if V8_TARGET_ARCH_RISCV

#include "src/base/cpu.h"
#include "src/codegen/riscv/assembler-riscv-inl.h"
#include "src/codegen/safepoint-table.h"
#include "src/codegen/string-constants.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/objects/heap-number-inl.h"

#include "src/diagnostics/disasm.h"
#include "src/diagnostics/disassembler.h"

namespace v8 {
namespace internal {
// Get the CPU features enabled by the build. For cross compilation the
// preprocessor symbols CAN_USE_FPU_INSTRUCTIONS
// can be defined to enable FPU instructions when building the
// snapshot.
static unsigned CpuFeaturesImpliedByCompiler() {
  unsigned answer = 0;
#ifdef CAN_USE_FPU_INSTRUCTIONS
  answer |= 1u << FPU;
#endif  // def CAN_USE_FPU_INSTRUCTIONS

  // If the compiler is allowed to use FPU then we can use FPU too in our code
  // generation even when generating snapshots.  This won't work for cross
  // compilation.
#if defined(__mips__) && defined(__mips_hard_float) && __mips_hard_float != 0
  answer |= 1u << FPU;
#endif

  return answer;
}

void CpuFeatures::ProbeImpl(bool cross_compile) {
  supported_ |= CpuFeaturesImpliedByCompiler();

  // Only use statically determined features for cross compile (snapshot).
  if (cross_compile) return;

    // If the compiler is allowed to use fpu then we can use fpu too in our
    // code generation.
#ifndef __mips__
  // For the simulator build, use FPU.
  supported_ |= 1u << FPU;
#if defined(_MIPS_ARCH_MIPS64R6) && defined(_MIPS_MSA)
  supported_ |= 1u << MIPS_SIMD;
#endif
#else
  // Probe for additional features at runtime.
  base::CPU cpu;
  if (cpu.has_fpu()) supported_ |= 1u << FPU;
#if defined(_MIPS_ARCH_MIPS64R6)
#if defined(_MIPS_MSA)
  supported_ |= 1u << MIPS_SIMD;
#else
  if (cpu.has_msa()) supported_ |= 1u << MIPS_SIMD;
#endif
#endif
#endif
}

void CpuFeatures::PrintTarget() {}
void CpuFeatures::PrintFeatures() {}

int ToNumber(Register reg) {
  DCHECK(reg.is_valid());
  const int kNumbers[] = {
      0,   // zero_reg
      1,   // ra
      2,   // sp
      3,   // gp
      4,   // tp
      5,   // t0
      6,   // t1
      7,   // t2
      8,   // s0/fp
      9,   // s1
      10,  // a0
      11,  // a1
      12,  // a2
      13,  // a3
      14,  // a4
      15,  // a5
      16,  // a6
      17,  // a7
      18,  // s2
      19,  // s3
      20,  // s4
      21,  // s5
      22,  // s6
      23,  // s7
      24,  // s8
      25,  // s9
      26,  // s10
      27,  // s11
      28,  // t3
      29,  // t4
      30,  // t5
      31,  // t6
  };
  return kNumbers[reg.code()];
}

Register ToRegister(int num) {
  DCHECK(num >= 0 && num < kNumRegisters);
  const Register kRegisters[] = {
      zero_reg, ra, sp, gp, tp, t0, t1, t2, fp, s1, a0,  a1,  a2, a3, a4, a5,
      a6,       a7, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, t3, t4, t5, t6};
  return kRegisters[num];
}

// -----------------------------------------------------------------------------
// Implementation of RelocInfo.

const int RelocInfo::kApplyMask =
    RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE) |
    RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE_ENCODED);

bool RelocInfo::IsCodedSpecially() {
  // The deserializer needs to know whether a pointer is specially coded.  Being
  // specially coded on MIPS means that it is a lui/ori instruction, and that is
  // always the case inside code objects.
  return true;
}

bool RelocInfo::IsInConstantPool() { return false; }

uint32_t RelocInfo::wasm_call_tag() const {
  DCHECK(rmode_ == WASM_CALL || rmode_ == WASM_STUB_CALL);
  return static_cast<uint32_t>(
      Assembler::target_address_at(pc_, constant_pool_));
}

// -----------------------------------------------------------------------------
// Implementation of Operand and MemOperand.
// See assembler-mips-inl.h for inlined constructors.

Operand::Operand(Handle<HeapObject> handle)
    : rm_(no_reg), rmode_(RelocInfo::FULL_EMBEDDED_OBJECT) {
  value_.immediate = static_cast<intptr_t>(handle.address());
}

Operand Operand::EmbeddedNumber(double value) {
  int32_t smi;
  if (DoubleToSmiInteger(value, &smi)) return Operand(Smi::FromInt(smi));
  Operand result(0, RelocInfo::FULL_EMBEDDED_OBJECT);
  result.is_heap_object_request_ = true;
  result.value_.heap_object_request = HeapObjectRequest(value);
  return result;
}

Operand Operand::EmbeddedStringConstant(const StringConstantBase* str) {
  Operand result(0, RelocInfo::FULL_EMBEDDED_OBJECT);
  result.is_heap_object_request_ = true;
  result.value_.heap_object_request = HeapObjectRequest(str);
  return result;
}

MemOperand::MemOperand(Register rm, int32_t offset) : Operand(rm) {
  offset_ = offset;
}

MemOperand::MemOperand(Register rm, int32_t unit, int32_t multiplier,
                       OffsetAddend offset_addend)
    : Operand(rm) {
  offset_ = unit * multiplier + offset_addend;
}

void Assembler::AllocateAndInstallRequestedHeapObjects(Isolate* isolate) {
  DCHECK_IMPLIES(isolate == nullptr, heap_object_requests_.empty());
  for (auto& request : heap_object_requests_) {
    Handle<HeapObject> object;
    switch (request.kind()) {
      case HeapObjectRequest::kHeapNumber:
        object = isolate->factory()->NewHeapNumber<AllocationType::kOld>(
            request.heap_number());
        break;
      case HeapObjectRequest::kStringConstant:
        const StringConstantBase* str = request.string();
        CHECK_NOT_NULL(str);
        object = str->AllocateStringConstant(isolate);
        break;
    }
    Address pc = reinterpret_cast<Address>(buffer_start_) + request.offset();
    set_target_value_at(pc, reinterpret_cast<uint64_t>(object.location()));
  }
}

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.

// daddiu(sp, sp, 8) aka Pop() operation or part of Pop(r)
// operations as post-increment of sp.
const Instr kPopInstruction = DADDIU | (sp.code() << kRsShift) |
                              (sp.code() << kRtShift) |
                              (kPointerSize & kImm16Mask);  // NOLINT
// daddiu(sp, sp, -8) part of Push(r) operation as pre-decrement of sp.
const Instr kPushInstruction = DADDIU | (sp.code() << kRsShift) |
                               (sp.code() << kRtShift) |
                               (-kPointerSize & kImm16Mask);  // NOLINT
// Sd(r, MemOperand(sp, 0))
const Instr kPushRegPattern =
    SD | (sp.code() << kRsShift) | (0 & kImm16Mask);  // NOLINT
//  Ld(r, MemOperand(sp, 0))
const Instr kPopRegPattern =
    LD | (sp.code() << kRsShift) | (0 & kImm16Mask);  // NOLINT

const Instr kLwRegFpOffsetPattern =
    LW | (fp.code() << kRsShift) | (0 & kImm16Mask);  // NOLINT

const Instr kSwRegFpOffsetPattern =
    SW | (fp.code() << kRsShift) | (0 & kImm16Mask);  // NOLINT

const Instr kLwRegFpNegOffsetPattern =
    LW | (fp.code() << kRsShift) | (kNegOffset & kImm16Mask);  // NOLINT

const Instr kSwRegFpNegOffsetPattern =
    SW | (fp.code() << kRsShift) | (kNegOffset & kImm16Mask);  // NOLINT
// A mask for the Rt register for push, pop, lw, sw instructions.
const Instr kRtMask = kRtFieldMask;
const Instr kLwSwInstrTypeMask = 0xFFE00000;
const Instr kLwSwInstrArgumentMask = ~kLwSwInstrTypeMask;
const Instr kLwSwOffsetMask = kImm16Mask;

Assembler::Assembler(const AssemblerOptions& options,
                     std::unique_ptr<AssemblerBuffer> buffer)
    : AssemblerBase(options, std::move(buffer)),
      scratch_register_list_(t3.bit()) {
  reloc_info_writer.Reposition(buffer_start_ + buffer_->size(), pc_);

  last_trampoline_pool_end_ = 0;
  no_trampoline_pool_before_ = 0;
  trampoline_pool_blocked_nesting_ = 0;
  // We leave space (16 * kTrampolineSlotsSize)
  // for BlockTrampolinePoolScope buffer.
  next_buffer_check_ = FLAG_force_long_branches
                           ? kMaxInt
                           : kMaxBranchOffset - kTrampolineSlotsSize * 16;
  internal_trampoline_exception_ = false;
  last_bound_pos_ = 0;

  trampoline_emitted_ = FLAG_force_long_branches;
  unbound_labels_count_ = 0;
  block_buffer_growth_ = false;
}

void Assembler::GetCode(Isolate* isolate, CodeDesc* desc,
                        SafepointTableBuilder* safepoint_table_builder,
                        int handler_table_offset) {
  EmitForbiddenSlotInstruction();

  int code_comments_size = WriteCodeComments();

  DCHECK(pc_ <= reloc_info_writer.pos());  // No overlap.

  AllocateAndInstallRequestedHeapObjects(isolate);

  // Set up code descriptor.
  // TODO(jgruber): Reconsider how these offsets and sizes are maintained up to
  // this point to make CodeDesc initialization less fiddly.

  static constexpr int kConstantPoolSize = 0;
  const int instruction_size = pc_offset();
  const int code_comments_offset = instruction_size - code_comments_size;
  const int constant_pool_offset = code_comments_offset - kConstantPoolSize;
  const int handler_table_offset2 = (handler_table_offset == kNoHandlerTable)
                                        ? constant_pool_offset
                                        : handler_table_offset;
  const int safepoint_table_offset =
      (safepoint_table_builder == kNoSafepointTable)
          ? handler_table_offset2
          : safepoint_table_builder->GetCodeOffset();
  const int reloc_info_offset =
      static_cast<int>(reloc_info_writer.pos() - buffer_->start());
  CodeDesc::Initialize(desc, this, safepoint_table_offset,
                       handler_table_offset2, constant_pool_offset,
                       code_comments_offset, reloc_info_offset);
}

void Assembler::Align(int m) {
  DCHECK(m >= 4 && base::bits::IsPowerOfTwo(m));
  EmitForbiddenSlotInstruction();
  while ((pc_offset() & (m - 1)) != 0) {
    nop();
  }
}

void Assembler::CodeTargetAlign() {
  // No advantage to aligning branch/call targets to more than
  // single instruction, that I am aware of.
  Align(4);
}

Register Assembler::GetRtReg(Instr instr) {
  return Register::from_code((instr & kRtFieldMask) >> kRtShift);
}

Register Assembler::GetRsReg(Instr instr) {
  return Register::from_code((instr & kRsFieldMask) >> kRsShift);
}

Register Assembler::GetRdReg(Instr instr) {
  return Register::from_code((instr & kRdFieldMask) >> kRdShift);
}

uint32_t Assembler::GetRt(Instr instr) {
  return (instr & kRtFieldMask) >> kRtShift;
}

uint32_t Assembler::GetRtField(Instr instr) { return instr & kRtFieldMask; }

uint32_t Assembler::GetRs(Instr instr) {
  return (instr & kRsFieldMask) >> kRsShift;
}

uint32_t Assembler::GetRsField(Instr instr) { return instr & kRsFieldMask; }

uint32_t Assembler::GetRd(Instr instr) {
  return (instr & kRdFieldMask) >> kRdShift;
}

uint32_t Assembler::GetRdField(Instr instr) { return instr & kRdFieldMask; }

uint32_t Assembler::GetSa(Instr instr) {
  return (instr & kSaFieldMask) >> kSaShift;
}

uint32_t Assembler::GetSaField(Instr instr) { return instr & kSaFieldMask; }

uint32_t Assembler::GetOpcodeField(Instr instr) { return instr & kOpcodeMask; }

uint32_t Assembler::GetFunction(Instr instr) {
  return (instr & kFunctionFieldMask) >> kFunctionShift;
}

uint32_t Assembler::GetFunctionField(Instr instr) {
  return instr & kFunctionFieldMask;
}

uint32_t Assembler::GetImmediate16(Instr instr) { return instr & kImm16Mask; }

uint32_t Assembler::GetLabelConst(Instr instr) { return instr & ~kImm16Mask; }

bool Assembler::IsPop(Instr instr) {
  return (instr & ~kRtMask) == kPopRegPattern;
}

bool Assembler::IsPush(Instr instr) {
  return (instr & ~kRtMask) == kPushRegPattern;
}

bool Assembler::IsSwRegFpOffset(Instr instr) {
  return ((instr & kLwSwInstrTypeMask) == kSwRegFpOffsetPattern);
}

bool Assembler::IsLwRegFpOffset(Instr instr) {
  return ((instr & kLwSwInstrTypeMask) == kLwRegFpOffsetPattern);
}

bool Assembler::IsSwRegFpNegOffset(Instr instr) {
  return ((instr & (kLwSwInstrTypeMask | kNegOffset)) ==
          kSwRegFpNegOffsetPattern);
}

bool Assembler::IsLwRegFpNegOffset(Instr instr) {
  return ((instr & (kLwSwInstrTypeMask | kNegOffset)) ==
          kLwRegFpNegOffsetPattern);
}

// Labels refer to positions in the (to be) generated code.
// There are bound, linked, and unused labels.
//
// Bound labels refer to known positions in the already
// generated code. pos() is the position the label refers to.
//
// Linked labels refer to unknown positions in the code
// to be generated; pos() is the position of the last
// instruction using the label.

// The link chain is terminated by a value in the instruction of 0,
// which is an otherwise illegal value (branch 0 is inf loop).

const int kEndOfChain = 0;

// Determines the end of the Jump chain (a subset of the label link chain).
const int kEndOfJumpChain = 0;

bool Assembler::IsMsaBranch(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  uint32_t rs_field = GetRsField(instr);
  if (opcode == COP1) {
    switch (rs_field) {
      case BZ_V:
      case BZ_B:
      case BZ_H:
      case BZ_W:
      case BZ_D:
      case BNZ_V:
      case BNZ_B:
      case BNZ_H:
      case BNZ_W:
      case BNZ_D:
        return true;
      default:
        return false;
    }
  } else {
    return false;
  }
}

bool Assembler::RV_IsBranch(Instr instr) {
  return (instr & kBaseOpcodeMask) == BRANCH;
}

bool Assembler::IsBranch(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  uint32_t rt_field = GetRtField(instr);
  uint32_t rs_field = GetRsField(instr);
  // Checks if the instruction is a branch.
  bool isBranch =
      opcode == BEQ || opcode == BNE || opcode == BLEZ || opcode == BGTZ ||
      opcode == BEQL || opcode == BNEL || opcode == BLEZL || opcode == BGTZL ||
      (opcode == REGIMM && (rt_field == BLTZ || rt_field == BGEZ ||
                            rt_field == BLTZAL || rt_field == BGEZAL)) ||
      (opcode == COP1 && rs_field == BC1) ||  // Coprocessor branch.
      (opcode == COP1 && rs_field == BC1EQZ) ||
      (opcode == COP1 && rs_field == BC1NEZ) || IsMsaBranch(instr);
  if (!isBranch && kArchVariant == kMips64r6) {
    // All the 3 variants of POP10 (BOVC, BEQC, BEQZALC) and
    // POP30 (BNVC, BNEC, BNEZALC) are branch ops.
    isBranch |= opcode == POP10 || opcode == POP30 || opcode == BC ||
                opcode == BALC ||
                (opcode == POP66 && rs_field != 0) ||  // BEQZC
                (opcode == POP76 && rs_field != 0);    // BNEZC
  }
  return isBranch;
}

bool Assembler::IsBc(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  // Checks if the instruction is a BC or BALC.
  return opcode == BC || opcode == BALC;
}

bool Assembler::IsNal(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  uint32_t rt_field = GetRtField(instr);
  uint32_t rs_field = GetRsField(instr);
  return opcode == REGIMM && rt_field == BLTZAL && rs_field == 0;
}

bool Assembler::IsBzc(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  // Checks if the instruction is BEQZC or BNEZC.
  return (opcode == POP66 && GetRsField(instr) != 0) ||
         (opcode == POP76 && GetRsField(instr) != 0);
}

bool Assembler::IsEmittedConstant(Instr instr) {
  uint32_t label_constant = GetLabelConst(instr);
  return label_constant == 0;  // Emitted label const in reg-exp engine.
}

bool Assembler::IsBeq(Instr instr) { return GetOpcodeField(instr) == BEQ; }

bool Assembler::IsBne(Instr instr) { return GetOpcodeField(instr) == BNE; }

bool Assembler::IsBeqzc(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  return opcode == POP66 && GetRsField(instr) != 0;
}

bool Assembler::IsBnezc(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  return opcode == POP76 && GetRsField(instr) != 0;
}

bool Assembler::IsBeqc(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  uint32_t rs = GetRsField(instr);
  uint32_t rt = GetRtField(instr);
  return opcode == POP10 && rs != 0 && rs < rt;  // && rt != 0
}

bool Assembler::IsBnec(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  uint32_t rs = GetRsField(instr);
  uint32_t rt = GetRtField(instr);
  return opcode == POP30 && rs != 0 && rs < rt;  // && rt != 0
}

bool Assembler::IsMov(Instr instr, Register rd, Register rs) {
  uint32_t opcode = GetOpcodeField(instr);
  uint32_t rd_field = GetRd(instr);
  uint32_t rs_field = GetRs(instr);
  uint32_t rt_field = GetRt(instr);
  uint32_t rd_reg = static_cast<uint32_t>(rd.code());
  uint32_t rs_reg = static_cast<uint32_t>(rs.code());
  uint32_t function_field = GetFunctionField(instr);
  // Checks if the instruction is a OR with zero_reg argument (aka MOV).
  bool res = opcode == SPECIAL && function_field == OR && rd_field == rd_reg &&
             rs_field == rs_reg && rt_field == 0;
  return res;
}

bool Assembler::IsJump(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  uint32_t rt_field = GetRtField(instr);
  uint32_t rd_field = GetRdField(instr);
  uint32_t function_field = GetFunctionField(instr);
  // Checks if the instruction is a jump.
  return opcode == J || opcode == JAL ||
         (opcode == SPECIAL && rt_field == 0 &&
          ((function_field == JALR) ||
           (rd_field == 0 && (function_field == JR))));
}

bool Assembler::RV_IsJump(Instr instr) {
  int Op = instr & kBaseOpcodeMask;
  return Op == RV_JAL || Op == RV_JALR;
}

bool Assembler::RV_IsJal(Instr instr) {
  return (instr & kBaseOpcodeMask) == RV_JAL;
}

bool Assembler::RV_IsJalr(Instr instr) {
  return (instr & kBaseOpcodeMask) == RV_JALR;
}

bool Assembler::IsJ(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  // Checks if the instruction is a jump.
  return opcode == J;
}

bool Assembler::IsJal(Instr instr) { return GetOpcodeField(instr) == JAL; }

bool Assembler::IsJr(Instr instr) {
  return GetOpcodeField(instr) == SPECIAL && GetFunctionField(instr) == JR;
}

bool Assembler::IsJalr(Instr instr) {
  return GetOpcodeField(instr) == SPECIAL && GetFunctionField(instr) == JALR;
}

bool Assembler::IsLui(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  // Checks if the instruction is a load upper immediate.
  return opcode == LUI;
}

bool Assembler::IsOri(Instr instr) {
  uint32_t opcode = GetOpcodeField(instr);
  // Checks if the instruction is a load upper immediate.
  return opcode == ORI;
}

bool Assembler::IsNop(Instr instr, unsigned int type) {
  // See Assembler::nop(type).
  DCHECK_LT(type, 32);
  uint32_t opcode = GetOpcodeField(instr);
  uint32_t function = GetFunctionField(instr);
  uint32_t rt = GetRt(instr);
  uint32_t rd = GetRd(instr);
  uint32_t sa = GetSa(instr);

  // Traditional mips nop == sll(zero_reg, zero_reg, 0)
  // When marking non-zero type, use sll(zero_reg, at, type)
  // to avoid use of mips ssnop and ehb special encodings
  // of the sll instruction.

  Register nop_rt_reg = (type == 0) ? zero_reg : t3;
  bool ret = (opcode == SPECIAL && function == SLL &&
              rd == static_cast<uint32_t>(ToNumber(zero_reg)) &&
              rt == static_cast<uint32_t>(ToNumber(nop_rt_reg)) && sa == type);

  return ret;
}

int32_t Assembler::GetBranchOffset(Instr instr) {
  DCHECK(IsBranch(instr));
  return (static_cast<int16_t>(instr & kImm16Mask)) << 2;
}

bool Assembler::IsLw(Instr instr) {
  return (static_cast<uint32_t>(instr & kOpcodeMask) == LW);
}

int16_t Assembler::GetLwOffset(Instr instr) {
  DCHECK(IsLw(instr));
  return ((instr & kImm16Mask));
}

Instr Assembler::SetLwOffset(Instr instr, int16_t offset) {
  DCHECK(IsLw(instr));

  // We actually create a new lw instruction based on the original one.
  Instr temp_instr = LW | (instr & kRsFieldMask) | (instr & kRtFieldMask) |
                     (offset & kImm16Mask);

  return temp_instr;
}

bool Assembler::IsSw(Instr instr) {
  return (static_cast<uint32_t>(instr & kOpcodeMask) == SW);
}

Instr Assembler::SetSwOffset(Instr instr, int16_t offset) {
  DCHECK(IsSw(instr));
  return ((instr & ~kImm16Mask) | (offset & kImm16Mask));
}

bool Assembler::IsAddImmediate(Instr instr) {
  return ((instr & kOpcodeMask) == ADDIU || (instr & kOpcodeMask) == DADDIU);
}

Instr Assembler::SetAddImmediateOffset(Instr instr, int16_t offset) {
  DCHECK(IsAddImmediate(instr));
  return ((instr & ~kImm16Mask) | (offset & kImm16Mask));
}

bool Assembler::IsAndImmediate(Instr instr) {
  return GetOpcodeField(instr) == ANDI;
}

int Assembler::RV_target_at(int pos, bool is_internal) {
  if (is_internal) {
    int64_t* p = reinterpret_cast<int64_t*>(buffer_start_ + pos);
    int64_t address = *p;
    if (address == kEndOfJumpChain) {
      return kEndOfChain;
    } else {
      int64_t instr_address = reinterpret_cast<int64_t>(p);
      DCHECK(instr_address - address < INT_MAX);
      int delta = static_cast<int>(instr_address - address);
      DCHECK(pos > delta);
      return pos - delta;
    }
  }
  Instr instr = instr_at(pos);
  DEBUG_PRINTF("target_at: %p (%d)\n\t",
               reinterpret_cast<Instr*>(buffer_start_ + pos), pos);
  disassembleInstr(instr);
  if (RV_IsBranch(instr)) {
    int32_t imm13 = RV_BranchOffset(instr);
    if (imm13 == kEndOfJumpChain) {
      // EndOfChain sentinel is returned directly, not relative to pc or pos.
      return kEndOfChain;
    } else {
      return pos + imm13;
    }
  } else if (RV_IsJal(instr)) {
    int32_t imm21 = RV_JumpOffset(instr);
    if (imm21 == kEndOfJumpChain) {
      // EndOfChain sentinel is returned directly, not relative to pc or pos.
      return kEndOfChain;
    } else {
      return pos + imm21;
    }
  } else if (RV_IsJalr(instr)) {
    int32_t imm12 = instr >> 20;
    if (imm12 == kEndOfJumpChain) {
      // EndOfChain sentinel is returned directly, not relative to pc or pos.
      return kEndOfChain;
    } else {
      return pos + imm12;
    }
  } else {
    // Emitted label constant, not part of a branch.
    if (instr == 0) {
      return kEndOfChain;
    } else {
      int32_t imm18 = ((instr & static_cast<int32_t>(kImm16Mask)) << 16) >> 14;
      return (imm18 + pos);
    }
  }
}

int Assembler::target_at(int pos, bool is_internal) {
  DCHECK(0);
  return 0;
}

static inline Instr RV_SetBranchOffset(int32_t pos, int32_t target_pos,
                                       Instr instr) {
  int32_t imm = target_pos - pos;
  DCHECK_EQ(imm & 1, 0);

  instr &= ~kBImm12Mask;
  int32_t imm12 = ((imm & 0x800) >> 4) |   // bit  11
                  ((imm & 0x1e) << 7) |    // bits 4-1
                  ((imm & 0x7e0) << 20) |  // bits 10-5
                  ((imm & 0x1000) << 19);  // bit 12

  return instr | (imm12 & kBImm12Mask);
}

static inline Instr RV_SetJalOffset(int32_t pos, int32_t target_pos,
                                    Instr instr) {
  int32_t imm = target_pos - pos;
  DCHECK_EQ(imm & 1, 0);

  instr &= ~kImm20Mask;
  int32_t imm20 = (imm & 0xff000) |          // bits 19-12
                  ((imm & 0x800) << 9) |     // bit  11
                  ((imm & 0x7fe) << 20) |    // bits 10-1
                  ((imm & 0x100000) << 11);  // bit  20

  return instr | (imm20 & kImm20Mask);
}

void Assembler::RV_target_at_put(int pos, int target_pos, bool is_internal) {
  if (is_internal) {
    uint64_t imm = reinterpret_cast<uint64_t>(buffer_start_) + target_pos;
    *reinterpret_cast<uint64_t*>(buffer_start_ + pos) = imm;
    return;
  }
  DEBUG_PRINTF("target_at_put: %p (%d) to %p (%d)\n",
               reinterpret_cast<Instr*>(buffer_start_ + pos), pos,
               reinterpret_cast<Instr*>(buffer_start_ + target_pos),
               target_pos);
  Instr instr = instr_at(pos);

  if (RV_IsBranch(instr)) {
    instr = RV_SetBranchOffset(pos, target_pos, instr);
    instr_at_put(pos, instr);
  } else if (RV_IsJal(instr)) {
    instr = RV_SetJalOffset(pos, target_pos, instr);
    instr_at_put(pos, instr);
  } else {
    // Emitted label constant, not part of a branch.
    // Make label relative to Code pointer of generated Code object.
    instr_at_put(pos, target_pos + (Code::kHeaderSize - kHeapObjectTag));
  }
  disassembleInstr(instr);
}

void Assembler::target_at_put(int pos, int target_pos, bool is_internal) {
  DCHECK(0);
}

void Assembler::print(const Label* L) {
  if (L->is_unused()) {
    PrintF("unused label\n");
  } else if (L->is_bound()) {
    PrintF("bound label to %d\n", L->pos());
  } else if (L->is_linked()) {
    Label l;
    l.link_to(L->pos());
    PrintF("unbound label");
    while (l.is_linked()) {
      PrintF("@ %d ", l.pos());
      Instr instr = instr_at(l.pos());
      if ((instr & ~kImm16Mask) == 0) {
        PrintF("value\n");
      } else {
        PrintF("%d\n", instr);
      }
      next(&l, is_internal_reference(&l));
    }
  } else {
    PrintF("label in inconsistent state (pos = %d)\n", L->pos_);
  }
}

void Assembler::bind_to(Label* L, int pos) {
  DCHECK(0 <= pos && pos <= pc_offset());  // Must have valid binding position.
  DEBUG_PRINTF("binding %d to label %p\n", pos, L);
  int trampoline_pos = kInvalidSlotPos;
  bool is_internal = false;
  if (L->is_linked() && !trampoline_emitted_) {
    unbound_labels_count_--;
    if (!is_internal_reference(L)) {
      next_buffer_check_ += kTrampolineSlotsSize;
    }
  }

  while (L->is_linked()) {
    int fixup_pos = L->pos();
    int dist = pos - fixup_pos;
    is_internal = is_internal_reference(L);
    next(L, is_internal);  // Call next before overwriting link with target
                           // at fixup_pos.
    Instr instr = instr_at(fixup_pos);
    DEBUG_PRINTF("\tfixup: %d to %d\n", fixup_pos, dist);
    if (is_internal) {
      RV_target_at_put(fixup_pos, pos, is_internal);
    } else {
      if (RV_IsBranch(instr)) {
        if (dist > kMaxBranchOffset) {
          if (trampoline_pos == kInvalidSlotPos) {
            trampoline_pos = get_trampoline_entry(fixup_pos);
            CHECK_NE(trampoline_pos, kInvalidSlotPos);
          }
          CHECK((trampoline_pos - fixup_pos) <= kMaxBranchOffset);
          DEBUG_PRINTF("\t\ttrampolining: %d\n", trampoline_pos);
          RV_target_at_put(fixup_pos, trampoline_pos, false);
          fixup_pos = trampoline_pos;
        }
        RV_target_at_put(fixup_pos, pos, false);
      } else {
        RV_target_at_put(fixup_pos, pos, false);
      }
    }
  }
  L->bind_to(pos);

  // Keep track of the last bound label so we don't eliminate any instructions
  // before a bound label.
  if (pos > last_bound_pos_) last_bound_pos_ = pos;
}

void Assembler::bind(Label* L) {
  DCHECK(!L->is_bound());  // Label can only be bound once.
  bind_to(L, pc_offset());
}

void Assembler::next(Label* L, bool is_internal) {
  DCHECK(L->is_linked());
  int link = RV_target_at(L->pos(), is_internal);
  if (link == kEndOfChain) {
    L->Unuse();
  } else {
    DCHECK_GT(link, 0);
    DEBUG_PRINTF("next: %p to %p (%d)\n", L,
                 reinterpret_cast<Instr*>(buffer_start_ + link), link);
    L->link_to(link);
  }
}

bool Assembler::is_near(Label* L) {
  DCHECK(L->is_bound());
  return is_intn((pc_offset() - L->pos()), kJumpOffsetBits);
}

bool Assembler::is_near(Label* L, OffsetSize bits) {
  if (L == nullptr || !L->is_bound()) return true;
  return is_intn((pc_offset() - L->pos()), bits);
}

bool Assembler::is_near_branch(Label* L) {
  DCHECK(L->is_bound());
  return is_intn((pc_offset() - L->pos()), kBranchOffsetBits);
}

int Assembler::RV_BranchOffset(Instr instr) {
  // | imm[12] | imm[10:5] | rs2 | rs1 | funct3 | imm[4:1|11] | opcode |
  //  31          25                      11          7
  int32_t imm13 = ((instr & 0xf00) >> 7) | ((instr & 0x7e000000) >> 20) |
                  ((instr & 0x80) << 4) | ((instr & 0x80000000) >> 19);
  imm13 = imm13 << 19 >> 19;
  return imm13;
}

int Assembler::RV_JumpOffset(Instr instr) {
  int32_t imm21 = ((instr & 0x7fe00000) >> 20) | ((instr & 0x100000) >> 9) |
                  (instr & 0xff000) | ((instr & 0x80000000) >> 11);
  imm21 = imm21 << 11 >> 11;
  return imm21;
}

// We have to use a temporary register for things that can be relocated even
// if they can be encoded in the MIPS's 16 bits of immediate-offset instruction
// space.  There is no guarantee that the relocated location can be similarly
// encoded.
bool Assembler::MustUseReg(RelocInfo::Mode rmode) {
  return !RelocInfo::IsNone(rmode);
}

void Assembler::disassembleInstr(Instr instr) {
  if (!FLAG_debug_riscv) return;
  disasm::NameConverter converter;
  disasm::Disassembler disasm(converter);
  EmbeddedVector<char, 128> disasm_buffer;

  disasm.InstructionDecode(disasm_buffer, reinterpret_cast<byte*>(&instr));
  DEBUG_PRINTF("%s\n", disasm_buffer.begin());
}

// ----- Top-level instruction formats match those in the ISA manual
// (R, I, S, B, U, J). These match the formats defined in LLVM's
// RISCVInstrFormats.td.
void Assembler::GenInstrR(uint8_t funct7, uint8_t funct3, Opcode opcode,
                          Register rd, Register rs1, Register rs2) {
  DCHECK(is_uint7(funct7) && is_uint3(funct3) && rd.is_valid() &&
         rs1.is_valid() && rs2.is_valid());
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct7 << kFunct7Shift);
  emit(instr);
}

void Assembler::GenInstrR(uint8_t funct7, uint8_t funct3, Opcode opcode,
                          FPURegister rd, FPURegister rs1, FPURegister rs2) {
  DCHECK(is_uint7(funct7) && is_uint3(funct3) && rd.is_valid() &&
         rs1.is_valid() && rs2.is_valid());
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct7 << kFunct7Shift);
  emit(instr);
}

void Assembler::GenInstrR(uint8_t funct7, uint8_t funct3, Opcode opcode,
                          Register rd, FPURegister rs1, Register rs2) {
  DCHECK(is_uint7(funct7) && is_uint3(funct3) && rd.is_valid() &&
         rs1.is_valid() && rs2.is_valid());
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct7 << kFunct7Shift);
  emit(instr);
}

void Assembler::GenInstrR(uint8_t funct7, uint8_t funct3, Opcode opcode,
                          FPURegister rd, Register rs1, Register rs2) {
  DCHECK(is_uint7(funct7) && is_uint3(funct3) && rd.is_valid() &&
         rs1.is_valid() && rs2.is_valid());
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct7 << kFunct7Shift);
  emit(instr);
}

void Assembler::GenInstrR(uint8_t funct7, uint8_t funct3, Opcode opcode,
                          FPURegister rd, FPURegister rs1, Register rs2) {
  DCHECK(is_uint7(funct7) && is_uint3(funct3) && rd.is_valid() &&
         rs1.is_valid() && rs2.is_valid());
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct7 << kFunct7Shift);
  emit(instr);
}

void Assembler::GenInstrR(uint8_t funct7, uint8_t funct3, Opcode opcode,
                          Register rd, FPURegister rs1, FPURegister rs2) {
  DCHECK(is_uint7(funct7) && is_uint3(funct3) && rd.is_valid() &&
         rs1.is_valid() && rs2.is_valid());
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct7 << kFunct7Shift);
  emit(instr);
}

void Assembler::GenInstrR4(uint8_t funct2, Opcode opcode, Register rd,
                           Register rs1, Register rs2, Register rs3,
                           RoundingMode frm) {
  DCHECK(is_uint2(funct2) && rd.is_valid() && rs1.is_valid() &&
         rs2.is_valid() && rs3.is_valid() && is_uint3(frm));
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (frm << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct2 << kFunct2Shift) | (rs3.code() << kRs3Shift);
  emit(instr);
}

void Assembler::GenInstrR4(uint8_t funct2, Opcode opcode, FPURegister rd,
                           FPURegister rs1, FPURegister rs2, FPURegister rs3,
                           RoundingMode frm) {
  DCHECK(is_uint2(funct2) && rd.is_valid() && rs1.is_valid() &&
         rs2.is_valid() && rs3.is_valid() && is_uint3(frm));
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (frm << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct2 << kFunct2Shift) | (rs3.code() << kRs3Shift);
  emit(instr);
}

void Assembler::GenInstrRAtomic(uint8_t funct5, bool aq, bool rl,
                                uint8_t funct3, Register rd, Register rs1,
                                Register rs2) {
  DCHECK(is_uint5(funct5) && is_uint3(funct3) && rd.is_valid() &&
         rs1.is_valid() && rs2.is_valid());
  Instr instr = AMO | (rd.code() << RV_kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (rl << kRlShift) | (aq << kAqShift) | (funct5 << kFunct5Shift);
  emit(instr);
}

void Assembler::GenInstrRFrm(uint8_t funct7, Opcode opcode, Register rd,
                             Register rs1, Register rs2, RoundingMode frm) {
  DCHECK(rd.is_valid() && rs1.is_valid() && rs2.is_valid() && is_uint3(frm));
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (frm << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct7 << kFunct7Shift);
  emit(instr);
}

void Assembler::GenInstrI(uint8_t funct3, Opcode opcode, Register rd,
                          Register rs1, int16_t imm12) {
  DCHECK(is_uint3(funct3) && rd.is_valid() && rs1.is_valid() &&
         (is_uint12(imm12) || is_int12(imm12)));
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (imm12 << kImm12Shift);
  emit(instr);
}

void Assembler::GenInstrI(uint8_t funct3, Opcode opcode, FPURegister rd,
                          Register rs1, int16_t imm12) {
  DCHECK(is_uint3(funct3) && rd.is_valid() && rs1.is_valid() &&
         (is_uint12(imm12) || is_int12(imm12)));
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (imm12 << kImm12Shift);
  emit(instr);
}

void Assembler::GenInstrIShift(bool arithshift, uint8_t funct3, Opcode opcode,
                               Register rd, Register rs1, uint8_t shamt) {
  DCHECK(is_uint3(funct3) && rd.is_valid() && rs1.is_valid() &&
         is_uint6(shamt));
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (shamt << kShamtShift) |
                (arithshift << kArithShiftShift);
  emit(instr);
}

void Assembler::GenInstrIShiftW(bool arithshift, uint8_t funct3, Opcode opcode,
                                Register rd, Register rs1, uint8_t shamt) {
  DCHECK(is_uint3(funct3) && rd.is_valid() && rs1.is_valid() &&
         is_uint5(shamt));
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (shamt << kShamtWShift) |
                (arithshift << kArithShiftShift);
  emit(instr);
}

void Assembler::GenInstrS(uint8_t funct3, Opcode opcode, Register rs1,
                          Register rs2, int16_t imm12) {
  DCHECK(is_uint3(funct3) && rs1.is_valid() && rs2.is_valid() &&
         is_int12(imm12));
  Instr instr = opcode | ((imm12 & 0x1f) << 7) |  // bits  4-0
                (funct3 << kFunct3Shift) | (rs1.code() << kRs1Shift) |
                (rs2.code() << kRs2Shift) |
                ((imm12 & 0xfe0) << 20);  // bits 11-5
  emit(instr);
}

void Assembler::GenInstrS(uint8_t funct3, Opcode opcode, Register rs1,
                          FPURegister rs2, int16_t imm12) {
  DCHECK(is_uint3(funct3) && rs1.is_valid() && rs2.is_valid() &&
         is_int12(imm12));
  Instr instr = opcode | ((imm12 & 0x1f) << 7) |  // bits  4-0
                (funct3 << kFunct3Shift) | (rs1.code() << kRs1Shift) |
                (rs2.code() << kRs2Shift) |
                ((imm12 & 0xfe0) << 20);  // bits 11-5
  emit(instr);
}

void Assembler::GenInstrB(uint8_t funct3, Opcode opcode, Register rs1,
                          Register rs2, int16_t imm13) {
  DCHECK(is_uint3(funct3) && rs1.is_valid() && rs2.is_valid() &&
         is_int13(imm13) && ((imm13 & 1) == 0));
  Instr instr = opcode | ((imm13 & 0x800) >> 4) |  // bit  11
                ((imm13 & 0x1e) << 7) |            // bits 4-1
                (funct3 << kFunct3Shift) | (rs1.code() << kRs1Shift) |
                (rs2.code() << kRs2Shift) |
                ((imm13 & 0x7e0) << 20) |  // bits 10-5
                ((imm13 & 0x1000) << 19);  // bit 12
  emit(instr);
}

void Assembler::GenInstrU(Opcode opcode, Register rd, int32_t imm20) {
  DCHECK(rd.is_valid() && is_int20(imm20));
  Instr instr = opcode | (rd.code() << RV_kRdShift) | (imm20 << kImm20Shift);
  emit(instr);
}

void Assembler::GenInstrJ(Opcode opcode, Register rd, int32_t imm21) {
  DCHECK(rd.is_valid() && is_int21(imm21) && ((imm21 & 1) == 0));
  Instr instr = opcode | (rd.code() << RV_kRdShift) |
                (imm21 & 0xff000) |          // bits 19-12
                ((imm21 & 0x800) << 9) |     // bit  11
                ((imm21 & 0x7fe) << 20) |    // bits 10-1
                ((imm21 & 0x100000) << 11);  // bit  20
  emit(instr);
}

// ----- Instruction class templates match those in LLVM's RISCVInstrInfo.td

void Assembler::GenInstrBranchCC_rri(uint8_t funct3, Register rs1, Register rs2,
                                     int16_t imm13) {
  GenInstrB(funct3, BRANCH, rs1, rs2, imm13);
}

void Assembler::GenInstrLoad_ri(uint8_t funct3, Register rd, Register rs1,
                                int16_t imm12) {
  GenInstrI(funct3, LOAD, rd, rs1, imm12);
}

void Assembler::GenInstrStore_rri(uint8_t funct3, Register rs1, Register rs2,
                                  int16_t imm12) {
  GenInstrS(funct3, STORE, rs1, rs2, imm12);
}

void Assembler::GenInstrALU_ri(uint8_t funct3, Register rd, Register rs1,
                               int16_t imm12) {
  GenInstrI(funct3, OP_IMM, rd, rs1, imm12);
}

void Assembler::GenInstrShift_ri(bool arithshift, uint8_t funct3, Register rd,
                                 Register rs1, uint8_t shamt) {
  DCHECK(is_uint6(shamt));
  GenInstrI(funct3, OP_IMM, rd, rs1, (arithshift << 10) | shamt);
}

void Assembler::GenInstrALU_rr(uint8_t funct7, uint8_t funct3, Register rd,
                               Register rs1, Register rs2) {
  GenInstrR(funct7, funct3, OP, rd, rs1, rs2);
}

void Assembler::GenInstrCSR_ir(uint8_t funct3, Register rd,
                               ControlStatusReg csr, Register rs1) {
  GenInstrI(funct3, SYSTEM, rd, rs1, csr);
}

void Assembler::GenInstrCSR_ii(uint8_t funct3, Register rd,
                               ControlStatusReg csr, uint8_t imm5) {
  GenInstrI(funct3, SYSTEM, rd, ToRegister(imm5), csr);
}

void Assembler::GenInstrShiftW_ri(bool arithshift, uint8_t funct3, Register rd,
                                  Register rs1, uint8_t shamt) {
  GenInstrIShiftW(arithshift, funct3, OP_IMM_32, rd, rs1, shamt);
}

void Assembler::GenInstrALUW_rr(uint8_t funct7, uint8_t funct3, Register rd,
                                Register rs1, Register rs2) {
  GenInstrR(funct7, funct3, OP_32, rd, rs1, rs2);
}

void Assembler::GenInstrPriv(uint8_t funct7, Register rs1, Register rs2) {
  GenInstrR(funct7, 0b000, SYSTEM, ToRegister(0), rs1, rs2);
}

void Assembler::GenInstrLoadFP_ri(uint8_t funct3, FPURegister rd, Register rs1,
                                  int16_t imm12) {
  GenInstrI(funct3, LOAD_FP, rd, rs1, imm12);
}

void Assembler::GenInstrStoreFP_rri(uint8_t funct3, Register rs1,
                                    FPURegister rs2, int16_t imm12) {
  GenInstrS(funct3, STORE_FP, rs1, rs2, imm12);
}

void Assembler::GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, FPURegister rd,
                                 FPURegister rs1, FPURegister rs2) {
  GenInstrR(funct7, funct3, OP_FP, rd, rs1, rs2);
}

void Assembler::GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, FPURegister rd,
                                 Register rs1, Register rs2) {
  GenInstrR(funct7, funct3, OP_FP, rd, rs1, rs2);
}

void Assembler::GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, FPURegister rd,
                                 FPURegister rs1, Register rs2) {
  GenInstrR(funct7, funct3, OP_FP, rd, rs1, rs2);
}

void Assembler::GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, Register rd,
                                 FPURegister rs1, Register rs2) {
  GenInstrR(funct7, funct3, OP_FP, rd, rs1, rs2);
}

void Assembler::GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, Register rd,
                                 FPURegister rs1, FPURegister rs2) {
  GenInstrR(funct7, funct3, OP_FP, rd, rs1, rs2);
}

// Original MIPS instruction templates

void Assembler::GenInstrRegister(Opcode opcode, Register rs, Register rt,
                                 Register rd, uint16_t sa,
                                 SecondaryField func) {
  DCHECK(rd.is_valid() && rs.is_valid() && rt.is_valid() && is_uint5(sa));
  Instr instr = opcode | (rs.code() << kRsShift) | (rt.code() << kRtShift) |
                (rd.code() << kRdShift) | (sa << kSaShift) | func;
  emit(instr);
}

void Assembler::GenInstrRegister(Opcode opcode, Register rs, Register rt,
                                 uint16_t msb, uint16_t lsb,
                                 SecondaryField func) {
  DCHECK(rs.is_valid() && rt.is_valid() && is_uint5(msb) && is_uint5(lsb));
  Instr instr = opcode | (rs.code() << kRsShift) | (rt.code() << kRtShift) |
                (msb << kRdShift) | (lsb << kSaShift) | func;
  emit(instr);
}

void Assembler::GenInstrRegister(Opcode opcode, SecondaryField fmt,
                                 FPURegister ft, FPURegister fs, FPURegister fd,
                                 SecondaryField func) {
  DCHECK(fd.is_valid() && fs.is_valid() && ft.is_valid());
  Instr instr = opcode | fmt | (ft.code() << kFtShift) |
                (fs.code() << kFsShift) | (fd.code() << kFdShift) | func;
  emit(instr);
}

void Assembler::GenInstrRegister(Opcode opcode, FPURegister fr, FPURegister ft,
                                 FPURegister fs, FPURegister fd,
                                 SecondaryField func) {
  DCHECK(fd.is_valid() && fr.is_valid() && fs.is_valid() && ft.is_valid());
  Instr instr = opcode | (fr.code() << kFrShift) | (ft.code() << kFtShift) |
                (fs.code() << kFsShift) | (fd.code() << kFdShift) | func;
  emit(instr);
}

void Assembler::GenInstrRegister(Opcode opcode, SecondaryField fmt, Register rt,
                                 FPURegister fs, FPURegister fd,
                                 SecondaryField func) {
  DCHECK(fd.is_valid() && fs.is_valid() && rt.is_valid());
  Instr instr = opcode | fmt | (rt.code() << kRtShift) |
                (fs.code() << kFsShift) | (fd.code() << kFdShift) | func;
  emit(instr);
}

void Assembler::GenInstrRegister(Opcode opcode, SecondaryField fmt, Register rt,
                                 FPUControlRegister fs, SecondaryField func) {
  DCHECK(fs.is_valid() && rt.is_valid());
  Instr instr =
      opcode | fmt | (rt.code() << kRtShift) | (fs.code() << kFsShift) | func;
  emit(instr);
}

// Instructions with immediate value.
// Registers are in the order of the instruction encoding, from left to right.
void Assembler::GenInstrImmediate(Opcode opcode, Register rs, Register rt,
                                  int32_t j,
                                  CompactBranchType is_compact_branch) {
  DCHECK(rs.is_valid() && rt.is_valid() && (is_int16(j) || is_uint16(j)));
  Instr instr = opcode | (rs.code() << kRsShift) | (rt.code() << kRtShift) |
                (j & kImm16Mask);
  emit(instr, is_compact_branch);
}

void Assembler::GenInstrImmediate(Opcode opcode, Register base, Register rt,
                                  int32_t offset9, int bit6,
                                  SecondaryField func) {
  DCHECK(base.is_valid() && rt.is_valid() && is_int9(offset9) &&
         is_uint1(bit6));
  Instr instr = opcode | (base.code() << kBaseShift) | (rt.code() << kRtShift) |
                ((offset9 << kImm9Shift) & kImm9Mask) | bit6 << kBit6Shift |
                func;
  emit(instr);
}

void Assembler::GenInstrImmediate(Opcode opcode, Register rs, SecondaryField SF,
                                  int32_t j,
                                  CompactBranchType is_compact_branch) {
  DCHECK(rs.is_valid() && (is_int16(j) || is_uint16(j)));
  Instr instr = opcode | (rs.code() << kRsShift) | SF | (j & kImm16Mask);
  emit(instr, is_compact_branch);
}

void Assembler::GenInstrImmediate(Opcode opcode, Register rs, FPURegister ft,
                                  int32_t j,
                                  CompactBranchType is_compact_branch) {
  DCHECK(rs.is_valid() && ft.is_valid() && (is_int16(j) || is_uint16(j)));
  Instr instr = opcode | (rs.code() << kRsShift) | (ft.code() << kFtShift) |
                (j & kImm16Mask);
  emit(instr, is_compact_branch);
}

void Assembler::GenInstrImmediate(Opcode opcode, Register rs, int32_t offset21,
                                  CompactBranchType is_compact_branch) {
  DCHECK(rs.is_valid() && (is_int21(offset21)));
  Instr instr = opcode | (rs.code() << kRsShift) | (offset21 & kImm21Mask);
  emit(instr, is_compact_branch);
}

void Assembler::GenInstrImmediate(Opcode opcode, Register rs,
                                  uint32_t offset21) {
  DCHECK(rs.is_valid() && (is_uint21(offset21)));
  Instr instr = opcode | (rs.code() << kRsShift) | (offset21 & kImm21Mask);
  emit(instr);
}

void Assembler::GenInstrImmediate(Opcode opcode, int32_t offset26,
                                  CompactBranchType is_compact_branch) {
  DCHECK(is_int26(offset26));
  Instr instr = opcode | (offset26 & kImm26Mask);
  emit(instr, is_compact_branch);
}

void Assembler::GenInstrJump(Opcode opcode, uint32_t address) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK(is_uint26(address));
  Instr instr = opcode | address;
  emit(instr);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}

// MSA instructions
void Assembler::GenInstrMsaI8(SecondaryField operation, uint32_t imm8,
                              MSARegister ws, MSARegister wd) {
  DCHECK((kArchVariant == kMips64r6) && IsEnabled(MIPS_SIMD));
  DCHECK(ws.is_valid() && wd.is_valid() && is_uint8(imm8));
  Instr instr = MSA | operation | ((imm8 & kImm8Mask) << kWtShift) |
                (ws.code() << kWsShift) | (wd.code() << kWdShift);
  emit(instr);
}

void Assembler::GenInstrMsaI5(SecondaryField operation, SecondaryField df,
                              int32_t imm5, MSARegister ws, MSARegister wd) {
  DCHECK((kArchVariant == kMips64r6) && IsEnabled(MIPS_SIMD));
  DCHECK(ws.is_valid() && wd.is_valid());
  DCHECK((operation == MAXI_S) || (operation == MINI_S) ||
                 (operation == CEQI) || (operation == CLTI_S) ||
                 (operation == CLEI_S)
             ? is_int5(imm5)
             : is_uint5(imm5));
  Instr instr = MSA | operation | df | ((imm5 & kImm5Mask) << kWtShift) |
                (ws.code() << kWsShift) | (wd.code() << kWdShift);
  emit(instr);
}

void Assembler::GenInstrMsaBit(SecondaryField operation, SecondaryField df,
                               uint32_t m, MSARegister ws, MSARegister wd) {
  DCHECK((kArchVariant == kMips64r6) && IsEnabled(MIPS_SIMD));
  DCHECK(ws.is_valid() && wd.is_valid() && is_valid_msa_df_m(df, m));
  Instr instr = MSA | operation | df | (m << kWtShift) |
                (ws.code() << kWsShift) | (wd.code() << kWdShift);
  emit(instr);
}

void Assembler::GenInstrMsaI10(SecondaryField operation, SecondaryField df,
                               int32_t imm10, MSARegister wd) {
  DCHECK((kArchVariant == kMips64r6) && IsEnabled(MIPS_SIMD));
  DCHECK(wd.is_valid() && is_int10(imm10));
  Instr instr = MSA | operation | df | ((imm10 & kImm10Mask) << kWsShift) |
                (wd.code() << kWdShift);
  emit(instr);
}

template <typename RegType>
void Assembler::GenInstrMsa3R(SecondaryField operation, SecondaryField df,
                              RegType t, MSARegister ws, MSARegister wd) {
  DCHECK((kArchVariant == kMips64r6) && IsEnabled(MIPS_SIMD));
  DCHECK(t.is_valid() && ws.is_valid() && wd.is_valid());
  Instr instr = MSA | operation | df | (t.code() << kWtShift) |
                (ws.code() << kWsShift) | (wd.code() << kWdShift);
  emit(instr);
}

template <typename DstType, typename SrcType>
void Assembler::GenInstrMsaElm(SecondaryField operation, SecondaryField df,
                               uint32_t n, SrcType src, DstType dst) {
  DCHECK((kArchVariant == kMips64r6) && IsEnabled(MIPS_SIMD));
  DCHECK(src.is_valid() && dst.is_valid() && is_valid_msa_df_n(df, n));
  Instr instr = MSA | operation | df | (n << kWtShift) |
                (src.code() << kWsShift) | (dst.code() << kWdShift) |
                MSA_ELM_MINOR;
  emit(instr);
}

void Assembler::GenInstrMsa3RF(SecondaryField operation, uint32_t df,
                               MSARegister wt, MSARegister ws, MSARegister wd) {
  DCHECK((kArchVariant == kMips64r6) && IsEnabled(MIPS_SIMD));
  DCHECK(wt.is_valid() && ws.is_valid() && wd.is_valid());
  DCHECK_LT(df, 2);
  Instr instr = MSA | operation | (df << 21) | (wt.code() << kWtShift) |
                (ws.code() << kWsShift) | (wd.code() << kWdShift);
  emit(instr);
}

void Assembler::GenInstrMsaVec(SecondaryField operation, MSARegister wt,
                               MSARegister ws, MSARegister wd) {
  DCHECK((kArchVariant == kMips64r6) && IsEnabled(MIPS_SIMD));
  DCHECK(wt.is_valid() && ws.is_valid() && wd.is_valid());
  Instr instr = MSA | operation | (wt.code() << kWtShift) |
                (ws.code() << kWsShift) | (wd.code() << kWdShift) |
                MSA_VEC_2R_2RF_MINOR;
  emit(instr);
}

void Assembler::GenInstrMsaMI10(SecondaryField operation, int32_t s10,
                                Register rs, MSARegister wd) {
  DCHECK((kArchVariant == kMips64r6) && IsEnabled(MIPS_SIMD));
  DCHECK(rs.is_valid() && wd.is_valid() && is_int10(s10));
  Instr instr = MSA | operation | ((s10 & kImm10Mask) << kWtShift) |
                (rs.code() << kWsShift) | (wd.code() << kWdShift);
  emit(instr);
}

void Assembler::GenInstrMsa2R(SecondaryField operation, SecondaryField df,
                              MSARegister ws, MSARegister wd) {
  DCHECK((kArchVariant == kMips64r6) && IsEnabled(MIPS_SIMD));
  DCHECK(ws.is_valid() && wd.is_valid());
  Instr instr = MSA | MSA_2R_FORMAT | operation | df | (ws.code() << kWsShift) |
                (wd.code() << kWdShift) | MSA_VEC_2R_2RF_MINOR;
  emit(instr);
}

void Assembler::GenInstrMsa2RF(SecondaryField operation, SecondaryField df,
                               MSARegister ws, MSARegister wd) {
  DCHECK((kArchVariant == kMips64r6) && IsEnabled(MIPS_SIMD));
  DCHECK(ws.is_valid() && wd.is_valid());
  Instr instr = MSA | MSA_2RF_FORMAT | operation | df |
                (ws.code() << kWsShift) | (wd.code() << kWdShift) |
                MSA_VEC_2R_2RF_MINOR;
  emit(instr);
}

void Assembler::GenInstrMsaBranch(SecondaryField operation, MSARegister wt,
                                  int32_t offset16) {
  DCHECK((kArchVariant == kMips64r6) && IsEnabled(MIPS_SIMD));
  DCHECK(wt.is_valid() && is_int16(offset16));
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Instr instr =
      COP1 | operation | (wt.code() << kWtShift) | (offset16 & kImm16Mask);
  emit(instr);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}

// Returns the next free trampoline entry.
int32_t Assembler::get_trampoline_entry(int32_t pos) {
  int32_t trampoline_entry = kInvalidSlotPos;
  if (!internal_trampoline_exception_) {
    if (trampoline_.start() > pos) {
      trampoline_entry = trampoline_.take_slot();
    }

    if (kInvalidSlotPos == trampoline_entry) {
      internal_trampoline_exception_ = true;
    }
  }
  return trampoline_entry;
}

uint64_t Assembler::jump_address(Label* L) {
  int64_t target_pos;
  DEBUG_PRINTF("jump_address: %p to %p (%d)\n", L,
               reinterpret_cast<Instr*>(buffer_start_ + pc_offset()),
               pc_offset());
  if (L->is_bound()) {
    target_pos = L->pos();
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link.
      L->link_to(pc_offset());
    } else {
      L->link_to(pc_offset());
      return kEndOfJumpChain;
    }
  }
  uint64_t imm = reinterpret_cast<uint64_t>(buffer_start_) + target_pos;
  DCHECK_EQ(imm & 3, 0);

  return imm;
}

uint64_t Assembler::jump_offset(Label* L) {
  int64_t target_pos;

  DEBUG_PRINTF("jump_offset: %p to %p (%d)\n", L,
               reinterpret_cast<Instr*>(buffer_start_ + pc_offset()),
               pc_offset());
  if (L->is_bound()) {
    target_pos = L->pos();
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link.
      L->link_to(pc_offset());
    } else {
      L->link_to(pc_offset());
      return kEndOfJumpChain;
    }
  }
  int64_t imm = target_pos - (pc_offset());
  DCHECK_EQ(imm & 3, 0);

  return static_cast<uint64_t>(imm);
}

uint64_t Assembler::branch_long_offset(Label* L) {
  int64_t target_pos;

  DEBUG_PRINTF("branch_long_offset: %p to %p (%d)\n", L,
               reinterpret_cast<Instr*>(buffer_start_ + pc_offset()),
               pc_offset());
  if (L->is_bound()) {
    target_pos = L->pos();
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link.
      L->link_to(pc_offset());
    } else {
      L->link_to(pc_offset());
      return kEndOfJumpChain;
    }
  }
  int64_t offset = target_pos - pc_offset();
  DCHECK_EQ(offset & 3, 0);

  return static_cast<uint64_t>(offset);
}

int32_t Assembler::branch_offset_helper(Label* L, OffsetSize bits) {
  int32_t target_pos;

  DEBUG_PRINTF("branch_offset_helper: %p to %p (%d)\n", L,
               reinterpret_cast<Instr*>(buffer_start_ + pc_offset()),
               pc_offset());
  if (L->is_bound()) {
    target_pos = L->pos();
    DEBUG_PRINTF("\tbound: %d", target_pos);
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();
      L->link_to(pc_offset());
      DEBUG_PRINTF("\tadded to link: %d\n", target_pos);
    } else {
      L->link_to(pc_offset());
      if (!trampoline_emitted_) {
        unbound_labels_count_++;
        next_buffer_check_ -= kTrampolineSlotsSize;
      }
      DEBUG_PRINTF("\tstarted link\n");
      return kEndOfChain;
    }
  }

  int32_t offset = target_pos - pc_offset();
  DCHECK(is_intn(offset, bits));
  DCHECK_EQ(offset & 1, 0);
  DEBUG_PRINTF("\toffset = %d\n", offset);
  return offset;
}

void Assembler::label_at_put(Label* L, int at_offset) {
  int target_pos;
  DEBUG_PRINTF("label_at_put: %p @ %p (%d)\n", L,
               reinterpret_cast<Instr*>(buffer_start_ + at_offset), at_offset);
  if (L->is_bound()) {
    target_pos = L->pos();
    instr_at_put(at_offset, target_pos + (Code::kHeaderSize - kHeapObjectTag));
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link.
      int32_t imm18 = target_pos - at_offset;
      DCHECK_EQ(imm18 & 3, 0);
      int32_t imm16 = imm18 >> 2;
      DCHECK(is_int16(imm16));
      instr_at_put(at_offset, (imm16 & kImm16Mask));
    } else {
      target_pos = kEndOfChain;
      instr_at_put(at_offset, 0);
      if (!trampoline_emitted_) {
        unbound_labels_count_++;
        next_buffer_check_ -= kTrampolineSlotsSize;
      }
    }
    L->link_to(at_offset);
  }
}

//------- Branch and jump instructions --------

void Assembler::b(int16_t offset) {
  if (is_int13(offset))
    RV_beq(zero_reg, zero_reg, offset);
  else {
    // Generate position independent long branch.
    BlockTrampolinePoolScope block_trampoline_pool(this);
    RV_auipc(t5, 0);    // Read PC into t5.
    RV_li(t6, offset);  // Load offset into t6
    RV_add(t6, t5, t6);
    RV_jr(t6);
  }
}

void Assembler::bal(int16_t offset) { bgezal(zero_reg, offset); }

void Assembler::bc(int32_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  GenInstrImmediate(BC, offset, CompactBranchType::COMPACT_BRANCH);
}

void Assembler::balc(int32_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  GenInstrImmediate(BALC, offset, CompactBranchType::COMPACT_BRANCH);
}

void Assembler::beq(Register rs, Register rt, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(BEQ, rs, rt, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}

void Assembler::bgez(Register rs, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(REGIMM, rs, BGEZ, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}

void Assembler::bgezc(Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rt != zero_reg);
  GenInstrImmediate(BLEZL, rt, rt, offset, CompactBranchType::COMPACT_BRANCH);
}

void Assembler::bgeuc(Register rs, Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rs != zero_reg);
  DCHECK(rt != zero_reg);
  DCHECK(rs.code() != rt.code());
  GenInstrImmediate(BLEZ, rs, rt, offset, CompactBranchType::COMPACT_BRANCH);
}

void Assembler::bgec(Register rs, Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rs != zero_reg);
  DCHECK(rt != zero_reg);
  DCHECK(rs.code() != rt.code());
  GenInstrImmediate(BLEZL, rs, rt, offset, CompactBranchType::COMPACT_BRANCH);
}

void Assembler::bgezal(Register rs, int16_t offset) {
  DCHECK(kArchVariant != kMips64r6 || rs == zero_reg);
  DCHECK(rs != ra);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(REGIMM, rs, BGEZAL, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}

void Assembler::bgtz(Register rs, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(BGTZ, rs, zero_reg, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}

void Assembler::bgtzc(Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rt != zero_reg);
  GenInstrImmediate(BGTZL, zero_reg, rt, offset,
                    CompactBranchType::COMPACT_BRANCH);
}

void Assembler::blez(Register rs, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(BLEZ, rs, zero_reg, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}

void Assembler::blezc(Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rt != zero_reg);
  GenInstrImmediate(BLEZL, zero_reg, rt, offset,
                    CompactBranchType::COMPACT_BRANCH);
}

void Assembler::bltzc(Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rt != zero_reg);
  GenInstrImmediate(BGTZL, rt, rt, offset, CompactBranchType::COMPACT_BRANCH);
}

void Assembler::bltuc(Register rs, Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rs != zero_reg);
  DCHECK(rt != zero_reg);
  DCHECK(rs.code() != rt.code());
  GenInstrImmediate(BGTZ, rs, rt, offset, CompactBranchType::COMPACT_BRANCH);
}

void Assembler::bltc(Register rs, Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rs != zero_reg);
  DCHECK(rt != zero_reg);
  DCHECK(rs.code() != rt.code());
  GenInstrImmediate(BGTZL, rs, rt, offset, CompactBranchType::COMPACT_BRANCH);
}

void Assembler::bltz(Register rs, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(REGIMM, rs, BLTZ, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}

void Assembler::bltzal(Register rs, int16_t offset) {
  DCHECK(kArchVariant != kMips64r6 || rs == zero_reg);
  DCHECK(rs != ra);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(REGIMM, rs, BLTZAL, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}

void Assembler::bne(Register rs, Register rt, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(BNE, rs, rt, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}

void Assembler::bovc(Register rs, Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  if (rs.code() >= rt.code()) {
    GenInstrImmediate(ADDI, rs, rt, offset, CompactBranchType::COMPACT_BRANCH);
  } else {
    GenInstrImmediate(ADDI, rt, rs, offset, CompactBranchType::COMPACT_BRANCH);
  }
}

void Assembler::bnvc(Register rs, Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  if (rs.code() >= rt.code()) {
    GenInstrImmediate(DADDI, rs, rt, offset, CompactBranchType::COMPACT_BRANCH);
  } else {
    GenInstrImmediate(DADDI, rt, rs, offset, CompactBranchType::COMPACT_BRANCH);
  }
}

void Assembler::blezalc(Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rt != zero_reg);
  DCHECK(rt != ra);
  GenInstrImmediate(BLEZ, zero_reg, rt, offset,
                    CompactBranchType::COMPACT_BRANCH);
}

void Assembler::bgezalc(Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rt != zero_reg);
  DCHECK(rt != ra);
  GenInstrImmediate(BLEZ, rt, rt, offset, CompactBranchType::COMPACT_BRANCH);
}

void Assembler::bgezall(Register rs, int16_t offset) {
  DCHECK_NE(kArchVariant, kMips64r6);
  DCHECK(rs != zero_reg);
  DCHECK(rs != ra);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(REGIMM, rs, BGEZALL, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}

void Assembler::bltzalc(Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rt != zero_reg);
  DCHECK(rt != ra);
  GenInstrImmediate(BGTZ, rt, rt, offset, CompactBranchType::COMPACT_BRANCH);
}

void Assembler::bgtzalc(Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rt != zero_reg);
  DCHECK(rt != ra);
  GenInstrImmediate(BGTZ, zero_reg, rt, offset,
                    CompactBranchType::COMPACT_BRANCH);
}

void Assembler::beqzalc(Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rt != zero_reg);
  DCHECK(rt != ra);
  GenInstrImmediate(ADDI, zero_reg, rt, offset,
                    CompactBranchType::COMPACT_BRANCH);
}

void Assembler::bnezalc(Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rt != zero_reg);
  DCHECK(rt != ra);
  GenInstrImmediate(DADDI, zero_reg, rt, offset,
                    CompactBranchType::COMPACT_BRANCH);
}

void Assembler::beqc(Register rs, Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rs.code() != rt.code() && rs.code() != 0 && rt.code() != 0);
  if (rs.code() < rt.code()) {
    GenInstrImmediate(ADDI, rs, rt, offset, CompactBranchType::COMPACT_BRANCH);
  } else {
    GenInstrImmediate(ADDI, rt, rs, offset, CompactBranchType::COMPACT_BRANCH);
  }
}

void Assembler::beqzc(Register rs, int32_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rs != zero_reg);
  GenInstrImmediate(POP66, rs, offset, CompactBranchType::COMPACT_BRANCH);
}

void Assembler::bnec(Register rs, Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rs.code() != rt.code() && rs.code() != 0 && rt.code() != 0);
  if (rs.code() < rt.code()) {
    GenInstrImmediate(DADDI, rs, rt, offset, CompactBranchType::COMPACT_BRANCH);
  } else {
    GenInstrImmediate(DADDI, rt, rs, offset, CompactBranchType::COMPACT_BRANCH);
  }
}

void Assembler::bnezc(Register rs, int32_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(rs != zero_reg);
  GenInstrImmediate(POP76, rs, offset, CompactBranchType::COMPACT_BRANCH);
}

void Assembler::j(int64_t target) {
  // Deprecated. Use PC-relative jumps instead.
  UNREACHABLE();
}

void Assembler::j(Label* target) {
  // Deprecated. Use PC-relative jumps instead.
  UNREACHABLE();
}

void Assembler::jal(Label* target) {
  // Deprecated. Use PC-relative jumps instead.
  UNREACHABLE();
}

void Assembler::jal(int64_t target) {
  // Deprecated. Use PC-relative jumps instead.
  UNREACHABLE();
}

void Assembler::jr(Register rs) {
  if (kArchVariant != kMips64r6) {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    GenInstrRegister(SPECIAL, rs, zero_reg, zero_reg, 0, JR);
    BlockTrampolinePoolFor(1);  // For associated delay slot.
  } else {
    jalr(rs, zero_reg);
  }
}

void Assembler::jalr(Register rs, Register rd) {
  DCHECK(rs.code() != rd.code());
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrRegister(SPECIAL, rs, zero_reg, rd, 0, JALR);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}

void Assembler::jic(Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  GenInstrImmediate(POP66, zero_reg, rt, offset);
}

void Assembler::jialc(Register rt, int16_t offset) {
  DCHECK_EQ(kArchVariant, kMips64r6);
  GenInstrImmediate(POP76, zero_reg, rt, offset);
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

void Assembler::RV_lui(Register rd, int32_t imm20) {
  GenInstrU(RV_LUI, rd, imm20);
}

void Assembler::RV_auipc(Register rd, int32_t imm20) {
  GenInstrU(RV_AUIPC, rd, imm20);
}

// Jumps

void Assembler::RV_jal(Register rd, int32_t imm21) {
  GenInstrJ(RV_JAL, rd, imm21);
}

void Assembler::RV_jalr(Register rd, Register rs1, int16_t imm12) {
  GenInstrI(0b000, RV_JALR, rd, rs1, imm12);
}

// Branches

void Assembler::RV_beq(Register rs1, Register rs2, int16_t imm13) {
  GenInstrBranchCC_rri(0b000, rs1, rs2, imm13);
}

void Assembler::RV_bne(Register rs1, Register rs2, int16_t imm13) {
  GenInstrBranchCC_rri(0b001, rs1, rs2, imm13);
}

void Assembler::RV_blt(Register rs1, Register rs2, int16_t imm13) {
  GenInstrBranchCC_rri(0b100, rs1, rs2, imm13);
}

void Assembler::RV_bge(Register rs1, Register rs2, int16_t imm13) {
  GenInstrBranchCC_rri(0b101, rs1, rs2, imm13);
}

void Assembler::RV_bltu(Register rs1, Register rs2, int16_t imm13) {
  GenInstrBranchCC_rri(0b110, rs1, rs2, imm13);
}

void Assembler::RV_bgeu(Register rs1, Register rs2, int16_t imm13) {
  GenInstrBranchCC_rri(0b111, rs1, rs2, imm13);
}

// Loads

void Assembler::RV_lb(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b000, rd, rs1, imm12);
}

void Assembler::RV_lh(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b001, rd, rs1, imm12);
}

void Assembler::RV_lw(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b010, rd, rs1, imm12);
}

void Assembler::RV_lbu(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b100, rd, rs1, imm12);
}

void Assembler::RV_lhu(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b101, rd, rs1, imm12);
}

// Stores

void Assembler::RV_sb(Register source, Register base, int16_t imm12) {
  GenInstrStore_rri(0b000, base, source, imm12);
}

void Assembler::RV_sh(Register source, Register base, int16_t imm12) {
  GenInstrStore_rri(0b001, base, source, imm12);
}

void Assembler::RV_sw(Register source, Register base, int16_t imm12) {
  GenInstrStore_rri(0b010, base, source, imm12);
}

// Arithmetic with immediate

void Assembler::RV_addi(Register rd, Register rs1, int16_t imm12) {
  GenInstrALU_ri(0b000, rd, rs1, imm12);
}

void Assembler::RV_slti(Register rd, Register rs1, int16_t imm12) {
  GenInstrALU_ri(0b010, rd, rs1, imm12);
}

void Assembler::RV_sltiu(Register rd, Register rs1, int16_t imm12) {
  GenInstrALU_ri(0b011, rd, rs1, imm12);
}

void Assembler::RV_xori(Register rd, Register rs1, int16_t imm12) {
  GenInstrALU_ri(0b100, rd, rs1, imm12);
}

void Assembler::RV_ori(Register rd, Register rs1, int16_t imm12) {
  GenInstrALU_ri(0b110, rd, rs1, imm12);
}

void Assembler::RV_andi(Register rd, Register rs1, int16_t imm12) {
  GenInstrALU_ri(0b111, rd, rs1, imm12);
}

void Assembler::RV_slli(Register rd, Register rs1, uint8_t shamt) {
  GenInstrShift_ri(0, 0b001, rd, rs1, shamt);
}

void Assembler::RV_srli(Register rd, Register rs1, uint8_t shamt) {
  GenInstrShift_ri(0, 0b101, rd, rs1, shamt);
}

void Assembler::RV_srai(Register rd, Register rs1, uint8_t shamt) {
  GenInstrShift_ri(1, 0b101, rd, rs1, shamt);
}

// Arithmetic

void Assembler::RV_add(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b000, rd, rs1, rs2);
}

void Assembler::RV_sub(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0100000, 0b000, rd, rs1, rs2);
}

void Assembler::RV_sll(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b001, rd, rs1, rs2);
}

void Assembler::RV_slt(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b010, rd, rs1, rs2);
}

void Assembler::RV_sltu(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b011, rd, rs1, rs2);
}

void Assembler::RV_xor_(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b100, rd, rs1, rs2);
}

void Assembler::RV_srl(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b101, rd, rs1, rs2);
}

void Assembler::RV_sra(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0100000, 0b101, rd, rs1, rs2);
}

void Assembler::RV_or_(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b110, rd, rs1, rs2);
}

void Assembler::RV_and_(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b111, rd, rs1, rs2);
}

// Memory fences

void Assembler::RV_fence(uint8_t pred, uint8_t succ) {
  DCHECK(is_uint4(pred) && is_uint4(succ));
  uint16_t imm12 = succ | (pred << 4) | (0b0000 << 8);
  GenInstrI(0b000, MISC_MEM, ToRegister(0), ToRegister(0), imm12);
}

void Assembler::RV_fence_tso() {
  uint16_t imm12 = (0b0011) | (0b0011 << 4) | (0b1000 << 8);
  GenInstrI(0b000, MISC_MEM, ToRegister(0), ToRegister(0), imm12);
}

void Assembler::RV_fence_i() {
  GenInstrI(0b001, MISC_MEM, ToRegister(0), ToRegister(0), 0);
}

// Environment call / break

void Assembler::RV_ecall() {
  GenInstrI(0b000, SYSTEM, ToRegister(0), ToRegister(0), 0);
}

void Assembler::RV_ebreak() {
  GenInstrI(0b000, SYSTEM, ToRegister(0), ToRegister(0), 1);
}

// This is a de facto standard (as set by GNU binutils) 32-bit unimplemented
// instruction (i.e., it should always trap, if your implementation has invalid
// instruction traps).
void Assembler::RV_unimp() {
  GenInstrI(0b001, SYSTEM, ToRegister(0), ToRegister(0), 0b110000000000);
}

// CSR

void Assembler::RV_csrrw(Register rd, ControlStatusReg csr, Register rs1) {
  GenInstrCSR_ir(0b001, rd, csr, rs1);
}

void Assembler::RV_csrrs(Register rd, ControlStatusReg csr, Register rs1) {
  GenInstrCSR_ir(0b010, rd, csr, rs1);
}

void Assembler::RV_csrrc(Register rd, ControlStatusReg csr, Register rs1) {
  GenInstrCSR_ir(0b011, rd, csr, rs1);
}

void Assembler::RV_csrrwi(Register rd, ControlStatusReg csr, uint8_t imm5) {
  GenInstrCSR_ii(0b101, rd, csr, imm5);
}

void Assembler::RV_csrrsi(Register rd, ControlStatusReg csr, uint8_t imm5) {
  GenInstrCSR_ii(0b110, rd, csr, imm5);
}

void Assembler::RV_csrrci(Register rd, ControlStatusReg csr, uint8_t imm5) {
  GenInstrCSR_ii(0b111, rd, csr, imm5);
}

// RV64I

void Assembler::RV_lwu(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b110, rd, rs1, imm12);
}

void Assembler::RV_ld(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b011, rd, rs1, imm12);
}

void Assembler::RV_sd(Register source, Register base, int16_t imm12) {
  GenInstrStore_rri(0b011, base, source, imm12);
}

void Assembler::RV_addiw(Register rd, Register rs1, int16_t imm12) {
  GenInstrI(0b000, OP_IMM_32, rd, rs1, imm12);
}

void Assembler::RV_slliw(Register rd, Register rs1, uint8_t shamt) {
  GenInstrShiftW_ri(0, 0b001, rd, rs1, shamt);
}

void Assembler::RV_srliw(Register rd, Register rs1, uint8_t shamt) {
  GenInstrShiftW_ri(0, 0b101, rd, rs1, shamt);
}

void Assembler::RV_sraiw(Register rd, Register rs1, uint8_t shamt) {
  GenInstrShiftW_ri(1, 0b101, rd, rs1, shamt);
}

void Assembler::RV_addw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0000000, 0b000, rd, rs1, rs2);
}

void Assembler::RV_subw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0100000, 0b000, rd, rs1, rs2);
}

void Assembler::RV_sllw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0000000, 0b001, rd, rs1, rs2);
}

void Assembler::RV_srlw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0000000, 0b101, rd, rs1, rs2);
}

void Assembler::RV_sraw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0100000, 0b101, rd, rs1, rs2);
}

// RV32M Standard Extension

void Assembler::RV_mul(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000001, 0b000, rd, rs1, rs2);
}

void Assembler::RV_mulh(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000001, 0b001, rd, rs1, rs2);
}

void Assembler::RV_mulhsu(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000001, 0b010, rd, rs1, rs2);
}

void Assembler::RV_mulhu(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000001, 0b011, rd, rs1, rs2);
}

void Assembler::RV_div(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000001, 0b100, rd, rs1, rs2);
}

void Assembler::RV_divu(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000001, 0b101, rd, rs1, rs2);
}

void Assembler::RV_rem(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000001, 0b110, rd, rs1, rs2);
}

void Assembler::RV_remu(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000001, 0b111, rd, rs1, rs2);
}

// RV64M Standard Extension (in addition to RV32M)

void Assembler::RV_mulw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0000001, 0b000, rd, rs1, rs2);
}

void Assembler::RV_divw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0000001, 0b100, rd, rs1, rs2);
}

void Assembler::RV_divuw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0000001, 0b101, rd, rs1, rs2);
}

void Assembler::RV_remw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0000001, 0b110, rd, rs1, rs2);
}

void Assembler::RV_remuw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0000001, 0b111, rd, rs1, rs2);
}

// RV32A Standard Extension

void Assembler::RV_lr_w(bool aq, bool rl, Register rd, Register rs1) {
  GenInstrRAtomic(0b00010, aq, rl, 0b010, rd, rs1, zero_reg);
}

void Assembler::RV_sc_w(bool aq, bool rl, Register rd, Register rs1,
                        Register rs2) {
  GenInstrRAtomic(0b00011, aq, rl, 0b010, rd, rs1, rs2);
}

void Assembler::RV_amoswap_w(bool aq, bool rl, Register rd, Register rs1,
                             Register rs2) {
  GenInstrRAtomic(0b00001, aq, rl, 0b010, rd, rs1, rs2);
}

void Assembler::RV_amoadd_w(bool aq, bool rl, Register rd, Register rs1,
                            Register rs2) {
  GenInstrRAtomic(0b00000, aq, rl, 0b010, rd, rs1, rs2);
}

void Assembler::RV_amoxor_w(bool aq, bool rl, Register rd, Register rs1,
                            Register rs2) {
  GenInstrRAtomic(0b00100, aq, rl, 0b010, rd, rs1, rs2);
}

void Assembler::RV_amoand_w(bool aq, bool rl, Register rd, Register rs1,
                            Register rs2) {
  GenInstrRAtomic(0b01100, aq, rl, 0b010, rd, rs1, rs2);
}

void Assembler::RV_amoor_w(bool aq, bool rl, Register rd, Register rs1,
                           Register rs2) {
  GenInstrRAtomic(0b01000, aq, rl, 0b010, rd, rs1, rs2);
}

void Assembler::RV_amomin_w(bool aq, bool rl, Register rd, Register rs1,
                            Register rs2) {
  GenInstrRAtomic(0b10000, aq, rl, 0b010, rd, rs1, rs2);
}

void Assembler::RV_amomax_w(bool aq, bool rl, Register rd, Register rs1,
                            Register rs2) {
  GenInstrRAtomic(0b10100, aq, rl, 0b010, rd, rs1, rs2);
}

void Assembler::RV_amominu_w(bool aq, bool rl, Register rd, Register rs1,
                             Register rs2) {
  GenInstrRAtomic(0b11000, aq, rl, 0b010, rd, rs1, rs2);
}

void Assembler::RV_amomaxu_w(bool aq, bool rl, Register rd, Register rs1,
                             Register rs2) {
  GenInstrRAtomic(0b11100, aq, rl, 0b010, rd, rs1, rs2);
}

// RV64A Standard Extension (in addition to RV32A)

void Assembler::RV_lr_d(bool aq, bool rl, Register rd, Register rs1) {
  GenInstrRAtomic(0b00010, aq, rl, 0b011, rd, rs1, zero_reg);
}

void Assembler::RV_sc_d(bool aq, bool rl, Register rd, Register rs1,
                        Register rs2) {
  GenInstrRAtomic(0b00011, aq, rl, 0b011, rd, rs1, rs2);
}

void Assembler::RV_amoswap_d(bool aq, bool rl, Register rd, Register rs1,
                             Register rs2) {
  GenInstrRAtomic(0b00001, aq, rl, 0b011, rd, rs1, rs2);
}

void Assembler::RV_amoadd_d(bool aq, bool rl, Register rd, Register rs1,
                            Register rs2) {
  GenInstrRAtomic(0b00000, aq, rl, 0b011, rd, rs1, rs2);
}

void Assembler::RV_amoxor_d(bool aq, bool rl, Register rd, Register rs1,
                            Register rs2) {
  GenInstrRAtomic(0b00100, aq, rl, 0b011, rd, rs1, rs2);
}

void Assembler::RV_amoand_d(bool aq, bool rl, Register rd, Register rs1,
                            Register rs2) {
  GenInstrRAtomic(0b01100, aq, rl, 0b011, rd, rs1, rs2);
}

void Assembler::RV_amoor_d(bool aq, bool rl, Register rd, Register rs1,
                           Register rs2) {
  GenInstrRAtomic(0b01000, aq, rl, 0b011, rd, rs1, rs2);
}

void Assembler::RV_amomin_d(bool aq, bool rl, Register rd, Register rs1,
                            Register rs2) {
  GenInstrRAtomic(0b10000, aq, rl, 0b011, rd, rs1, rs2);
}

void Assembler::RV_amomax_d(bool aq, bool rl, Register rd, Register rs1,
                            Register rs2) {
  GenInstrRAtomic(0b10100, aq, rl, 0b011, rd, rs1, rs2);
}

void Assembler::RV_amominu_d(bool aq, bool rl, Register rd, Register rs1,
                             Register rs2) {
  GenInstrRAtomic(0b11000, aq, rl, 0b011, rd, rs1, rs2);
}

void Assembler::RV_amomaxu_d(bool aq, bool rl, Register rd, Register rs1,
                             Register rs2) {
  GenInstrRAtomic(0b11100, aq, rl, 0b011, rd, rs1, rs2);
}

// RV32F Standard Extension

void Assembler::RV_flw(FPURegister rd, Register rs1, int16_t imm12) {
  GenInstrLoadFP_ri(0b010, rd, rs1, imm12);
}

void Assembler::RV_fsw(FPURegister source, Register base, int16_t imm12) {
  GenInstrStoreFP_rri(0b010, base, source, imm12);
}

void Assembler::RV_fmadd_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                           FPURegister rs3, RoundingMode frm) {
  GenInstrR4(0b00, MADD, rd, rs1, rs2, rs3, frm);
}

void Assembler::RV_fmsub_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                           FPURegister rs3, RoundingMode frm) {
  GenInstrR4(0b00, MSUB, rd, rs1, rs2, rs3, frm);
}

void Assembler::RV_fnmsub_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                            FPURegister rs3, RoundingMode frm) {
  GenInstrR4(0b00, NMSUB, rd, rs1, rs2, rs3, frm);
}

void Assembler::RV_fnmadd_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                            FPURegister rs3, RoundingMode frm) {
  GenInstrR4(0b00, NMADD, rd, rs1, rs2, rs3, frm);
}

void Assembler::RV_fadd_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                          RoundingMode frm) {
  GenInstrALUFP_rr(0b0000000, frm, rd, rs1, rs2);
}

void Assembler::RV_fsub_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                          RoundingMode frm) {
  GenInstrALUFP_rr(0b0000100, frm, rd, rs1, rs2);
}

void Assembler::RV_fmul_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                          RoundingMode frm) {
  GenInstrALUFP_rr(0b0001000, frm, rd, rs1, rs2);
}

void Assembler::RV_fdiv_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                          RoundingMode frm) {
  GenInstrALUFP_rr(0b0001100, frm, rd, rs1, rs2);
}

void Assembler::RV_fsqrt_s(FPURegister rd, FPURegister rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b0101100, frm, rd, rs1, zero_reg);
}

void Assembler::RV_fsgnj_s(FPURegister rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b0010000, 0b000, rd, rs1, rs2);
}

void Assembler::RV_fsgnjn_s(FPURegister rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b0010000, 0b001, rd, rs1, rs2);
}

void Assembler::RV_fsgnjx_s(FPURegister rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b0010000, 0b010, rd, rs1, rs2);
}

void Assembler::RV_fmin_s(FPURegister rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b0010100, 0b000, rd, rs1, rs2);
}

void Assembler::RV_fmax_s(FPURegister rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b0010100, 0b001, rd, rs1, rs2);
}

void Assembler::RV_fcvt_w_s(Register rd, FPURegister rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1100000, frm, rd, rs1, zero_reg);
}

void Assembler::RV_fcvt_wu_s(Register rd, FPURegister rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1100000, frm, rd, rs1, ToRegister(1));
}

void Assembler::RV_fmv_x_w(Register rd, FPURegister rs1) {
  GenInstrALUFP_rr(0b1110000, 0b000, rd, rs1, zero_reg);
}

void Assembler::RV_feq_s(Register rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b1010000, 0b010, rd, rs1, rs2);
}

void Assembler::RV_flt_s(Register rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b1010000, 0b001, rd, rs1, rs2);
}

void Assembler::RV_fle_s(Register rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b1010000, 0b000, rd, rs1, rs2);
}

void Assembler::RV_fclass_s(Register rd, FPURegister rs1) {
  GenInstrALUFP_rr(0b1110000, 0b001, rd, rs1, zero_reg);
}

void Assembler::RV_fcvt_s_w(FPURegister rd, Register rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1101000, frm, rd, rs1, zero_reg);
}

void Assembler::RV_fcvt_s_wu(FPURegister rd, Register rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1101000, frm, rd, rs1, ToRegister(1));
}

void Assembler::RV_fmv_w_x(FPURegister rd, Register rs1) {
  GenInstrALUFP_rr(0b1111000, 0b000, rd, rs1, zero_reg);
}

// RV64F Standard Extension (in addition to RV32F)

void Assembler::RV_fcvt_l_s(Register rd, FPURegister rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1100000, frm, rd, rs1, ToRegister(2));
}

void Assembler::RV_fcvt_lu_s(Register rd, FPURegister rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1100000, frm, rd, rs1, ToRegister(3));
}

void Assembler::RV_fcvt_s_l(FPURegister rd, Register rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1101000, frm, rd, rs1, ToRegister(2));
}

void Assembler::RV_fcvt_s_lu(FPURegister rd, Register rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1101000, frm, rd, rs1, ToRegister(3));
}

// RV32D Standard Extension

void Assembler::RV_fld(FPURegister rd, Register rs1, int16_t imm12) {
  GenInstrLoadFP_ri(0b011, rd, rs1, imm12);
}

void Assembler::RV_fsd(FPURegister source, Register base, int16_t imm12) {
  GenInstrStoreFP_rri(0b011, base, source, imm12);
}

void Assembler::RV_fmadd_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                           FPURegister rs3, RoundingMode frm) {
  GenInstrR4(0b01, MADD, rd, rs1, rs2, rs3, frm);
}

void Assembler::RV_fmsub_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                           FPURegister rs3, RoundingMode frm) {
  GenInstrR4(0b01, MSUB, rd, rs1, rs2, rs3, frm);
}

void Assembler::RV_fnmsub_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                            FPURegister rs3, RoundingMode frm) {
  GenInstrR4(0b01, NMSUB, rd, rs1, rs2, rs3, frm);
}

void Assembler::RV_fnmadd_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                            FPURegister rs3, RoundingMode frm) {
  GenInstrR4(0b01, NMADD, rd, rs1, rs2, rs3, frm);
}

void Assembler::RV_fadd_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                          RoundingMode frm) {
  GenInstrALUFP_rr(0b0000001, frm, rd, rs1, rs2);
}

void Assembler::RV_fsub_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                          RoundingMode frm) {
  GenInstrALUFP_rr(0b0000101, frm, rd, rs1, rs2);
}

void Assembler::RV_fmul_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                          RoundingMode frm) {
  GenInstrALUFP_rr(0b0001001, frm, rd, rs1, rs2);
}

void Assembler::RV_fdiv_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                          RoundingMode frm) {
  GenInstrALUFP_rr(0b0001101, frm, rd, rs1, rs2);
}

void Assembler::RV_fsqrt_d(FPURegister rd, FPURegister rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b0101101, frm, rd, rs1, zero_reg);
}

void Assembler::RV_fsgnj_d(FPURegister rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b0010001, 0b000, rd, rs1, rs2);
}

void Assembler::RV_fsgnjn_d(FPURegister rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b0010001, 0b001, rd, rs1, rs2);
}

void Assembler::RV_fsgnjx_d(FPURegister rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b0010001, 0b010, rd, rs1, rs2);
}

void Assembler::RV_fmin_d(FPURegister rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b0010101, 0b000, rd, rs1, rs2);
}

void Assembler::RV_fmax_d(FPURegister rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b0010101, 0b001, rd, rs1, rs2);
}

void Assembler::RV_fcvt_s_d(FPURegister rd, FPURegister rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b0100000, frm, rd, rs1, ToRegister(1));
}

void Assembler::RV_fcvt_d_s(FPURegister rd, FPURegister rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b0100001, frm, rd, rs1, zero_reg);
}

void Assembler::RV_feq_d(Register rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b1010001, 0b010, rd, rs1, rs2);
}

void Assembler::RV_flt_d(Register rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b1010001, 0b001, rd, rs1, rs2);
}

void Assembler::RV_fle_d(Register rd, FPURegister rs1, FPURegister rs2) {
  GenInstrALUFP_rr(0b1010001, 0b000, rd, rs1, rs2);
}

void Assembler::RV_fclass_d(Register rd, FPURegister rs1) {
  GenInstrALUFP_rr(0b1110001, 0b001, rd, rs1, zero_reg);
}

void Assembler::RV_fcvt_w_d(Register rd, FPURegister rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1100001, frm, rd, rs1, zero_reg);
}

void Assembler::RV_fcvt_wu_d(Register rd, FPURegister rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1100001, frm, rd, rs1, ToRegister(1));
}

void Assembler::RV_fcvt_d_w(FPURegister rd, Register rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1101001, frm, rd, rs1, zero_reg);
}

void Assembler::RV_fcvt_d_wu(FPURegister rd, Register rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1101001, frm, rd, rs1, ToRegister(1));
}

// RV64D Standard Extension (in addition to RV32D)

void Assembler::RV_fcvt_l_d(Register rd, FPURegister rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1100001, frm, rd, rs1, ToRegister(2));
}

void Assembler::RV_fcvt_lu_d(Register rd, FPURegister rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1100001, frm, rd, rs1, ToRegister(3));
}

void Assembler::RV_fmv_x_d(Register rd, FPURegister rs1) {
  GenInstrALUFP_rr(0b1110001, 0b000, rd, rs1, zero_reg);
}

void Assembler::RV_fcvt_d_l(FPURegister rd, Register rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1101001, frm, rd, rs1, ToRegister(2));
}

void Assembler::RV_fcvt_d_lu(FPURegister rd, Register rs1, RoundingMode frm) {
  GenInstrALUFP_rr(0b1101001, frm, rd, rs1, ToRegister(3));
}

void Assembler::RV_fmv_d_x(FPURegister rd, Register rs1) {
  GenInstrALUFP_rr(0b1111001, 0b000, rd, rs1, zero_reg);
}

// Privileged

void Assembler::RV_uret() {
  GenInstrPriv(0b0000000, ToRegister(0), ToRegister(0b00010));
}

void Assembler::RV_sret() {
  GenInstrPriv(0b0001000, ToRegister(0), ToRegister(0b00010));
}

void Assembler::RV_mret() {
  GenInstrPriv(0b0011000, ToRegister(0), ToRegister(0b00010));
}

void Assembler::RV_wfi() {
  GenInstrPriv(0b0001000, ToRegister(0), ToRegister(0b00101));
}

void Assembler::RV_sfence_vma(Register rs1, Register rs2) {
  GenInstrR(0b0001001, 0b000, SYSTEM, ToRegister(0), rs1, rs2);
}

// Assembler Pseudo Instructions (Tables 25.2 and 25.3, RISC-V Unprivileged ISA)

void Assembler::RV_nop() { RV_addi(ToRegister(0), ToRegister(0), 0); }
void Assembler::RV_li(Register rd, int64_t imm) {
  if (is_int32(imm + 0x800)) {
    // Based on LLVM's `generateInstSeq` (RISCVMatInt.cpp)
    // Depending on the active bits in the immediate Value v, the following
    // instruction sequences are emitted:
    //
    // v == 0                        : ADDI
    // v[0,12) != 0 && v[12,32) == 0 : ADDI
    // v[0,12) == 0 && v[12,32) != 0 : LUI
    // v[0,32) != 0                  : LUI+ADDI(W)
    int64_t Hi20 = ((imm + 0x800) >> 12);
    int64_t Lo12 = imm << 52 >> 52;
    Register base = zero_reg;

    if (Hi20) {
      RV_lui(rd, (int32_t)Hi20);
      base = rd;
    }

    if (Lo12 || Hi20 == 0) {
      RV_addi(rd, base, Lo12);
    }
    return;
  }

  assert(V8_TARGET_ARCH_64_BIT && "Can't emit >32-bit imm for non-RV64 target");

  // In the worst case, for a full 64-bit constant, a sequence of 8 instructions
  // (i.e., LUI+ADDIW+SLLI+ADDI+SLLI+ADDI+SLLI+ADDI) has to be emmitted. Note
  // that the first two instructions (LUI+ADDIW) can contribute up to 32 bits
  // while the following ADDI instructions contribute up to 12 bits each.
  //
  // On the first glance, implementing this seems to be possible by simply
  // emitting the most significant 32 bits (LUI+ADDIW) followed by as many left
  // shift (SLLI) and immediate additions (ADDI) as needed. However, due to the
  // fact that ADDI performs a sign extended addition, doing it like that would
  // only be possible when at most 11 bits of the ADDI instructions are used.
  // Using all 12 bits of the ADDI instructions, like done by GAS, actually
  // requires that the constant is processed starting with the least significant
  // bit.
  //
  // In the following, constants are processed from LSB to MSB but instruction
  // emission is performed from MSB to LSB by recursively calling
  // generateInstSeq. In each recursion, first the lowest 12 bits are removed
  // from the constant and the optimal shift amount, which can be greater than
  // 12 bits if the constant is sparse, is determined. Then, the shifted
  // remaining constant is processed recursively and gets emitted as soon as it
  // fits into 32 bits. The emission of the shifts and additions is subsequently
  // performed when the recursion returns.

  int64_t Lo12 = imm << 52 >> 52;
  int64_t Hi52 = ((uint64_t)imm + 0x800ull) >> 12;
  int FirstBit = 0;
  uint64_t Val = Hi52;
  while ((Val & 1) == 0) {
    Val = Val >> 1;
    FirstBit++;
  }
  int ShiftAmount = 12 + FirstBit;
  Hi52 = (Hi52 >> (ShiftAmount - 12)) << ShiftAmount >> ShiftAmount;

  RV_li(rd, Hi52);

  RV_slli(rd, rd, ShiftAmount);
  if (Lo12) RV_addi(rd, rd, Lo12);
}
int Assembler::li_count(int64_t imm) {
  int count = 0;
  if (is_int32(imm + 0x800)) {
    int64_t Hi20 = ((imm + 0x800) >> 12);
    int64_t Lo12 = imm << 52 >> 52;

    if (Hi20) count++;

    if (Lo12 || Hi20 == 0) count++;
    return count;
  }

  int64_t Lo12 = imm << 52 >> 52;
  int64_t Hi52 = ((uint64_t)imm + 0x800ull) >> 12;
  int FirstBit = 0;
  uint64_t Val = Hi52;
  while ((Val & 1) == 0) {
    Val = Val >> 1;
    FirstBit++;
  }
  int ShiftAmount = 12 + FirstBit;
  Hi52 = (Hi52 >> (ShiftAmount - 12)) << ShiftAmount >> ShiftAmount;

  count += li_count(Hi52);

  count++;
  if (Lo12) count++;

  return count;
}
void Assembler::RV_li_constant(Register rd, int64_t imm) {
  DEBUG_PRINTF("RV_li_constant(%d, %lx <%ld>)\n", ToNumber(rd), imm, imm);
  RV_lui(rd, (imm + (1LL << 47) + (1LL << 35) + (1LL << 23) + (1LL << 11)) >>
                 48);  // Bits 63:48
  RV_addiw(rd, rd,
           (imm + (1LL << 35) + (1LL << 23) + (1LL << 11)) << 16 >>
               52);  // Bits 47:36
  RV_slli(rd, rd, 12);
  RV_addi(rd, rd, (imm + (1LL << 23) + (1LL << 11)) << 28 >> 52);  // Bits 35:24
  RV_slli(rd, rd, 12);
  RV_addi(rd, rd, (imm + (1LL << 11)) << 40 >> 52);  // Bits 23:12
  RV_slli(rd, rd, 12);
  RV_addi(rd, rd, imm << 52 >> 52);  // Bits 11:0
}
void Assembler::RV_mv(Register rd, Register rs) { RV_addi(rd, rs, 0); }
void Assembler::RV_not(Register rd, Register rs) { RV_xori(rd, rs, -1); }
void Assembler::RV_neg(Register rd, Register rs) { RV_sub(rd, zero_reg, rs); }
void Assembler::RV_negw(Register rd, Register rs) { RV_subw(rd, zero_reg, rs); }
void Assembler::RV_sext_w(Register rd, Register rs) { RV_addiw(rd, rs, 0); }
void Assembler::RV_seqz(Register rd, Register rs) { RV_sltiu(rd, rs, 1); }
void Assembler::RV_snez(Register rd, Register rs) { RV_sltu(rd, zero_reg, rs); }
void Assembler::RV_sltz(Register rd, Register rs) { RV_slt(rd, rs, zero_reg); }
void Assembler::RV_sgtz(Register rd, Register rs) { RV_slt(rd, zero_reg, rs); }

void Assembler::RV_fmv_s(FPURegister rd, FPURegister rs) {
  RV_fsgnj_s(rd, rs, rs);
}
void Assembler::RV_fabs_s(FPURegister rd, FPURegister rs) {
  RV_fsgnjx_s(rd, rs, rs);
}
void Assembler::RV_fneg_s(FPURegister rd, FPURegister rs) {
  RV_fsgnjn_s(rd, rs, rs);
}
void Assembler::RV_fmv_d(FPURegister rd, FPURegister rs) {
  RV_fsgnj_d(rd, rs, rs);
}
void Assembler::RV_fabs_d(FPURegister rd, FPURegister rs) {
  RV_fsgnjx_d(rd, rs, rs);
}
void Assembler::RV_fneg_d(FPURegister rd, FPURegister rs) {
  RV_fsgnjn_d(rd, rs, rs);
}

void Assembler::RV_beqz(Register rs, int16_t imm13) {
  RV_beq(rs, zero_reg, imm13);
}
void Assembler::RV_bnez(Register rs, int16_t imm13) {
  RV_bne(rs, zero_reg, imm13);
}
void Assembler::RV_blez(Register rs, int16_t imm13) {
  RV_bge(zero_reg, rs, imm13);
}
void Assembler::RV_bgez(Register rs, int16_t imm13) {
  RV_bge(rs, zero_reg, imm13);
}
void Assembler::RV_bltz(Register rs, int16_t imm13) {
  RV_blt(rs, zero_reg, imm13);
}
void Assembler::RV_bgtz(Register rs, int16_t imm13) {
  RV_blt(zero_reg, rs, imm13);
}

void Assembler::RV_bgt(Register rs1, Register rs2, int16_t imm13) {
  RV_blt(rs2, rs1, imm13);
}
void Assembler::RV_ble(Register rs1, Register rs2, int16_t imm13) {
  RV_bge(rs2, rs1, imm13);
}
void Assembler::RV_bgtu(Register rs1, Register rs2, int16_t imm13) {
  RV_bltu(rs2, rs1, imm13);
}
void Assembler::RV_bleu(Register rs1, Register rs2, int16_t imm13) {
  RV_bgeu(rs2, rs1, imm13);
}

// TODO: Replace uses of ToRegister with names once they are properly defined
void Assembler::RV_j(int32_t imm21) { RV_jal(zero_reg, imm21); }
void Assembler::RV_jal(int32_t imm21) { RV_jal(ToRegister(1), imm21); }
void Assembler::RV_jr(Register rs) { RV_jalr(zero_reg, rs, 0); }
void Assembler::RV_jalr(Register rs) { RV_jalr(ToRegister(1), rs, 0); }
void Assembler::RV_ret() { RV_jalr(zero_reg, ToRegister(1), 0); }
void Assembler::RV_call(int32_t offset) {
  RV_auipc(ToRegister(1), (offset >> 12) + ((offset & 0x800) >> 11));
  RV_jalr(ToRegister(1), ToRegister(1), offset << 20 >> 20);
}

void Assembler::RV_rdinstret(Register rd) {
  RV_csrrs(rd, csr_instret, zero_reg);
}
void Assembler::RV_rdinstreth(Register rd) {
  RV_csrrs(rd, csr_instreth, zero_reg);
}
void Assembler::RV_rdcycle(Register rd) { RV_csrrs(rd, csr_cycle, zero_reg); }
void Assembler::RV_rdcycleh(Register rd) { RV_csrrs(rd, csr_cycleh, zero_reg); }
void Assembler::RV_rdtime(Register rd) { RV_csrrs(rd, csr_time, zero_reg); }
void Assembler::RV_rdtimeh(Register rd) { RV_csrrs(rd, csr_timeh, zero_reg); }

void Assembler::RV_csrr(Register rd, ControlStatusReg csr) {
  RV_csrrs(rd, csr, zero_reg);
}
void Assembler::RV_csrw(ControlStatusReg csr, Register rs) {
  RV_csrrw(zero_reg, csr, rs);
}
void Assembler::RV_csrs(ControlStatusReg csr, Register rs) {
  RV_csrrs(zero_reg, csr, rs);
}
void Assembler::RV_csrc(ControlStatusReg csr, Register rs) {
  RV_csrrc(zero_reg, csr, rs);
}

void Assembler::RV_csrwi(ControlStatusReg csr, uint8_t imm) {
  RV_csrrwi(zero_reg, csr, imm);
}
void Assembler::RV_csrsi(ControlStatusReg csr, uint8_t imm) {
  RV_csrrsi(zero_reg, csr, imm);
}
void Assembler::RV_csrci(ControlStatusReg csr, uint8_t imm) {
  RV_csrrci(zero_reg, csr, imm);
}

void Assembler::RV_frcsr(Register rd) { RV_csrrs(rd, csr_fcsr, zero_reg); }
void Assembler::RV_fscsr(Register rd, Register rs) {
  RV_csrrw(rd, csr_fcsr, rs);
}
void Assembler::RV_fscsr(Register rs) { RV_csrrw(zero_reg, csr_fcsr, rs); }

void Assembler::RV_frrm(Register rd) { RV_csrrs(rd, csr_frm, zero_reg); }
void Assembler::RV_fsrm(Register rd, Register rs) { RV_csrrw(rd, csr_frm, rs); }
void Assembler::RV_fsrm(Register rs) { RV_csrrw(zero_reg, csr_frm, rs); }

void Assembler::RV_frflags(Register rd) { RV_csrrs(rd, csr_fflags, zero_reg); }
void Assembler::RV_fsflags(Register rd, Register rs) {
  RV_csrrw(rd, csr_fflags, rs);
}
void Assembler::RV_fsflags(Register rs) { RV_csrrw(zero_reg, csr_fflags, rs); }

// Original MIPS Instructions

void Assembler::addu(Register rd, Register rs, Register rt) {
  RV_addw(rd, rs, rt);
}

void Assembler::addiu(Register rd, Register rs, int32_t j) {
  if (is_int12(j))
    RV_addiw(rd, rs, j);
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, j);
    RV_addw(rd, rs, scratch);
  }
}

void Assembler::subu(Register rd, Register rs, Register rt) {
  RV_subw(rd, rs, rt);
}

void Assembler::mul(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::muh(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::mulu(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::muhu(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::dmul(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::dmuh(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::dmulu(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::dmuhu(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::mult(Register rs, Register rt) { UNREACHABLE(); }

void Assembler::multu(Register rs, Register rt) { UNREACHABLE(); }

void Assembler::daddiu(Register rd, Register rs, int32_t j) {
  if (is_int12(j))
    RV_addi(rd, rs, j);
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, j);
    RV_add(rd, rs, scratch);
  }
}

void Assembler::div(Register rs, Register rt) { UNREACHABLE(); }

void Assembler::div(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::mod(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::divu(Register rs, Register rt) { UNREACHABLE(); }

void Assembler::divu(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::modu(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::daddu(Register rd, Register rs, Register rt) {
  RV_add(rd, rs, rt);
}

void Assembler::dsubu(Register rd, Register rs, Register rt) {
  RV_sub(rd, rs, rt);
}

void Assembler::dmult(Register rs, Register rt) { UNREACHABLE(); }

void Assembler::dmultu(Register rs, Register rt) { UNREACHABLE(); }

void Assembler::ddiv(Register rs, Register rt) { UNREACHABLE(); }

void Assembler::ddiv(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::dmod(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::ddivu(Register rs, Register rt) { UNREACHABLE(); }

void Assembler::ddivu(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::dmodu(Register rd, Register rs, Register rt) { UNREACHABLE(); }

// Logical.

void Assembler::and_(Register rd, Register rs, Register rt) {
  RV_and_(rd, rs, rt);
}

void Assembler::andi(Register rt, Register rs, int32_t j) {
  if (is_int12(j))
    RV_andi(rt, rs, j);
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, j);
    RV_and_(rt, rs, scratch);
  }
}

void Assembler::or_(Register rd, Register rs, Register rt) {
  RV_or_(rd, rs, rt);
}

void Assembler::ori(Register rt, Register rs, int32_t j) { UNREACHABLE(); }

void Assembler::xor_(Register rd, Register rs, Register rt) {
  RV_xor_(rd, rs, rt);
}

void Assembler::xori(Register rt, Register rs, int32_t j) {
  if (is_int12(j))
    RV_xori(rt, rs, j);
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, j);
    RV_xor_(rt, rs, scratch);
  }
}

void Assembler::nor(Register rd, Register rs, Register rt) {
  RV_or_(rd, rs, rt);
  RV_not(rd, rd);
}

// Shifts.
void Assembler::sll(Register rd, Register rt, uint16_t sa,
                    bool coming_from_nop) {
  RV_slliw(rd, rt, sa);
}

void Assembler::sllv(Register rd, Register rt, Register rs) {
  RV_sllw(rd, rt, rs);
}

void Assembler::srl(Register rd, Register rt, uint16_t sa) {
  RV_srliw(rd, rt, sa);
}

void Assembler::srlv(Register rd, Register rt, Register rs) {
  RV_srlw(rd, rt, rs);
}

void Assembler::sra(Register rd, Register rt, uint16_t sa) {
  RV_sraiw(rd, rt, sa & 0x1F);
}

void Assembler::srav(Register rd, Register rt, Register rs) {
  RV_sraw(rd, rt, rs);
}

void Assembler::rotr(Register rd, Register rt, uint16_t sa) { UNREACHABLE(); }

void Assembler::rotrv(Register rd, Register rt, Register rs) { UNREACHABLE(); }

void Assembler::dsll(Register rd, Register rt, uint16_t sa) {
  RV_slli(rd, rt, sa & 0x1F);
}

void Assembler::dsllv(Register rd, Register rt, Register rs) {
  RV_sll(rd, rt, rs);
}

void Assembler::dsrl(Register rd, Register rt, uint16_t sa) {
  RV_srli(rd, rt, sa & 0x1F);
}

void Assembler::dsrlv(Register rd, Register rt, Register rs) { UNREACHABLE(); }

void Assembler::drotr(Register rd, Register rt, uint16_t sa) { UNREACHABLE(); }

void Assembler::drotr32(Register rd, Register rt, uint16_t sa) {
  UNREACHABLE();
}

void Assembler::drotrv(Register rd, Register rt, Register rs) { UNREACHABLE(); }

void Assembler::dsra(Register rd, Register rt, uint16_t sa) {
  RV_srai(rd, rt, sa & 0x1F);
}

void Assembler::dsrav(Register rd, Register rt, Register rs) { UNREACHABLE(); }

void Assembler::dsll32(Register rd, Register rt, uint16_t sa) {
  RV_slli(rd, rt, 32 + (sa & 0x1F));
}

void Assembler::dsrl32(Register rd, Register rt, uint16_t sa) {
  RV_srli(rd, rt, 32 + (sa & 0x1F));
}

void Assembler::dsra32(Register rd, Register rt, uint16_t sa) {
  RV_srai(rd, rt, 32 + (sa & 0x1F));
}

void Assembler::lsa(Register rd, Register rt, Register rs, uint8_t sa) {
  UNREACHABLE();
}

void Assembler::dlsa(Register rd, Register rt, Register rs, uint8_t sa) {
  UNREACHABLE();
}

// ------------Memory-instructions-------------

bool Assembler::NeedAdjustBaseAndOffset(const MemOperand& src,
                                        OffsetAccessType access_type,
                                        int second_access_add_to_offset) {
  bool two_accesses = static_cast<bool>(access_type);
  DCHECK_LE(second_access_add_to_offset, 7);  // Must be <= 7.

  // is_int12 must be passed a signed value, hence the static cast below.
  if (is_int12(src.offset()) &&
      (!two_accesses || is_int12(static_cast<int32_t>(
                            src.offset() + second_access_add_to_offset)))) {
    // Nothing to do: 'offset' (and, if needed, 'offset + 4', or other specified
    // value) fits into int12.
    return false;
  }
  return true;
}

void Assembler::AdjustBaseAndOffset(MemOperand* src, Register scratch,
                                    OffsetAccessType access_type,
                                    int second_Access_add_to_offset) {
  // This method is used to adjust the base register and offset pair
  // for a load/store when the offset doesn't fit into int12.

  // Must not overwrite the register 'base' while loading 'offset'.
  DCHECK(src->rm() != scratch);

  // FIXME(RISC-V): There may be a more optimal way to do this
  RV_li(scratch, src->offset());
  RV_add(scratch, scratch, src->rm());
  src->offset_ = 0;
  src->rm_ = scratch;
}

void Assembler::lb(Register rd, const MemOperand& rs) {
  if (is_int12(rs.offset()))
    RV_lb(rd, rs.rm(), rs.offset());
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, rs.offset());
    RV_add(scratch, scratch, rs.rm());
    RV_lb(rd, scratch, 0);
  }
}

void Assembler::lbu(Register rd, const MemOperand& rs) {
  if (is_int12(rs.offset_))
    RV_lbu(rd, rs.rm(), rs.offset_);
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, rs.offset());
    RV_add(scratch, scratch, rs.rm());
    RV_lbu(rd, scratch, 0);
  }
}

void Assembler::lh(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::lhu(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::lw(Register rd, const MemOperand& rs) {
  if (is_int12(rs.offset()))
    RV_lw(rd, rs.rm(), rs.offset());
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, rs.offset());
    RV_add(scratch, scratch, rs.rm());
    RV_lw(rd, scratch, 0);
  }
}

void Assembler::lwu(Register rd, const MemOperand& rs) {
  if (is_int12(rs.offset_))
    RV_lwu(rd, rs.rm(), rs.offset_);
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, rs.offset());
    RV_add(scratch, scratch, rs.rm());
    RV_lwu(rd, scratch, 0);
  }
}

void Assembler::lwl(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::lwr(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::sb(Register rd, const MemOperand& rs) {
  if (is_int12(rs.offset()))
    RV_sb(rd, rs.rm(), rs.offset());
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, rs.offset());
    RV_add(scratch, scratch, rs.rm());
    RV_sb(rd, scratch, 0);
  }
}

void Assembler::sh(Register rd, const MemOperand& rs) {
  if (is_int12(rs.offset_))
    RV_sh(rd, rs.rm(), rs.offset_);
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, rs.offset());
    RV_add(scratch, scratch, rs.rm());
    RV_sh(rd, scratch, 0);
  }
}

void Assembler::sw(Register rd, const MemOperand& rs) {
  if (is_int12(rs.offset()))
    RV_sw(rd, rs.rm(), rs.offset());
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, rs.offset());
    RV_add(scratch, scratch, rs.rm());
    RV_sw(rd, scratch, 0);
  }
}

void Assembler::swl(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::swr(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::ll(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::lld(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::sc(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::scd(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::lui(Register rd, int32_t j) { UNREACHABLE(); }

void Assembler::aui(Register rt, Register rs, int32_t j) { UNREACHABLE(); }

void Assembler::daui(Register rt, Register rs, int32_t j) { UNREACHABLE(); }

void Assembler::dahi(Register rs, int32_t j) { UNREACHABLE(); }

void Assembler::dati(Register rs, int32_t j) { UNREACHABLE(); }

void Assembler::ldl(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::ldr(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::sdl(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::sdr(Register rd, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::ld(Register rd, const MemOperand& rs) {
  if (is_int12(rs.offset_))
    RV_ld(rd, rs.rm(), rs.offset_);
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, rs.offset());
    RV_add(scratch, scratch, rs.rm());
    RV_ld(rd, scratch, 0);
  }
}

void Assembler::sd(Register rd, const MemOperand& rs) {
  if (is_int12(rs.offset_))
    RV_sd(rd, rs.rm(), rs.offset_);
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, rs.offset());
    RV_add(scratch, scratch, rs.rm());
    RV_sd(rd, scratch, 0);
  }
}

// ---------PC-Relative instructions-----------

void Assembler::addiupc(Register rs, int32_t imm19) { UNREACHABLE(); }

void Assembler::lwpc(Register rs, int32_t offset19) { UNREACHABLE(); }

void Assembler::lwupc(Register rs, int32_t offset19) { UNREACHABLE(); }

void Assembler::ldpc(Register rs, int32_t offset18) { UNREACHABLE(); }

void Assembler::auipc(Register rs, int16_t imm16) { UNREACHABLE(); }

void Assembler::aluipc(Register rs, int16_t imm16) { UNREACHABLE(); }

// -------------Misc-instructions--------------

// Break / Trap instructions.
void Assembler::break_(uint32_t code, bool break_as_stop) {
  // FIXME(RISCV): There does not seem to be a standard for where to put this
  // `code`. It should probably go into a register, but this is not defined.
  RV_ebreak();
}

// FIXME (RISCV): may need to remove MIPS flags
void Assembler::stop(uint32_t code) {
  DCHECK_GT(code, kMaxWatchpointCode);
  DCHECK_LE(code, kMaxStopCode);
#if defined(V8_HOST_ARCH_MIPS) || defined(V8_HOST_ARCH_MIPS64)
  break_(0x54321);
#else  // V8_HOST_ARCH_MIPS
  break_(code, true);
#endif
}

void Assembler::tge(Register rs, Register rt, uint16_t code) { UNREACHABLE(); }

void Assembler::tgeu(Register rs, Register rt, uint16_t code) { UNREACHABLE(); }

void Assembler::tlt(Register rs, Register rt, uint16_t code) { UNREACHABLE(); }

void Assembler::tltu(Register rs, Register rt, uint16_t code) { UNREACHABLE(); }

void Assembler::teq(Register rs, Register rt, uint16_t code) { UNREACHABLE(); }

void Assembler::tne(Register rs, Register rt, uint16_t code) { UNREACHABLE(); }

void Assembler::sync() { RV_fence(0b1111, 0b1111); }

// Move from HI/LO register.

void Assembler::mfhi(Register rd) { UNREACHABLE(); }

void Assembler::mflo(Register rd) { UNREACHABLE(); }

// Set on less than instructions.
void Assembler::slt(Register rd, Register rs, Register rt) {
  DCHECK(rd != rt);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  RV_sext_w(rd, rs);
  RV_sext_w(scratch, rt);
  RV_slt(rd, rd, scratch);
}

void Assembler::sltu(Register rd, Register rs, Register rt) {
  DCHECK(rd != rt);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  RV_sext_w(rd, rs);
  RV_sext_w(scratch, rt);
  RV_sltu(rd, rd, scratch);
}

void Assembler::slti(Register rt, Register rs, int32_t j) { UNREACHABLE(); }

void Assembler::sltiu(Register rd, Register rs, int32_t j) {
  RV_sext_w(rd, rs);
  if (is_int12(j)) {
    RV_sltiu(rd, rd, j);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    RV_li(scratch, j);
    RV_sltu(rd, rd, scratch);
  }
}

// Conditional move.
void Assembler::movz(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::movn(Register rd, Register rs, Register rt) { UNREACHABLE(); }

void Assembler::movt(Register rd, Register rs, uint16_t cc) { UNREACHABLE(); }

// FIXME (RISCV): to be ported
void Assembler::movf(Register rd, Register rs, uint16_t cc) {
  printf("ERROR: movf generated\n");
  Register rt = Register::from_code((cc & 0x0007) << 2 | 0);
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, MOVCI);
}

void Assembler::min_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  RV_fmin_s(fd, fs, ft);
}

void Assembler::min_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  RV_fmin_d(fd, fs, ft);
}

void Assembler::max_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  RV_fmax_s(fd, fs, ft);
}

void Assembler::max_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  RV_fmax_d(fd, fs, ft);
}

void Assembler::mina_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  mina(S, fd, fs, ft);
}

void Assembler::mina_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  mina(D, fd, fs, ft);
}

void Assembler::maxa_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  maxa(S, fd, fs, ft);
}

void Assembler::maxa_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  maxa(D, fd, fs, ft);
}

// GPR.
void Assembler::seleqz(Register rd, Register rs, Register rt) { UNREACHABLE(); }

// GPR.
void Assembler::selnez(Register rd, Register rs, Register rt) {
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
  RV_snez(scratch, rt);
  RV_neg(scratch, scratch);  // if rt == 0, scratch = 0; else, scratch = -1
  RV_and_(rd, rs, scratch);  // if rt == 0, rd = 0; else rd = rs
}

// Bit twiddling.
void Assembler::clz(Register rd, Register rs) { UNREACHABLE(); }

void Assembler::dclz(Register rd, Register rs) { UNREACHABLE(); }

void Assembler::ins_(Register rt, Register rs, uint16_t pos, uint16_t size) {
  UNREACHABLE();
}

void Assembler::dins_(Register rt, Register rs, uint16_t pos, uint16_t size) {
  UNREACHABLE();
}

void Assembler::dinsm_(Register rt, Register rs, uint16_t pos, uint16_t size) {
  UNREACHABLE();
}

void Assembler::dinsu_(Register rt, Register rs, uint16_t pos, uint16_t size) {
  UNREACHABLE();
}

void Assembler::ext_(Register rt, Register rs, uint16_t pos, uint16_t size) {
  UNREACHABLE();
}

void Assembler::dext_(Register rt, Register rs, uint16_t pos, uint16_t size) {
  UNREACHABLE();
}

void Assembler::dextm_(Register rt, Register rs, uint16_t pos, uint16_t size) {
  UNREACHABLE();
}

void Assembler::dextu_(Register rt, Register rs, uint16_t pos, uint16_t size) {
  UNREACHABLE();
}

void Assembler::bitswap(Register rd, Register rt) { UNREACHABLE(); }

void Assembler::dbitswap(Register rd, Register rt) { UNREACHABLE(); }

void Assembler::pref(int32_t hint, const MemOperand& rs) { UNREACHABLE(); }

void Assembler::align(Register rd, Register rs, Register rt, uint8_t bp) {
  UNREACHABLE();
}

void Assembler::dalign(Register rd, Register rs, Register rt, uint8_t bp) {
  UNREACHABLE();
}

void Assembler::wsbh(Register rd, Register rt) { UNREACHABLE(); }

void Assembler::dsbh(Register rd, Register rt) { UNREACHABLE(); }

void Assembler::dshd(Register rd, Register rt) { UNREACHABLE(); }

void Assembler::seh(Register rd, Register rt) {
  RV_slli(rd, rt, 64 - 16);
  RV_srai(rd, rd, 64 - 16);
}

void Assembler::seb(Register rd, Register rt) {
  RV_slli(rd, rt, 64 - 8);
  RV_srai(rd, rd, 64 - 8);
}

// --------Coprocessor-instructions----------------

// Load, store, move.
void Assembler::lwc1(FPURegister fd, const MemOperand& src) {
  if (is_int12(src.offset()))
    RV_flw(fd, src.rm(), src.offset());
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, src.offset());
    RV_add(scratch, scratch, src.rm());
    RV_flw(fd, scratch, 0);
  }
}

void Assembler::ldc1(FPURegister fd, const MemOperand& src) { UNREACHABLE(); }

void Assembler::swc1(FPURegister fs, const MemOperand& src) {
  if (is_int12(src.offset()))
    RV_fsw(fs, src.rm(), src.offset());
  else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t5;
    RV_li(scratch, src.offset());
    RV_add(scratch, scratch, src.rm());
    RV_fsw(fs, scratch, 0);
  }
}

void Assembler::sdc1(FPURegister fs, const MemOperand& src) { UNREACHABLE(); }

void Assembler::mtc1(Register rt, FPURegister fs) { RV_fmv_w_x(fs, rt); }

// FIXME (RISCV): need to be ported
void Assembler::mthc1(Register rt, FPURegister fs) {
  printf("ERROR: mthc1 generated\n");
  GenInstrRegister(COP1, MTHC1, rt, fs, fa0);
}

void Assembler::dmtc1(Register rt, FPURegister fs) { RV_fmv_d_x(fs, rt); }

void Assembler::mfc1(Register rt, FPURegister fs) { RV_fmv_x_w(rt, fs); }

// FIXME (RISCV): need to be ported
void Assembler::mfhc1(Register rt, FPURegister fs) {
  printf("ERROR: mfhc1 generated\n");
  GenInstrRegister(COP1, MFHC1, rt, fs, fa0);
}

void Assembler::dmfc1(Register rt, FPURegister fs) { RV_fmv_x_d(rt, fs); }

// FIXME (RISCV): to be ported
void Assembler::ctc1(Register rt, FPUControlRegister fs) {
  printf("ERROR: ctc1 generated\n");
  GenInstrRegister(COP1, CTC1, rt, fs);
}

// FIXME (RISCV): to be ported
void Assembler::cfc1(Register rt, FPUControlRegister fs) {
  printf("ERROR: cfc1 generated\n");
  GenInstrRegister(COP1, CFC1, rt, fs);
}

void Assembler::sel(SecondaryField fmt, FPURegister fd, FPURegister fs,
                    FPURegister ft) {
  UNREACHABLE();
}

void Assembler::sel_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  sel(S, fd, fs, ft);
}

void Assembler::sel_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  sel(D, fd, fs, ft);
}

// FPR.
void Assembler::seleqz(SecondaryField fmt, FPURegister fd, FPURegister fs,
                       FPURegister ft) {
  UNREACHABLE();
}

void Assembler::seleqz_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  seleqz(D, fd, fs, ft);
}

void Assembler::seleqz_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  seleqz(S, fd, fs, ft);
}

void Assembler::selnez_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  selnez(D, fd, fs, ft);
}

void Assembler::selnez_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  selnez(S, fd, fs, ft);
}

void Assembler::movz_s(FPURegister fd, FPURegister fs, Register rt) {
  UNREACHABLE();
}

void Assembler::movz_d(FPURegister fd, FPURegister fs, Register rt) {
  UNREACHABLE();
}

void Assembler::movt_s(FPURegister fd, FPURegister fs, uint16_t cc) {
  UNREACHABLE();
}

void Assembler::movt_d(FPURegister fd, FPURegister fs, uint16_t cc) {
  UNREACHABLE();
}

void Assembler::movf_s(FPURegister fd, FPURegister fs, uint16_t cc) {
  UNREACHABLE();
}

void Assembler::movf_d(FPURegister fd, FPURegister fs, uint16_t cc) {
  UNREACHABLE();
}

void Assembler::movn_s(FPURegister fd, FPURegister fs, Register rt) {
  UNREACHABLE();
}

void Assembler::movn_d(FPURegister fd, FPURegister fs, Register rt) {
  UNREACHABLE();
}

// FPR.
void Assembler::selnez(SecondaryField fmt, FPURegister fd, FPURegister fs,
                       FPURegister ft) {
  UNREACHABLE();
}

// Arithmetic.

void Assembler::add_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  RV_fadd_s(fd, fs, ft);
}

void Assembler::add_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  RV_fadd_d(fd, fs, ft);
}

void Assembler::sub_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  RV_fsub_s(fd, fs, ft);
}

void Assembler::sub_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  RV_fsub_d(fd, fs, ft);
}

void Assembler::mul_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  RV_fmul_s(fd, fs, ft);
}

void Assembler::mul_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  RV_fmul_d(fd, fs, ft);
}

void Assembler::madd_s(FPURegister fd, FPURegister fr, FPURegister fs,
                       FPURegister ft) {
  // On Loongson 3A (MIPS64R2), MADD.S instruction is actually fused MADD.S and
  // this causes failure in some of the tests. Since this optimization is rarely
  // used, and not used at all on MIPS64R6, this isntruction is removed.
  UNREACHABLE();
}

void Assembler::madd_d(FPURegister fd, FPURegister fr, FPURegister fs,
                       FPURegister ft) {
  // On Loongson 3A (MIPS64R2), MADD.D instruction is actually fused MADD.D and
  // this causes failure in some of the tests. Since this optimization is rarely
  // used, and not used at all on MIPS64R6, this isntruction is removed.
  UNREACHABLE();
}

void Assembler::msub_s(FPURegister fd, FPURegister fr, FPURegister fs,
                       FPURegister ft) {
  // See explanation for instruction madd_s.
  UNREACHABLE();
}

void Assembler::msub_d(FPURegister fd, FPURegister fr, FPURegister fs,
                       FPURegister ft) {
  // See explanation for instruction madd_d.
  UNREACHABLE();
}

void Assembler::maddf_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  UNREACHABLE();
}

void Assembler::maddf_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  UNREACHABLE();
}

void Assembler::msubf_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  UNREACHABLE();
}

void Assembler::msubf_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  UNREACHABLE();
}

void Assembler::div_s(FPURegister fd, FPURegister fs, FPURegister ft) {
  RV_fdiv_s(fd, fs, ft);
}

void Assembler::div_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  RV_fdiv_d(fd, fs, ft);
}

void Assembler::abs_s(FPURegister fd, FPURegister fs) { RV_fabs_s(fd, fs); }

void Assembler::abs_d(FPURegister fd, FPURegister fs) { RV_fabs_d(fd, fs); }

void Assembler::mov_d(FPURegister fd, FPURegister fs) { RV_fmv_d(fd, fs); }

void Assembler::mov_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::neg_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::neg_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::sqrt_s(FPURegister fd, FPURegister fs) { RV_fsqrt_s(fd, fs); }

void Assembler::sqrt_d(FPURegister fd, FPURegister fs) { RV_fsqrt_d(fd, fs); }

void Assembler::rsqrt_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::rsqrt_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::recip_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::recip_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

// Conversions.
void Assembler::cvt_w_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::cvt_w_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::trunc_w_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

// FIXME (RISCV): to be ported
void Assembler::trunc_w_d(FPURegister fd, FPURegister fs) {
  printf("ERROR: trunc_w_d generated\n");
  GenInstrRegister(COP1, D, fa0, fs, fd, TRUNC_W_D);
}

void Assembler::round_w_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::round_w_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::floor_w_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::floor_w_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::ceil_w_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::ceil_w_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::rint_s(FPURegister fd, FPURegister fs) { rint(S, fd, fs); }

void Assembler::rint_d(FPURegister fd, FPURegister fs) { rint(D, fd, fs); }

void Assembler::rint(SecondaryField fmt, FPURegister fd, FPURegister fs) {
  UNREACHABLE();
}

void Assembler::cvt_l_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::cvt_l_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::trunc_l_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::trunc_l_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::round_l_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::round_l_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::floor_l_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::floor_l_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::ceil_l_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::ceil_l_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::class_s(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::class_d(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::mina(SecondaryField fmt, FPURegister fd, FPURegister fs,
                     FPURegister ft) {
  UNREACHABLE();
}

void Assembler::maxa(SecondaryField fmt, FPURegister fd, FPURegister fs,
                     FPURegister ft) {
  UNREACHABLE();
}

void Assembler::cvt_s_w(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::cvt_s_l(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::cvt_s_d(FPURegister fd, FPURegister fs) { RV_fcvt_s_d(fd, fs); }

void Assembler::cvt_d_w(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::cvt_d_l(FPURegister fd, FPURegister fs) { UNREACHABLE(); }

void Assembler::cvt_d_s(FPURegister fd, FPURegister fs) { RV_fcvt_d_s(fd, fs); }

// Conditions for >= MIPSr6.
void Assembler::cmp(FPUCondition cond, SecondaryField fmt, FPURegister fd,
                    FPURegister fs, FPURegister ft) {
  UNREACHABLE();
}

void Assembler::cmp_s(FPUCondition cond, FPURegister fd, FPURegister fs,
                      FPURegister ft) {
  cmp(cond, W, fd, fs, ft);
}

void Assembler::cmp_d(FPUCondition cond, FPURegister fd, FPURegister fs,
                      FPURegister ft) {
  cmp(cond, L, fd, fs, ft);
}

void Assembler::bc1eqz(int16_t offset, FPURegister ft) { UNREACHABLE(); }

void Assembler::bc1nez(int16_t offset, FPURegister ft) { UNREACHABLE(); }

// Conditions for < MIPSr6.

// FIXME (RISCV): to be ported
void Assembler::c(FPUCondition cond, SecondaryField fmt, FPURegister fs,
                  FPURegister ft, uint16_t cc) {
  printf("ERROR: c generated\n");

  DCHECK_NE(kArchVariant, kMips64r6);
  DCHECK(is_uint3(cc));
  DCHECK(fmt == S || fmt == D);
  DCHECK_EQ(fmt & ~(31 << kRsShift), 0);
  Instr instr = COP1 | fmt | ft.code() << kFtShift | fs.code() << kFsShift |
                cc << 8 | 3 << 4 | cond;
  emit(instr);
}

void Assembler::c_s(FPUCondition cond, FPURegister fs, FPURegister ft,
                    uint16_t cc) {
  c(cond, S, fs, ft, cc);
}

void Assembler::c_d(FPUCondition cond, FPURegister fs, FPURegister ft,
                    uint16_t cc) {
  c(cond, D, fs, ft, cc);
}

void Assembler::fcmp(FPURegister src1, const double src2, FPUCondition cond) {
  UNREACHABLE();
}

void Assembler::bc1f(int16_t offset, uint16_t cc) { UNREACHABLE(); }

void Assembler::bc1t(int16_t offset, uint16_t cc) { UNREACHABLE(); }

// ---------- MSA instructions ------------
#define MSA_BRANCH_LIST(V) \
  V(bz_v, BZ_V)            \
  V(bz_b, BZ_B)            \
  V(bz_h, BZ_H)            \
  V(bz_w, BZ_W)            \
  V(bz_d, BZ_D)            \
  V(bnz_v, BNZ_V)          \
  V(bnz_b, BNZ_B)          \
  V(bnz_h, BNZ_H)          \
  V(bnz_w, BNZ_W)          \
  V(bnz_d, BNZ_D)

#define MSA_BRANCH(name, opcode) \
  void Assembler::name(MSARegister wt, int16_t offset) { UNREACHABLE(); }

MSA_BRANCH_LIST(MSA_BRANCH)
#undef MSA_BRANCH
#undef MSA_BRANCH_LIST

#define MSA_LD_ST_LIST(V) \
  V(ld_b, LD_B)           \
  V(ld_h, LD_H)           \
  V(ld_w, LD_W)           \
  V(ld_d, LD_D)           \
  V(st_b, ST_B)           \
  V(st_h, ST_H)           \
  V(st_w, ST_W)           \
  V(st_d, ST_D)

#define MSA_LD_ST(name, opcode) \
  void Assembler::name(MSARegister wd, const MemOperand& rs) { UNREACHABLE(); }

MSA_LD_ST_LIST(MSA_LD_ST)
#undef MSA_LD_ST
#undef MSA_LD_ST_LIST

#define MSA_I10_LIST(V) \
  V(ldi_b, I5_DF_b)     \
  V(ldi_h, I5_DF_h)     \
  V(ldi_w, I5_DF_w)     \
  V(ldi_d, I5_DF_d)

#define MSA_I10(name, format) \
  void Assembler::name(MSARegister wd, int32_t imm10) { UNREACHABLE(); }
MSA_I10_LIST(MSA_I10)
#undef MSA_I10
#undef MSA_I10_LIST

#define MSA_I5_LIST(V) \
  V(addvi, ADDVI)      \
  V(subvi, SUBVI)      \
  V(maxi_s, MAXI_S)    \
  V(maxi_u, MAXI_U)    \
  V(mini_s, MINI_S)    \
  V(mini_u, MINI_U)    \
  V(ceqi, CEQI)        \
  V(clti_s, CLTI_S)    \
  V(clti_u, CLTI_U)    \
  V(clei_s, CLEI_S)    \
  V(clei_u, CLEI_U)

#define MSA_I5_FORMAT(name, opcode, format)                       \
  void Assembler::name##_##format(MSARegister wd, MSARegister ws, \
                                  uint32_t imm5) {                \
    UNREACHABLE();                                                \
  }

#define MSA_I5(name, opcode)     \
  MSA_I5_FORMAT(name, opcode, b) \
  MSA_I5_FORMAT(name, opcode, h) \
  MSA_I5_FORMAT(name, opcode, w) \
  MSA_I5_FORMAT(name, opcode, d)

MSA_I5_LIST(MSA_I5)
#undef MSA_I5
#undef MSA_I5_FORMAT
#undef MSA_I5_LIST

#define MSA_I8_LIST(V) \
  V(andi_b, ANDI_B)    \
  V(ori_b, ORI_B)      \
  V(nori_b, NORI_B)    \
  V(xori_b, XORI_B)    \
  V(bmnzi_b, BMNZI_B)  \
  V(bmzi_b, BMZI_B)    \
  V(bseli_b, BSELI_B)  \
  V(shf_b, SHF_B)      \
  V(shf_h, SHF_H)      \
  V(shf_w, SHF_W)

#define MSA_I8(name, opcode)                                            \
  void Assembler::name(MSARegister wd, MSARegister ws, uint32_t imm8) { \
    UNREACHABLE();                                                      \
  }

MSA_I8_LIST(MSA_I8)
#undef MSA_I8
#undef MSA_I8_LIST

#define MSA_VEC_LIST(V) \
  V(and_v, AND_V)       \
  V(or_v, OR_V)         \
  V(nor_v, NOR_V)       \
  V(xor_v, XOR_V)       \
  V(bmnz_v, BMNZ_V)     \
  V(bmz_v, BMZ_V)       \
  V(bsel_v, BSEL_V)

#define MSA_VEC(name, opcode)                                            \
  void Assembler::name(MSARegister wd, MSARegister ws, MSARegister wt) { \
    UNREACHABLE();                                                       \
  }

MSA_VEC_LIST(MSA_VEC)
#undef MSA_VEC
#undef MSA_VEC_LIST

#define MSA_2R_LIST(V) \
  V(pcnt, PCNT)        \
  V(nloc, NLOC)        \
  V(nlzc, NLZC)

#define MSA_2R_FORMAT(name, opcode, format)                         \
  void Assembler::name##_##format(MSARegister wd, MSARegister ws) { \
    UNREACHABLE();                                                  \
  }

#define MSA_2R(name, opcode)     \
  MSA_2R_FORMAT(name, opcode, b) \
  MSA_2R_FORMAT(name, opcode, h) \
  MSA_2R_FORMAT(name, opcode, w) \
  MSA_2R_FORMAT(name, opcode, d)

MSA_2R_LIST(MSA_2R)
#undef MSA_2R
#undef MSA_2R_FORMAT
#undef MSA_2R_LIST

#define MSA_FILL(format) \
  void Assembler::fill_##format(MSARegister wd, Register rs) { UNREACHABLE(); }

MSA_FILL(b)
MSA_FILL(h)
MSA_FILL(w)
MSA_FILL(d)
#undef MSA_FILL

#define MSA_2RF_LIST(V) \
  V(fclass, FCLASS)     \
  V(ftrunc_s, FTRUNC_S) \
  V(ftrunc_u, FTRUNC_U) \
  V(fsqrt, FSQRT)       \
  V(frsqrt, FRSQRT)     \
  V(frcp, FRCP)         \
  V(frint, FRINT)       \
  V(flog2, FLOG2)       \
  V(fexupl, FEXUPL)     \
  V(fexupr, FEXUPR)     \
  V(ffql, FFQL)         \
  V(ffqr, FFQR)         \
  V(ftint_s, FTINT_S)   \
  V(ftint_u, FTINT_U)   \
  V(ffint_s, FFINT_S)   \
  V(ffint_u, FFINT_U)

#define MSA_2RF_FORMAT(name, opcode, format)                        \
  void Assembler::name##_##format(MSARegister wd, MSARegister ws) { \
    UNREACHABLE();                                                  \
  }

#define MSA_2RF(name, opcode)     \
  MSA_2RF_FORMAT(name, opcode, w) \
  MSA_2RF_FORMAT(name, opcode, d)

MSA_2RF_LIST(MSA_2RF)
#undef MSA_2RF
#undef MSA_2RF_FORMAT
#undef MSA_2RF_LIST

#define MSA_3R_LIST(V)  \
  V(sll, SLL_MSA)       \
  V(sra, SRA_MSA)       \
  V(srl, SRL_MSA)       \
  V(bclr, BCLR)         \
  V(bset, BSET)         \
  V(bneg, BNEG)         \
  V(binsl, BINSL)       \
  V(binsr, BINSR)       \
  V(addv, ADDV)         \
  V(subv, SUBV)         \
  V(max_s, MAX_S)       \
  V(max_u, MAX_U)       \
  V(min_s, MIN_S)       \
  V(min_u, MIN_U)       \
  V(max_a, MAX_A)       \
  V(min_a, MIN_A)       \
  V(ceq, CEQ)           \
  V(clt_s, CLT_S)       \
  V(clt_u, CLT_U)       \
  V(cle_s, CLE_S)       \
  V(cle_u, CLE_U)       \
  V(add_a, ADD_A)       \
  V(adds_a, ADDS_A)     \
  V(adds_s, ADDS_S)     \
  V(adds_u, ADDS_U)     \
  V(ave_s, AVE_S)       \
  V(ave_u, AVE_U)       \
  V(aver_s, AVER_S)     \
  V(aver_u, AVER_U)     \
  V(subs_s, SUBS_S)     \
  V(subs_u, SUBS_U)     \
  V(subsus_u, SUBSUS_U) \
  V(subsuu_s, SUBSUU_S) \
  V(asub_s, ASUB_S)     \
  V(asub_u, ASUB_U)     \
  V(mulv, MULV)         \
  V(maddv, MADDV)       \
  V(msubv, MSUBV)       \
  V(div_s, DIV_S_MSA)   \
  V(div_u, DIV_U)       \
  V(mod_s, MOD_S)       \
  V(mod_u, MOD_U)       \
  V(dotp_s, DOTP_S)     \
  V(dotp_u, DOTP_U)     \
  V(dpadd_s, DPADD_S)   \
  V(dpadd_u, DPADD_U)   \
  V(dpsub_s, DPSUB_S)   \
  V(dpsub_u, DPSUB_U)   \
  V(pckev, PCKEV)       \
  V(pckod, PCKOD)       \
  V(ilvl, ILVL)         \
  V(ilvr, ILVR)         \
  V(ilvev, ILVEV)       \
  V(ilvod, ILVOD)       \
  V(vshf, VSHF)         \
  V(srar, SRAR)         \
  V(srlr, SRLR)         \
  V(hadd_s, HADD_S)     \
  V(hadd_u, HADD_U)     \
  V(hsub_s, HSUB_S)     \
  V(hsub_u, HSUB_U)

#define MSA_3R_FORMAT(name, opcode, format)                       \
  void Assembler::name##_##format(MSARegister wd, MSARegister ws, \
                                  MSARegister wt) {               \
    UNREACHABLE();                                                \
  }

#define MSA_3R_FORMAT_SLD_SPLAT(name, opcode, format)             \
  void Assembler::name##_##format(MSARegister wd, MSARegister ws, \
                                  Register rt) {                  \
    UNREACHABLE();                                                \
  }

#define MSA_3R(name, opcode)     \
  MSA_3R_FORMAT(name, opcode, b) \
  MSA_3R_FORMAT(name, opcode, h) \
  MSA_3R_FORMAT(name, opcode, w) \
  MSA_3R_FORMAT(name, opcode, d)

#define MSA_3R_SLD_SPLAT(name, opcode)     \
  MSA_3R_FORMAT_SLD_SPLAT(name, opcode, b) \
  MSA_3R_FORMAT_SLD_SPLAT(name, opcode, h) \
  MSA_3R_FORMAT_SLD_SPLAT(name, opcode, w) \
  MSA_3R_FORMAT_SLD_SPLAT(name, opcode, d)

MSA_3R_LIST(MSA_3R)
MSA_3R_SLD_SPLAT(sld, SLD)
MSA_3R_SLD_SPLAT(splat, SPLAT)

#undef MSA_3R
#undef MSA_3R_FORMAT
#undef MSA_3R_FORMAT_SLD_SPLAT
#undef MSA_3R_SLD_SPLAT
#undef MSA_3R_LIST

#define MSA_3RF_LIST1(V) \
  V(fcaf, FCAF)          \
  V(fcun, FCUN)          \
  V(fceq, FCEQ)          \
  V(fcueq, FCUEQ)        \
  V(fclt, FCLT)          \
  V(fcult, FCULT)        \
  V(fcle, FCLE)          \
  V(fcule, FCULE)        \
  V(fsaf, FSAF)          \
  V(fsun, FSUN)          \
  V(fseq, FSEQ)          \
  V(fsueq, FSUEQ)        \
  V(fslt, FSLT)          \
  V(fsult, FSULT)        \
  V(fsle, FSLE)          \
  V(fsule, FSULE)        \
  V(fadd, FADD)          \
  V(fsub, FSUB)          \
  V(fmul, FMUL)          \
  V(fdiv, FDIV)          \
  V(fmadd, FMADD)        \
  V(fmsub, FMSUB)        \
  V(fexp2, FEXP2)        \
  V(fmin, FMIN)          \
  V(fmin_a, FMIN_A)      \
  V(fmax, FMAX)          \
  V(fmax_a, FMAX_A)      \
  V(fcor, FCOR)          \
  V(fcune, FCUNE)        \
  V(fcne, FCNE)          \
  V(fsor, FSOR)          \
  V(fsune, FSUNE)        \
  V(fsne, FSNE)

#define MSA_3RF_LIST2(V) \
  V(fexdo, FEXDO)        \
  V(ftq, FTQ)            \
  V(mul_q, MUL_Q)        \
  V(madd_q, MADD_Q)      \
  V(msub_q, MSUB_Q)      \
  V(mulr_q, MULR_Q)      \
  V(maddr_q, MADDR_Q)    \
  V(msubr_q, MSUBR_Q)

#define MSA_3RF_FORMAT(name, opcode, df, df_c)                \
  void Assembler::name##_##df(MSARegister wd, MSARegister ws, \
                              MSARegister wt) {               \
    UNREACHABLE();                                            \
  }

#define MSA_3RF_1(name, opcode)      \
  MSA_3RF_FORMAT(name, opcode, w, 0) \
  MSA_3RF_FORMAT(name, opcode, d, 1)

#define MSA_3RF_2(name, opcode)      \
  MSA_3RF_FORMAT(name, opcode, h, 0) \
  MSA_3RF_FORMAT(name, opcode, w, 1)

MSA_3RF_LIST1(MSA_3RF_1)
MSA_3RF_LIST2(MSA_3RF_2)
#undef MSA_3RF_1
#undef MSA_3RF_2
#undef MSA_3RF_FORMAT
#undef MSA_3RF_LIST1
#undef MSA_3RF_LIST2

void Assembler::sldi_b(MSARegister wd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::sldi_h(MSARegister wd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::sldi_w(MSARegister wd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::sldi_d(MSARegister wd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::splati_b(MSARegister wd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::splati_h(MSARegister wd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::splati_w(MSARegister wd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::splati_d(MSARegister wd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::copy_s_b(Register rd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::copy_s_h(Register rd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::copy_s_w(Register rd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::copy_s_d(Register rd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::copy_u_b(Register rd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::copy_u_h(Register rd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::copy_u_w(Register rd, MSARegister ws, uint32_t n) {
  UNREACHABLE();
}

void Assembler::insert_b(MSARegister wd, uint32_t n, Register rs) {
  UNREACHABLE();
}

void Assembler::insert_h(MSARegister wd, uint32_t n, Register rs) {
  UNREACHABLE();
}

void Assembler::insert_w(MSARegister wd, uint32_t n, Register rs) {
  UNREACHABLE();
}

void Assembler::insert_d(MSARegister wd, uint32_t n, Register rs) {
  UNREACHABLE();
}

void Assembler::insve_b(MSARegister wd, uint32_t n, MSARegister ws) {
  UNREACHABLE();
}

void Assembler::insve_h(MSARegister wd, uint32_t n, MSARegister ws) {
  UNREACHABLE();
}

void Assembler::insve_w(MSARegister wd, uint32_t n, MSARegister ws) {
  UNREACHABLE();
}

void Assembler::insve_d(MSARegister wd, uint32_t n, MSARegister ws) {
  UNREACHABLE();
}

void Assembler::move_v(MSARegister wd, MSARegister ws) { UNREACHABLE(); }

void Assembler::ctcmsa(MSAControlRegister cd, Register rs) { UNREACHABLE(); }

void Assembler::cfcmsa(Register rd, MSAControlRegister cs) { UNREACHABLE(); }

#define MSA_BIT_LIST(V) \
  V(slli, SLLI)         \
  V(srai, SRAI)         \
  V(srli, SRLI)         \
  V(bclri, BCLRI)       \
  V(bseti, BSETI)       \
  V(bnegi, BNEGI)       \
  V(binsli, BINSLI)     \
  V(binsri, BINSRI)     \
  V(sat_s, SAT_S)       \
  V(sat_u, SAT_U)       \
  V(srari, SRARI)       \
  V(srlri, SRLRI)

#define MSA_BIT_FORMAT(name, opcode, format)                      \
  void Assembler::name##_##format(MSARegister wd, MSARegister ws, \
                                  uint32_t m) {                   \
    UNREACHABLE();                                                \
  }

#define MSA_BIT(name, opcode)     \
  MSA_BIT_FORMAT(name, opcode, b) \
  MSA_BIT_FORMAT(name, opcode, h) \
  MSA_BIT_FORMAT(name, opcode, w) \
  MSA_BIT_FORMAT(name, opcode, d)

MSA_BIT_LIST(MSA_BIT)
#undef MSA_BIT
#undef MSA_BIT_FORMAT
#undef MSA_BIT_LIST

int Assembler::RelocateInternalReference(RelocInfo::Mode rmode, Address pc,
                                         intptr_t pc_delta) {
  if (RelocInfo::IsInternalReference(rmode)) {
    int64_t* p = reinterpret_cast<int64_t*>(pc);
    if (*p == kEndOfJumpChain) {
      return 0;  // Number of instructions patched.
    }
    *p += pc_delta;
    return 2;  // Number of instructions patched.
  }
  Instr instr = instr_at(pc);
  DCHECK(RelocInfo::IsInternalReferenceEncoded(rmode));
  if (IsLui(instr)) {
    Instr instr_lui = instr_at(pc + 0 * kInstrSize);
    Instr instr_ori = instr_at(pc + 1 * kInstrSize);
    Instr instr_ori2 = instr_at(pc + 3 * kInstrSize);
    DCHECK(IsOri(instr_ori));
    DCHECK(IsOri(instr_ori2));
    // TODO(plind): symbolic names for the shifts.
    int64_t imm = (instr_lui & static_cast<int64_t>(kImm16Mask)) << 48;
    imm |= (instr_ori & static_cast<int64_t>(kImm16Mask)) << 32;
    imm |= (instr_ori2 & static_cast<int64_t>(kImm16Mask)) << 16;
    // Sign extend address.
    imm >>= 16;

    if (imm == kEndOfJumpChain) {
      return 0;  // Number of instructions patched.
    }
    imm += pc_delta;
    DCHECK_EQ(imm & 3, 0);

    instr_lui &= ~kImm16Mask;
    instr_ori &= ~kImm16Mask;
    instr_ori2 &= ~kImm16Mask;

    instr_at_put(pc + 0 * kInstrSize, instr_lui | ((imm >> 32) & kImm16Mask));
    instr_at_put(pc + 1 * kInstrSize, instr_ori | (imm >> 16 & kImm16Mask));
    instr_at_put(pc + 3 * kInstrSize, instr_ori2 | (imm & kImm16Mask));
    return 4;  // Number of instructions patched.
  } else if (IsJ(instr) || IsJal(instr)) {
    // Regular j/jal relocation.
    uint32_t imm28 = (instr & static_cast<int32_t>(kImm26Mask)) << 2;
    imm28 += pc_delta;
    imm28 &= kImm28Mask;
    instr &= ~kImm26Mask;
    DCHECK_EQ(imm28 & 3, 0);
    uint32_t imm26 = static_cast<uint32_t>(imm28 >> 2);
    instr_at_put(pc, instr | (imm26 & kImm26Mask));
    return 1;  // Number of instructions patched.
  } else {
    DCHECK(((instr & kJumpRawMask) == kJRawMark) ||
           ((instr & kJumpRawMask) == kJalRawMark));
    // Unbox raw offset and emit j/jal.
    int32_t imm28 = (instr & static_cast<int32_t>(kImm26Mask)) << 2;
    // Sign extend 28-bit offset to 32-bit.
    imm28 = (imm28 << 4) >> 4;
    uint64_t target =
        static_cast<int64_t>(imm28) + reinterpret_cast<uint64_t>(pc);
    target &= kImm28Mask;
    DCHECK_EQ(imm28 & 3, 0);
    uint32_t imm26 = static_cast<uint32_t>(target >> 2);
    // Check markings whether to emit j or jal.
    uint32_t unbox = (instr & kJRawMark) ? J : JAL;
    instr_at_put(pc, unbox | (imm26 & kImm26Mask));
    return 1;  // Number of instructions patched.
  }
}

void Assembler::GrowBuffer() {
  DEBUG_PRINTF("GrowBuffer: %p -> ", buffer_start_);
  // Compute new buffer size.
  int old_size = buffer_->size();
  int new_size = std::min(2 * old_size, old_size + 1 * MB);

  // Some internal data structures overflow for very large buffers,
  // they must ensure that kMaximalBufferSize is not too large.
  if (new_size > kMaximalBufferSize) {
    V8::FatalProcessOutOfMemory(nullptr, "Assembler::GrowBuffer");
  }

  // Set up new buffer.
  std::unique_ptr<AssemblerBuffer> new_buffer = buffer_->Grow(new_size);
  DCHECK_EQ(new_size, new_buffer->size());
  byte* new_start = new_buffer->start();

  // Copy the data.
  intptr_t pc_delta = new_start - buffer_start_;
  intptr_t rc_delta = (new_start + new_size) - (buffer_start_ + old_size);
  size_t reloc_size = (buffer_start_ + old_size) - reloc_info_writer.pos();
  MemMove(new_start, buffer_start_, pc_offset());
  MemMove(reloc_info_writer.pos() + rc_delta, reloc_info_writer.pos(),
          reloc_size);

  // Switch buffers.
  buffer_ = std::move(new_buffer);
  buffer_start_ = new_start;
  DEBUG_PRINTF("%p\n", buffer_start_);
  pc_ += pc_delta;
  reloc_info_writer.Reposition(reloc_info_writer.pos() + rc_delta,
                               reloc_info_writer.last_pc() + pc_delta);

  // Relocate runtime entries.
  Vector<byte> instructions{buffer_start_, pc_offset()};
  Vector<const byte> reloc_info{reloc_info_writer.pos(), reloc_size};
  for (RelocIterator it(instructions, reloc_info, 0); !it.done(); it.next()) {
    RelocInfo::Mode rmode = it.rinfo()->rmode();
    if (rmode == RelocInfo::INTERNAL_REFERENCE) {
      RelocateInternalReference(rmode, it.rinfo()->pc(), pc_delta);
    }
  }
  DCHECK(!overflow());
}

void Assembler::db(uint8_t data) {
  CheckForEmitInForbiddenSlot();
  EmitHelper(data);
}

void Assembler::dd(uint32_t data) {
  CheckForEmitInForbiddenSlot();
  EmitHelper(data);
}

void Assembler::dq(uint64_t data) {
  CheckForEmitInForbiddenSlot();
  EmitHelper(data);
}

void Assembler::dd(Label* label) {
  uint64_t data;
  CheckForEmitInForbiddenSlot();
  if (label->is_bound()) {
    data = reinterpret_cast<uint64_t>(buffer_start_ + label->pos());
  } else {
    data = jump_address(label);
    unbound_labels_count_++;
    internal_reference_positions_.insert(label->pos());
  }
  RecordRelocInfo(RelocInfo::INTERNAL_REFERENCE);
  EmitHelper(data);
}

void Assembler::RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data) {
  if (!ShouldRecordRelocInfo(rmode)) return;
  // We do not try to reuse pool constants.
  RelocInfo rinfo(reinterpret_cast<Address>(pc_), rmode, data, Code());
  DCHECK_GE(buffer_space(), kMaxRelocSize);  // Too late to grow buffer here.
  reloc_info_writer.Write(&rinfo);
}

void Assembler::BlockTrampolinePoolFor(int instructions) {
  CheckTrampolinePoolQuick(instructions);
  BlockTrampolinePoolBefore(pc_offset() + instructions * kInstrSize);
}

void Assembler::CheckTrampolinePool() {
  // Some small sequences of instructions must not be broken up by the
  // insertion of a trampoline pool; such sequences are protected by setting
  // either trampoline_pool_blocked_nesting_ or no_trampoline_pool_before_,
  // which are both checked here. Also, recursive calls to CheckTrampolinePool
  // are blocked by trampoline_pool_blocked_nesting_.
  if ((trampoline_pool_blocked_nesting_ > 0) ||
      (pc_offset() < no_trampoline_pool_before_)) {
    // Emission is currently blocked; make sure we try again as soon as
    // possible.
    if (trampoline_pool_blocked_nesting_ > 0) {
      next_buffer_check_ = pc_offset() + kInstrSize;
    } else {
      next_buffer_check_ = no_trampoline_pool_before_;
    }
    return;
  }

  DCHECK(!trampoline_emitted_);
  DCHECK_GE(unbound_labels_count_, 0);
  if (unbound_labels_count_ > 0) {
    // First we emit jump, then we emit trampoline pool.
    {
      DEBUG_PRINTF("inserting trampoline pool at %p (%d)\n",
                   reinterpret_cast<Instr*>(buffer_start_ + pc_offset()),
                   pc_offset());
      BlockTrampolinePoolScope block_trampoline_pool(this);
      Label after_pool;
      RV_j(&after_pool);

      int pool_start = pc_offset();
      for (int i = 0; i < unbound_labels_count_; i++) {
        RV_j(&after_pool);
      }
      bind(&after_pool);
      trampoline_ = Trampoline(pool_start, unbound_labels_count_);

      trampoline_emitted_ = true;
      // As we are only going to emit trampoline once, we need to prevent any
      // further emission.
      next_buffer_check_ = kMaxInt;
    }
  } else {
    // Number of branches to unbound label at this point is zero, so we can
    // move next buffer check to maximum.
    next_buffer_check_ =
        pc_offset() + kMaxBranchOffset - kTrampolineSlotsSize * 16;
  }
  return;
}

Address Assembler::target_address_at(Address pc) {
  DEBUG_PRINTF("target_address_at: pc: %lx\t", pc);
  Instruction* instr0 = Instruction::At((unsigned char*)pc);
  Instruction* instr1 = Instruction::At((unsigned char*)(pc + 1 * kInstrSize));
  Instruction* instr3 = Instruction::At((unsigned char*)(pc + 3 * kInstrSize));
  Instruction* instr5 = Instruction::At((unsigned char*)(pc + 5 * kInstrSize));
  Instruction* instr7 = Instruction::At((unsigned char*)(pc + 7 * kInstrSize));

  // Interpret 5 instructions for address generated by li: See listing in
  // Assembler::set_target_address_at() just below.
  if ((instr0->BaseOpcodeFieldRaw() == RV_LUI) &&
      (instr1->ITypeBits() == RO_ADDIW) && (instr3->ITypeBits() == RO_ADDI) &&
      (instr5->ITypeBits() == RO_ADDI) && (instr7->ITypeBits() == RO_ADDI)) {
    // Assemble the 64 bit value.
    int64_t addr = (int64_t)(instr0->Imm20UValue() << kImm20Shift) +
                   (int64_t)instr1->Imm12Value();
    addr <<= 12;
    addr += (int64_t)instr3->Imm12Value();
    addr <<= 12;
    addr += (int64_t)instr5->Imm12Value();
    addr <<= 12;
    addr += (int64_t)instr7->Imm12Value();

    DEBUG_PRINTF("addr: %lx\n", addr);
    return static_cast<Address>(addr);
  }
  // We should never get here, force a bad address if we do.
  UNREACHABLE();
}

// On RISC-V, a 64-bit target address is stored in an 8-instruction sequence:
//    0: lui(rd, (j.imm64_ + (1LL << 47) + (1LL << 35) +
//                           (1LL << 23) + (1LL << 11)) >> 48);
//    1: addiw(rd, rd, (j.imm64_ + (1LL << 35) + (1LL << 23) + (1LL << 11))
//                       << 16 >> 52);
//    2: slli(rd, rd, 12);
//    3: addi(rd, rd, (j.imm64_ + (1LL << 23) + (1LL << 11)) << 28 >> 52);
//    4: slli(rd, rd, 12);
//    5: addi(rd, rd, (j.imm64_ + (1 << 11)) << 40 >> 52);
//    6: slli(rd, rd, 12);
//    7: addi(rd, rd, j.imm64_ << 52 >> 52);
//
// Patching the address must replace all the lui & addi instructions,
// and flush the i-cache.
void Assembler::set_target_value_at(Address pc, uint64_t target,
                                    ICacheFlushMode icache_flush_mode) {
  // FIXME(RISC-V): Does the below statement apply to RISC-V? If so, we do not
  //   need all 8 instructions.
  // There is an optimization where only 4 instructions are used to load address
  // in code on MIP64 because only 48-bits of address is effectively used.
  // It relies on fact the upper [63:48] bits are not used for virtual address
  // translation and they have to be set according to value of bit 47 in order
  // get canonical address.
  Instruction* instr0 = Instruction::At((unsigned char*)pc);
  DEBUG_PRINTF("set_target_value_at: pc: %lx\ttarget: %lx\n", pc, target);
  int rd_code = instr0->RV_RdValue();
  uint32_t* p = reinterpret_cast<uint32_t*>(pc);

#ifdef DEBUG
  // Check we have the result from a li macro-instruction.
  Instruction* instr1 = Instruction::At((unsigned char*)(pc + 1 * kInstrSize));
  Instruction* instr3 = Instruction::At((unsigned char*)(pc + 3 * kInstrSize));
  Instruction* instr5 = Instruction::At((unsigned char*)(pc + 5 * kInstrSize));
  Instruction* instr7 = Instruction::At((unsigned char*)(pc + 7 * kInstrSize));
  DCHECK(
      ((instr0->BaseOpcodeFieldRaw() == RV_LUI) &&
       (instr1->ITypeBits() == RO_ADDIW) && (instr3->ITypeBits() == RO_ADDI) &&
       (instr5->ITypeBits() == RO_ADDI) && (instr7->ITypeBits() == RO_ADDI)));
#endif

  // Must use 8 instructions to insure patchable code (see above comment).
  *p = RV_LUI | (rd_code << RV_kRdShift) |
       ((uint32_t)(
            (target + (1LL << 47) + (1LL << 35) + (1LL << 23) + (1LL << 11)) >>
            48)
        << kImm20Shift);
  *(p + 1) =
      OP_IMM_32 | (rd_code << RV_kRdShift) | (0b000 << kFunct3Shift) |
      (rd_code << kRs1Shift) |
      ((uint32_t)((target + (1LL << 35) + (1LL << 23) + (1LL << 11)) << 16 >>
                  52)
       << kImm12Shift);
  *(p + 2) = OP_IMM | (rd_code << RV_kRdShift) | (0b001 << kFunct3Shift) |
             (rd_code << kRs1Shift) | (12 << kImm12Shift);
  *(p + 3) = OP_IMM | (rd_code << RV_kRdShift) | (0b000 << kFunct3Shift) |
             (rd_code << kRs1Shift) |
             ((uint32_t)((target + (1LL << 23) + (1LL << 11)) << 28 >> 52)
              << kImm12Shift);
  *(p + 4) = OP_IMM | (rd_code << RV_kRdShift) | (0b001 << kFunct3Shift) |
             (rd_code << kRs1Shift) | (12 << kImm12Shift);
  *(p + 5) = OP_IMM | (rd_code << RV_kRdShift) | (0b000 << kFunct3Shift) |
             (rd_code << kRs1Shift) |
             ((uint32_t)((target + (1LL << 11)) << 40 >> 52) << kImm12Shift);
  *(p + 6) = OP_IMM | (rd_code << RV_kRdShift) | (0b001 << kFunct3Shift) |
             (rd_code << kRs1Shift) | (12 << kImm12Shift);
  *(p + 7) = OP_IMM | (rd_code << RV_kRdShift) | (0b000 << kFunct3Shift) |
             (rd_code << kRs1Shift) |
             ((uint32_t)(target << 52 >> 52) << kImm12Shift);

  if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
    FlushInstructionCache(pc, 8 * kInstrSize);
  }
}

UseScratchRegisterScope::UseScratchRegisterScope(Assembler* assembler)
    : available_(assembler->GetScratchRegisterList()),
      old_available_(*available_) {}

UseScratchRegisterScope::~UseScratchRegisterScope() {
  *available_ = old_available_;
}

Register UseScratchRegisterScope::Acquire() {
  DCHECK_NOT_NULL(available_);
  DCHECK_NE(*available_, 0);
  int index = static_cast<int>(base::bits::CountTrailingZeros32(*available_));
  *available_ &= ~(1UL << index);

  return Register::from_code(index);
}

bool UseScratchRegisterScope::hasAvailable() const { return *available_ != 0; }

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_RISCV
