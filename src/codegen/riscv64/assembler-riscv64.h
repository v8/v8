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

#ifndef V8_CODEGEN_RISCV_ASSEMBLER_RISCV_H_
#define V8_CODEGEN_RISCV_ASSEMBLER_RISCV_H_

#include <stdio.h>
#include <memory>
#include <set>

#include "src/codegen/assembler.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/label.h"
#include "src/codegen/riscv64/constants-riscv64.h"
#include "src/codegen/riscv64/register-riscv64.h"
#include "src/objects/contexts.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

class SafepointTableBuilder;

// -----------------------------------------------------------------------------
// Machine instruction Operands.
constexpr int kSmiShift = kSmiTagSize + kSmiShiftSize;
constexpr uint64_t kSmiShiftMask = (1UL << kSmiShift) - 1;
// Class Operand represents a shifter operand in data processing instructions.
class Operand {
 public:
  // Immediate.
  V8_INLINE explicit Operand(int64_t immediate,
                             RelocInfo::Mode rmode = RelocInfo::NONE)
      : rm_(no_reg), rmode_(rmode) {
    value_.immediate = immediate;
  }
  V8_INLINE explicit Operand(const ExternalReference& f)
      : rm_(no_reg), rmode_(RelocInfo::EXTERNAL_REFERENCE) {
    value_.immediate = static_cast<int64_t>(f.address());
  }
  V8_INLINE explicit Operand(const char* s);
  explicit Operand(Handle<HeapObject> handle);
  V8_INLINE explicit Operand(Smi value) : rm_(no_reg), rmode_(RelocInfo::NONE) {
    value_.immediate = static_cast<intptr_t>(value.ptr());
  }

  static Operand EmbeddedNumber(double number);  // Smi or HeapNumber.
  static Operand EmbeddedStringConstant(const StringConstantBase* str);

  // Register.
  V8_INLINE explicit Operand(Register rm) : rm_(rm) {}

  // Return true if this is a register operand.
  V8_INLINE bool is_reg() const;

  inline int64_t immediate() const;

  bool IsImmediate() const { return !rm_.is_valid(); }

  HeapObjectRequest heap_object_request() const {
    DCHECK(IsHeapObjectRequest());
    return value_.heap_object_request;
  }

  bool IsHeapObjectRequest() const {
    DCHECK_IMPLIES(is_heap_object_request_, IsImmediate());
    DCHECK_IMPLIES(is_heap_object_request_,
                   rmode_ == RelocInfo::FULL_EMBEDDED_OBJECT ||
                       rmode_ == RelocInfo::CODE_TARGET);
    return is_heap_object_request_;
  }

  Register rm() const { return rm_; }

  RelocInfo::Mode rmode() const { return rmode_; }

 private:
  Register rm_;
  union Value {
    Value() {}
    HeapObjectRequest heap_object_request;  // if is_heap_object_request_
    int64_t immediate;                      // otherwise
  } value_;                                 // valid if rm_ == no_reg
  bool is_heap_object_request_ = false;
  RelocInfo::Mode rmode_;

  friend class Assembler;
  friend class MacroAssembler;
};

// On RISC-V we have only one addressing mode with base_reg + offset.
// Class MemOperand represents a memory operand in load and store instructions.
class V8_EXPORT_PRIVATE MemOperand : public Operand {
 public:
  // Immediate value attached to offset.
  enum OffsetAddend { offset_minus_one = -1, offset_zero = 0 };

  explicit MemOperand(Register rn, int32_t offset = 0);
  explicit MemOperand(Register rn, int32_t unit, int32_t multiplier,
                      OffsetAddend offset_addend = offset_zero);
  int32_t offset() const { return offset_; }

  bool OffsetIsInt12Encodable() const { return is_int12(offset_); }

 private:
  int32_t offset_;

  friend class Assembler;
};

class V8_EXPORT_PRIVATE Assembler : public AssemblerBase {
 public:
  // Create an assembler. Instructions and relocation information are emitted
  // into a buffer, with the instructions starting from the beginning and the
  // relocation information starting from the end of the buffer. See CodeDesc
  // for a detailed comment on the layout (globals.h).
  //
  // If the provided buffer is nullptr, the assembler allocates and grows its
  // own buffer. Otherwise it takes ownership of the provided buffer.
  explicit Assembler(const AssemblerOptions&,
                     std::unique_ptr<AssemblerBuffer> = {});

  virtual ~Assembler() {}

  // GetCode emits any pending (non-emitted) code and fills the descriptor desc.
  static constexpr int kNoHandlerTable = 0;
  static constexpr SafepointTableBuilder* kNoSafepointTable = nullptr;
  void GetCode(Isolate* isolate, CodeDesc* desc,
               SafepointTableBuilder* safepoint_table_builder,
               int handler_table_offset);

  // Convenience wrapper for code without safepoint or handler tables.
  void GetCode(Isolate* isolate, CodeDesc* desc) {
    GetCode(isolate, desc, kNoSafepointTable, kNoHandlerTable);
  }

  // Unused on this architecture.
  void MaybeEmitOutOfLineConstantPool() {}

  // Label operations & relative jumps (PPUM Appendix D).
  //
  // Takes a branch opcode (cc) and a label (L) and generates
  // either a backward branch or a forward branch and links it
  // to the label fixup chain. Usage:
  //
  // Label L;    // unbound label
  // j(cc, &L);  // forward branch to unbound label
  // bind(&L);   // bind label to the current pc
  // j(cc, &L);  // backward branch to bound label
  // bind(&L);   // illegal: a label may be bound only once
  //
  // Note: The same Label can be used for forward and backward branches
  // but it may be bound only once.
  void bind(Label* L);  // Binds an unbound label L to current code position.

  enum OffsetSize : int {
    kOffset26 = 26,
    kOffset21 = 21,  // RISCV jal
    kOffset16 = 16,
    kOffset12 = 12,  // RISCV imm12
    kOffset20 = 20,  // RISCV imm20
    kOffset13 = 13   // RISCV branch
  };

  // Determines if Label is bound and near enough so that branch instruction
  // can be used to reach it, instead of jump instruction.
  bool is_near(Label* L);
  bool is_near(Label* L, OffsetSize bits);
  bool is_near_branch(Label* L);

  int RV_BranchOffset(Instr instr);
  int RV_JumpOffset(Instr instr);
  int BranchOffset(Instr instr);

  // Returns the branch offset to the given label from the current code
  // position. Links the label to the current position if it is still unbound.
  // Manages the jump elimination optimization if the second parameter is true.
  int32_t branch_offset_helper(Label* L, OffsetSize bits);
  inline int32_t RV_branch_offset(Label* L) {
    return branch_offset_helper(L, OffsetSize::kOffset13);
  }
  inline int32_t RV_jump_offset(Label* L) {
    return branch_offset_helper(L, OffsetSize::kOffset21);
  }
  inline int32_t branch_offset(Label* L) {
    return branch_offset_helper(L, OffsetSize::kOffset13);
  }
  inline int32_t branch_offset21(Label* L) {
    return branch_offset_helper(L, OffsetSize::kOffset21);
  }
  inline int32_t branch_offset26(Label* L) {
    return branch_offset_helper(L, OffsetSize::kOffset26);
  }
  inline int32_t shifted_branch_offset(Label* L) {
    return branch_offset(L) >> 2;
  }
  inline int32_t shifted_branch_offset21(Label* L) {
    return branch_offset21(L) >> 2;
  }
  inline int32_t shifted_branch_offset26(Label* L) {
    return branch_offset26(L) >> 2;
  }

  uint64_t jump_address(Label* L);
  uint64_t jump_offset(Label* L);
  uint64_t branch_long_offset(Label* L);

  // Puts a labels target address at the given position.
  // The high 8 bits are set to zero.
  void label_at_put(Label* L, int at_offset);

  // Read/Modify the code target address in the branch/call instruction at pc.
  // The isolate argument is unused (and may be nullptr) when skipping flushing.
  static Address target_address_at(Address pc);
  V8_INLINE static void set_target_address_at(
      Address pc, Address target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED) {
    set_target_value_at(pc, target, icache_flush_mode);
  }
  // On RISC-V there is no Constant Pool so we skip that parameter.
  V8_INLINE static Address target_address_at(Address pc,
                                             Address constant_pool) {
    return target_address_at(pc);
  }
  V8_INLINE static void set_target_address_at(
      Address pc, Address constant_pool, Address target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED) {
    set_target_address_at(pc, target, icache_flush_mode);
  }

  static void set_target_value_at(
      Address pc, uint64_t target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  static void JumpLabelToJumpRegister(Address pc);

  // This sets the branch destination (which gets loaded at the call address).
  // This is for calls and branches within generated code.  The serializer
  // has already deserialized the lui/ori instructions etc.
  inline static void deserialization_set_special_target_at(
      Address instruction_payload, Code code, Address target);

  // Get the size of the special target encoded at 'instruction_payload'.
  inline static int deserialization_special_target_size(
      Address instruction_payload);

  // This sets the internal reference at the pc.
  inline static void deserialization_set_target_internal_reference_at(
      Address pc, Address target,
      RelocInfo::Mode mode = RelocInfo::INTERNAL_REFERENCE);

  // Difference between address of current opcode and target address offset.
  static constexpr int kBranchPCOffset = kInstrSize;

  // Difference between address of current opcode and target address offset,
  // when we are generatinga sequence of instructions for long relative PC
  // branches
  static constexpr int kLongBranchPCOffset = 3 * kInstrSize;

  // Adjust ra register in branch delay slot of bal instruction so to skip
  // instructions not needed after optimization of PIC in
  // TurboAssembler::BranchAndLink method.

  static constexpr int kOptimizedBranchAndLinkLongReturnOffset = 4 * kInstrSize;

  // Here we are patching the address in the LUI/ADDI instruction pair.
  // These values are used in the serialization process and must be zero for
  // RISC-V platform, as Code, Embedded Object or External-reference pointers
  // are split across two consecutive instructions and don't exist separately
  // in the code, so the serializer should not step forwards in memory after
  // a target is resolved and written.
  static constexpr int kSpecialTargetSize = 0;

  // Number of consecutive instructions used to store 32bit/64bit constant.
  // This constant was used in RelocInfo::target_address_address() function
  // to tell serializer address of the instruction that follows
  // LUI/ADDI instruction pair.
  static constexpr int kInstructionsFor32BitConstant = 2;
  static constexpr int kInstructionsFor64BitConstant = 8;

  // Difference between address of current opcode and value read from pc
  // register.
  static constexpr int kPcLoadDelta = 4;

  // Bits available for offset field in branches
  static constexpr int kBranchOffsetBits = 13;

  // Bits available for offset field in jump
  static constexpr int kJumpOffsetBits = 21;

  // Max offset for b instructions with 12-bit offset field (multiple of 2)
  static constexpr int kMaxBranchOffset = (1 << (13 - 1)) - 1;

  // Max offset for jal instruction with 20-bit offset field (multiple of 2)
  static constexpr int kMaxJumpOffset = (1 << (21 - 1)) - 1;

  static constexpr int kTrampolineSlotsSize = 1 * kInstrSize;

  RegList* GetScratchRegisterList() { return &scratch_register_list_; }

  // ---------------------------------------------------------------------------
  // Code generation.

  // Insert the smallest number of nop instructions
  // possible to align the pc offset to a multiple
  // of m. m must be a power of 2 (>= 4).
  void Align(int m);
  // Insert the smallest number of zero bytes possible to align the pc offset
  // to a mulitple of m. m must be a power of 2 (>= 2).
  void DataAlign(int m);
  // Aligns code to something that's optimal for a jump target for the platform.
  void CodeTargetAlign();

  // Different nop operations are used by the code generator to detect certain
  // states of the generated code.
  enum NopMarkerTypes {
    NON_MARKING_NOP = 0,
    DEBUG_BREAK_NOP,
    // IC markers.
    PROPERTY_ACCESS_INLINED,
    PROPERTY_ACCESS_INLINED_CONTEXT,
    PROPERTY_ACCESS_INLINED_CONTEXT_DONT_DELETE,
    // Helper values.
    LAST_CODE_MARKER,
    FIRST_IC_MARKER = PROPERTY_ACCESS_INLINED,
  };

  // RISC-V Instructions Emited to a buffer

  void RV_lui(Register rd, int32_t imm20);
  void RV_auipc(Register rd, int32_t imm20);

  // Jumps
  void RV_jal(Register rd, int32_t imm20);
  void RV_jalr(Register rd, Register rs1, int16_t imm12);

  // Branches
  void RV_beq(Register rs1, Register rs2, int16_t imm12);
  inline void RV_beq(Register rs1, Register rs2, Label* L) {
    RV_beq(rs1, rs2, RV_branch_offset(L));
  }
  void RV_bne(Register rs1, Register rs2, int16_t imm12);
  inline void RV_bne(Register rs1, Register rs2, Label* L) {
    RV_bne(rs1, rs2, RV_branch_offset(L));
  }
  void RV_blt(Register rs1, Register rs2, int16_t imm12);
  inline void RV_blt(Register rs1, Register rs2, Label* L) {
    RV_blt(rs1, rs2, RV_branch_offset(L));
  }
  void RV_bge(Register rs1, Register rs2, int16_t imm12);
  inline void RV_bge(Register rs1, Register rs2, Label* L) {
    RV_bge(rs1, rs2, RV_branch_offset(L));
  }
  void RV_bltu(Register rs1, Register rs2, int16_t imm12);
  inline void RV_bltu(Register rs1, Register rs2, Label* L) {
    RV_bltu(rs1, rs2, RV_branch_offset(L));
  }
  void RV_bgeu(Register rs1, Register rs2, int16_t imm12);
  inline void RV_bgeu(Register rs1, Register rs2, Label* L) {
    RV_bgeu(rs1, rs2, RV_branch_offset(L));
  }

  // Loads
  void RV_lb(Register rd, Register rs1, int16_t imm12);
  void RV_lh(Register rd, Register rs1, int16_t imm12);
  void RV_lw(Register rd, Register rs1, int16_t imm12);
  void RV_lbu(Register rd, Register rs1, int16_t imm12);
  void RV_lhu(Register rd, Register rs1, int16_t imm12);

  // Stores
  void RV_sb(Register source, Register base, int16_t imm12);
  void RV_sh(Register source, Register base, int16_t imm12);
  void RV_sw(Register source, Register base, int16_t imm12);

  // Arithmetic with immediate
  void RV_addi(Register rd, Register rs1, int16_t imm12);
  void RV_slti(Register rd, Register rs1, int16_t imm12);
  void RV_sltiu(Register rd, Register rs1, int16_t imm12);
  void RV_xori(Register rd, Register rs1, int16_t imm12);
  void RV_ori(Register rd, Register rs1, int16_t imm12);
  void RV_andi(Register rd, Register rs1, int16_t imm12);
  void RV_slli(Register rd, Register rs1, uint8_t shamt);
  void RV_srli(Register rd, Register rs1, uint8_t shamt);
  void RV_srai(Register rd, Register rs1, uint8_t shamt);

  // Arithmetic
  void RV_add(Register rd, Register rs1, Register rs2);
  void RV_sub(Register rd, Register rs1, Register rs2);
  void RV_sll(Register rd, Register rs1, Register rs2);
  void RV_slt(Register rd, Register rs1, Register rs2);
  void RV_sltu(Register rd, Register rs1, Register rs2);
  void RV_xor_(Register rd, Register rs1, Register rs2);
  void RV_srl(Register rd, Register rs1, Register rs2);
  void RV_sra(Register rd, Register rs1, Register rs2);
  void RV_or_(Register rd, Register rs1, Register rs2);
  void RV_and_(Register rd, Register rs1, Register rs2);

  // Memory fences
  void RV_fence(uint8_t pred, uint8_t succ);
  void RV_fence_tso();
  void RV_fence_i();

  // Environment call / break
  void RV_ecall();
  void RV_ebreak();

  // This is a de facto standard (as set by GNU binutils) 32-bit unimplemented
  // instruction (i.e., it should always trap, if your implementation has
  // invalid instruction traps).
  void RV_unimp();

  // CSR
  void RV_csrrw(Register rd, ControlStatusReg csr, Register rs1);
  void RV_csrrs(Register rd, ControlStatusReg csr, Register rs1);
  void RV_csrrc(Register rd, ControlStatusReg csr, Register rs1);
  void RV_csrrwi(Register rd, ControlStatusReg csr, uint8_t imm5);
  void RV_csrrsi(Register rd, ControlStatusReg csr, uint8_t imm5);
  void RV_csrrci(Register rd, ControlStatusReg csr, uint8_t imm5);

  // RV64I
  void RV_lwu(Register rd, Register rs1, int16_t imm12);
  void RV_ld(Register rd, Register rs1, int16_t imm12);
  void RV_sd(Register source, Register base, int16_t imm12);
  void RV_addiw(Register rd, Register rs1, int16_t imm12);
  void RV_slliw(Register rd, Register rs1, uint8_t shamt);
  void RV_srliw(Register rd, Register rs1, uint8_t shamt);
  void RV_sraiw(Register rd, Register rs1, uint8_t shamt);
  void RV_addw(Register rd, Register rs1, Register rs2);
  void RV_subw(Register rd, Register rs1, Register rs2);
  void RV_sllw(Register rd, Register rs1, Register rs2);
  void RV_srlw(Register rd, Register rs1, Register rs2);
  void RV_sraw(Register rd, Register rs1, Register rs2);

  // RV32M Standard Extension
  void RV_mul(Register rd, Register rs1, Register rs2);
  void RV_mulh(Register rd, Register rs1, Register rs2);
  void RV_mulhsu(Register rd, Register rs1, Register rs2);
  void RV_mulhu(Register rd, Register rs1, Register rs2);
  void RV_div(Register rd, Register rs1, Register rs2);
  void RV_divu(Register rd, Register rs1, Register rs2);
  void RV_rem(Register rd, Register rs1, Register rs2);
  void RV_remu(Register rd, Register rs1, Register rs2);

  // RV64M Standard Extension (in addition to RV32M)
  void RV_mulw(Register rd, Register rs1, Register rs2);
  void RV_divw(Register rd, Register rs1, Register rs2);
  void RV_divuw(Register rd, Register rs1, Register rs2);
  void RV_remw(Register rd, Register rs1, Register rs2);
  void RV_remuw(Register rd, Register rs1, Register rs2);

  // RV32A Standard Extension
  void RV_lr_w(bool aq, bool rl, Register rd, Register rs1);
  void RV_sc_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amoswap_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amoadd_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amoxor_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amoand_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amoor_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amomin_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amomax_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amominu_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amomaxu_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);

  // RV64A Standard Extension (in addition to RV32A)
  void RV_lr_d(bool aq, bool rl, Register rd, Register rs1);
  void RV_sc_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amoswap_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amoadd_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amoxor_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amoand_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amoor_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amomin_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amomax_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amominu_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
  void RV_amomaxu_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);

  // RV32F Standard Extension
  void RV_flw(FPURegister rd, Register rs1, int16_t imm12);
  void RV_fsw(FPURegister source, Register base, int16_t imm12);
  void RV_fmadd_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                  FPURegister rs3, RoundingMode frm = RNE);
  void RV_fmsub_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                  FPURegister rs3, RoundingMode frm = RNE);
  void RV_fnmsub_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                   FPURegister rs3, RoundingMode frm = RNE);
  void RV_fnmadd_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                   FPURegister rs3, RoundingMode frm = RNE);
  void RV_fadd_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                 RoundingMode frm = RNE);
  void RV_fsub_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                 RoundingMode frm = RNE);
  void RV_fmul_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                 RoundingMode frm = RNE);
  void RV_fdiv_s(FPURegister rd, FPURegister rs1, FPURegister rs2,
                 RoundingMode frm = RNE);
  void RV_fsqrt_s(FPURegister rd, FPURegister rs1, RoundingMode frm = RNE);
  void RV_fsgnj_s(FPURegister rd, FPURegister rs1, FPURegister rs2);
  void RV_fsgnjn_s(FPURegister rd, FPURegister rs1, FPURegister rs2);
  void RV_fsgnjx_s(FPURegister rd, FPURegister rs1, FPURegister rs2);
  void RV_fmin_s(FPURegister rd, FPURegister rs1, FPURegister rs2);
  void RV_fmax_s(FPURegister rd, FPURegister rs1, FPURegister rs2);
  void RV_fcvt_w_s(Register rd, FPURegister rs1, RoundingMode frm = RNE);
  void RV_fcvt_wu_s(Register rd, FPURegister rs1, RoundingMode frm = RNE);
  void RV_fmv_x_w(Register rd, FPURegister rs1);
  void RV_feq_s(Register rd, FPURegister rs1, FPURegister rs2);
  void RV_flt_s(Register rd, FPURegister rs1, FPURegister rs2);
  void RV_fle_s(Register rd, FPURegister rs1, FPURegister rs2);
  void RV_fclass_s(Register rd, FPURegister rs1);
  void RV_fcvt_s_w(FPURegister rd, Register rs1, RoundingMode frm = RNE);
  void RV_fcvt_s_wu(FPURegister rd, Register rs1, RoundingMode frm = RNE);
  void RV_fmv_w_x(FPURegister rd, Register rs1);

  // RV64F Standard Extension (in addition to RV32F)
  void RV_fcvt_l_s(Register rd, FPURegister rs1, RoundingMode frm = RNE);
  void RV_fcvt_lu_s(Register rd, FPURegister rs1, RoundingMode frm = RNE);
  void RV_fcvt_s_l(FPURegister rd, Register rs1, RoundingMode frm = RNE);
  void RV_fcvt_s_lu(FPURegister rd, Register rs1, RoundingMode frm = RNE);

  // RV32D Standard Extension
  void RV_fld(FPURegister rd, Register rs1, int16_t imm12);
  void RV_fsd(FPURegister source, Register base, int16_t imm12);
  void RV_fmadd_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                  FPURegister rs3, RoundingMode frm = RNE);
  void RV_fmsub_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                  FPURegister rs3, RoundingMode frm = RNE);
  void RV_fnmsub_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                   FPURegister rs3, RoundingMode frm = RNE);
  void RV_fnmadd_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                   FPURegister rs3, RoundingMode frm = RNE);
  void RV_fadd_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                 RoundingMode frm = RNE);
  void RV_fsub_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                 RoundingMode frm = RNE);
  void RV_fmul_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                 RoundingMode frm = RNE);
  void RV_fdiv_d(FPURegister rd, FPURegister rs1, FPURegister rs2,
                 RoundingMode frm = RNE);
  void RV_fsqrt_d(FPURegister rd, FPURegister rs1, RoundingMode frm = RNE);
  void RV_fsgnj_d(FPURegister rd, FPURegister rs1, FPURegister rs2);
  void RV_fsgnjn_d(FPURegister rd, FPURegister rs1, FPURegister rs2);
  void RV_fsgnjx_d(FPURegister rd, FPURegister rs1, FPURegister rs2);
  void RV_fmin_d(FPURegister rd, FPURegister rs1, FPURegister rs2);
  void RV_fmax_d(FPURegister rd, FPURegister rs1, FPURegister rs2);
  void RV_fcvt_s_d(FPURegister rd, FPURegister rs1, RoundingMode frm = RNE);
  void RV_fcvt_d_s(FPURegister rd, FPURegister rs1, RoundingMode frm = RNE);
  void RV_feq_d(Register rd, FPURegister rs1, FPURegister rs2);
  void RV_flt_d(Register rd, FPURegister rs1, FPURegister rs2);
  void RV_fle_d(Register rd, FPURegister rs1, FPURegister rs2);
  void RV_fclass_d(Register rd, FPURegister rs1);
  void RV_fcvt_w_d(Register rd, FPURegister rs1, RoundingMode frm = RNE);
  void RV_fcvt_wu_d(Register rd, FPURegister rs1, RoundingMode frm = RNE);
  void RV_fcvt_d_w(FPURegister rd, Register rs1, RoundingMode frm = RNE);
  void RV_fcvt_d_wu(FPURegister rd, Register rs1, RoundingMode frm = RNE);

  // RV64D Standard Extension (in addition to RV32D)
  void RV_fcvt_l_d(Register rd, FPURegister rs1, RoundingMode frm = RNE);
  void RV_fcvt_lu_d(Register rd, FPURegister rs1, RoundingMode frm = RNE);
  void RV_fmv_x_d(Register rd, FPURegister rs1);
  void RV_fcvt_d_l(FPURegister rd, Register rs1, RoundingMode frm = RNE);
  void RV_fcvt_d_lu(FPURegister rd, Register rs1, RoundingMode frm = RNE);
  void RV_fmv_d_x(FPURegister rd, Register rs1);

  // Privileged
  void RV_uret();
  void RV_sret();
  void RV_mret();
  void RV_wfi();
  void RV_sfence_vma(Register rs1, Register rs2);

  // Assembler Pseudo Instructions (Tables 25.2, 25.3, RISC-V Unprivileged ISA)
  void nop();
  void RV_li(Register rd, int64_t imm);
  // Returns the number of instructions required to load the immediate
  static int li_count(int64_t imm);
  // Loads an immediate, always using 8 instructions, regardless of the value,
  // so that it can be modified later.
  void RV_li_constant(Register rd, int64_t imm);
  void RV_mv(Register rd, Register rs1);
  void RV_not(Register rd, Register rs1);
  void RV_neg(Register rd, Register rs1);
  void RV_negw(Register rd, Register rs1);
  void RV_sext_w(Register rd, Register rs1);
  void RV_seqz(Register rd, Register rs1);
  void RV_snez(Register rd, Register rs1);
  void RV_sltz(Register rd, Register rs1);
  void RV_sgtz(Register rd, Register rs1);

  void RV_fmv_s(FPURegister rd, FPURegister rs);
  void RV_fabs_s(FPURegister rd, FPURegister rs);
  void RV_fneg_s(FPURegister rd, FPURegister rs);
  void RV_fmv_d(FPURegister rd, FPURegister rs);
  void RV_fabs_d(FPURegister rd, FPURegister rs);
  void RV_fneg_d(FPURegister rd, FPURegister rs);

  void RV_beqz(Register rs1, int16_t imm12);
  inline void RV_beqz(Register rs1, Label* L) {
    RV_beqz(rs1, RV_branch_offset(L));
  }
  void RV_bnez(Register rs1, int16_t imm12);
  inline void RV_bnez(Register rs1, Label* L) {
    RV_bnez(rs1, RV_branch_offset(L));
  }
  void RV_blez(Register rs, int16_t imm12);
  inline void RV_blez(Register rs1, Label* L) {
    RV_blez(rs1, RV_branch_offset(L));
  }
  void RV_bgez(Register rs, int16_t imm12);
  inline void RV_bgez(Register rs1, Label* L) {
    RV_bgez(rs1, RV_branch_offset(L));
  }
  void RV_bltz(Register rs, int16_t imm12);
  inline void RV_bltz(Register rs1, Label* L) {
    RV_bltz(rs1, RV_branch_offset(L));
  }
  void RV_bgtz(Register rs, int16_t imm12);
  inline void RV_bgtz(Register rs1, Label* L) {
    RV_bgtz(rs1, RV_branch_offset(L));
  }
  void RV_bgt(Register rs1, Register rs2, int16_t imm12);
  inline void RV_bgt(Register rs1, Register rs2, Label* L) {
    RV_bgt(rs1, rs2, RV_branch_offset(L));
  }
  void RV_ble(Register rs1, Register rs2, int16_t imm12);
  inline void RV_ble(Register rs1, Register rs2, Label* L) {
    RV_ble(rs1, rs2, RV_branch_offset(L));
  }
  void RV_bgtu(Register rs1, Register rs2, int16_t imm12);
  inline void RV_bgtu(Register rs1, Register rs2, Label* L) {
    RV_bgtu(rs1, rs2, RV_branch_offset(L));
  }
  void RV_bleu(Register rs1, Register rs2, int16_t imm12);
  inline void RV_bleu(Register rs1, Register rs2, Label* L) {
    RV_bleu(rs1, rs2, RV_branch_offset(L));
  }

  void RV_j(int32_t imm20);
  inline void RV_j(Label* L) { RV_j(RV_jump_offset(L)); }
  void RV_jal(int32_t imm20);
  inline void RV_jal(Label* L) { RV_jal(RV_jump_offset(L)); }
  void RV_jr(Register rs);
  void RV_jalr(Register rs);
  void RV_ret();
  void RV_call(int32_t offset);

  // Read instructions-retired counter
  void RV_rdinstret(Register rd);
  // Read upper 32-bits of instructions-retired counter
  void RV_rdinstreth(Register rd);
  // Read cycle counter
  void RV_rdcycle(Register rd);
  // Read upper 32-bits of cycle counter
  void RV_rdcycleh(Register rd);
  // Read real-time clock
  void RV_rdtime(Register rd);
  // Read upper 32-bits of real-time clock
  void RV_rdtimeh(Register rd);

  // Read CSR
  void RV_csrr(Register rd, ControlStatusReg csr);
  // Write CSR
  void RV_csrw(ControlStatusReg csr, Register rs);
  // Set bits in CSR
  void RV_csrs(ControlStatusReg csr, Register rs);
  // Clear bits in CSR
  void RV_csrc(ControlStatusReg csr, Register rs);

  // Write CSR, immediate
  void RV_csrwi(ControlStatusReg csr, uint8_t imm);
  // Set bits in CSR, immediate
  void RV_csrsi(ControlStatusReg csr, uint8_t imm);
  // Clear bits in CSR, immediate
  void RV_csrci(ControlStatusReg csr, uint8_t imm);

  // Read FP control/status register
  void RV_frcsr(Register rd);
  // Swap FP control/status register
  void RV_fscsr(Register rd, Register rs);
  // Write FP control/status register
  void RV_fscsr(Register rs);

  // Read FP rounding mode
  void RV_frrm(Register rd);
  // Swap FP rounding mode
  void RV_fsrm(Register rd, Register rs);
  // Write FP rounding mode
  void RV_fsrm(Register rs);

  // Read FP exception flags
  void RV_frflags(Register rd);
  // Swap FP exception flags
  void RV_fsflags(Register rd, Register rs);
  // Write FP exception flags
  void RV_fsflags(Register rs);

  // MIPS Instructions

  // --------Branch-and-jump-instructions----------
  // We don't use likely variant of instructions.
  void b(int16_t offset);
  inline void b(Label* L) { b(shifted_branch_offset(L)); }
  void bal(int16_t offset);
  inline void bal(Label* L) { bal(shifted_branch_offset(L)); }

  // -------Data-processing-instructions---------

  // Arithmetic.

  void addu(Register rd, Register rs, Register rt);
  void subu(Register rd, Register rs, Register rt);
  void daddu(Register rd, Register rs, Register rt);
  void dsubu(Register rd, Register rs, Register rt);

  void addiu(Register rd, Register rs, int32_t j);
  void daddiu(Register rd, Register rs, int32_t j);

  // Logical.
  void and_(Register rd, Register rs, Register rt);
  void or_(Register rd, Register rs, Register rt);
  void xor_(Register rd, Register rs, Register rt);
  void nor(Register rd, Register rs, Register rt);

  void andi(Register rd, Register rs, int32_t j);
  void xori(Register rd, Register rs, int32_t j);

  // Shifts.
  // Please note: sll(zero_reg, zero_reg, x) instructions are reserved as nop
  // and may cause problems in normal code. coming_from_nop makes sure this
  // doesn't happen.
  void sll(Register rd, Register rt, uint16_t sa, bool coming_from_nop = false);
  void sllv(Register rd, Register rt, Register rs);
  void srl(Register rd, Register rt, uint16_t sa);
  void srlv(Register rd, Register rt, Register rs);
  void sra(Register rt, Register rd, uint16_t sa);
  void srav(Register rt, Register rd, Register rs);
  void dsll(Register rd, Register rt, uint16_t sa);
  void dsllv(Register rd, Register rt, Register rs);
  void dsrl(Register rd, Register rt, uint16_t sa);
  void dsra(Register rt, Register rd, uint16_t sa);
  void dsll32(Register rt, Register rd, uint16_t sa);
  void dsrl32(Register rt, Register rd, uint16_t sa);
  void dsra32(Register rt, Register rd, uint16_t sa);

  // ------------Memory-instructions-------------

  void lb(Register rd, const MemOperand& rs);
  void lbu(Register rd, const MemOperand& rs);
  void lw(Register rd, const MemOperand& rs);
  void lwu(Register rd, const MemOperand& rs);
  void sb(Register rd, const MemOperand& rs);
  void sh(Register rd, const MemOperand& rs);
  void sw(Register rd, const MemOperand& rs);
  void ld(Register rd, const MemOperand& rs);
  void sd(Register rd, const MemOperand& rs);

  // ----------Atomic instructions--------------

  void ll(Register rd, const MemOperand& rs);
  void sc(Register rd, const MemOperand& rs);
  void lld(Register rd, const MemOperand& rs);
  void scd(Register rd, const MemOperand& rs);

  // ----------------Prefetch--------------------

  void pref(int32_t hint, const MemOperand& rs);

  // -------------Misc-instructions--------------

  // Break / Trap instructions.
  void break_(uint32_t code, bool break_as_stop = false);
  void stop(uint32_t code = kMaxStopCode);

  // Memory barrier instruction.
  void sync();

  // --------Coprocessor-instructions----------------

  // Load, store, and move.
  void lwc1(FPURegister fd, const MemOperand& src);
  void swc1(FPURegister fs, const MemOperand& dst);

  // Check the code size generated from label to here.
  int SizeOfCodeGeneratedSince(Label* label) {
    return pc_offset() - label->pos();
  }

  // Check the number of instructions generated from label to here.
  int InstructionsGeneratedSince(Label* label) {
    return SizeOfCodeGeneratedSince(label) / kInstrSize;
  }

  // Class for scoping postponing the trampoline pool generation.
  class BlockTrampolinePoolScope {
   public:
    explicit BlockTrampolinePoolScope(Assembler* assem) : assem_(assem) {
      assem_->StartBlockTrampolinePool();
    }
    ~BlockTrampolinePoolScope() { assem_->EndBlockTrampolinePool(); }

   private:
    Assembler* assem_;

    DISALLOW_IMPLICIT_CONSTRUCTORS(BlockTrampolinePoolScope);
  };

  // Class for postponing the assembly buffer growth. Typically used for
  // sequences of instructions that must be emitted as a unit, before
  // buffer growth (and relocation) can occur.
  // This blocking scope is not nestable.
  class BlockGrowBufferScope {
   public:
    explicit BlockGrowBufferScope(Assembler* assem) : assem_(assem) {
      assem_->StartBlockGrowBuffer();
    }
    ~BlockGrowBufferScope() { assem_->EndBlockGrowBuffer(); }

   private:
    Assembler* assem_;

    DISALLOW_IMPLICIT_CONSTRUCTORS(BlockGrowBufferScope);
  };

  // Record a deoptimization reason that can be used by a log or cpu profiler.
  // Use --trace-deopt to enable.
  void RecordDeoptReason(DeoptimizeReason reason, SourcePosition position,
                         int id);

  static int RelocateInternalReference(RelocInfo::Mode rmode, Address pc,
                                       intptr_t pc_delta);

  // Writes a single byte or word of data in the code stream.  Used for
  // inline tables, e.g., jump-tables.
  void db(uint8_t data);
  void dd(uint32_t data);
  void dq(uint64_t data);
  void dp(uintptr_t data) { dq(data); }
  void dd(Label* label);

  // Postpone the generation of the trampoline pool for the specified number of
  // instructions.
  void BlockTrampolinePoolFor(int instructions);

  // Check if there is less than kGap bytes available in the buffer.
  // If this is the case, we need to grow the buffer before emitting
  // an instruction or relocation information.
  inline bool overflow() const { return pc_ >= reloc_info_writer.pos() - kGap; }

  // Get the number of bytes available in the buffer.
  inline intptr_t available_space() const {
    return reloc_info_writer.pos() - pc_;
  }

  // Read/patch instructions.
  static Instr instr_at(Address pc) { return *reinterpret_cast<Instr*>(pc); }
  static void instr_at_put(Address pc, Instr instr) {
    *reinterpret_cast<Instr*>(pc) = instr;
  }
  Instr instr_at(int pos) {
    return *reinterpret_cast<Instr*>(buffer_start_ + pos);
  }
  void instr_at_put(int pos, Instr instr) {
    *reinterpret_cast<Instr*>(buffer_start_ + pos) = instr;
  }

  // Check if an instruction is a branch of some kind.
  static bool RV_IsBranch(Instr instr);
  static bool RV_IsJump(Instr instr);
  static bool RV_IsJal(Instr instr);
  static bool RV_IsJalr(Instr instr);

  void CheckTrampolinePool();

  bool IsPrevInstrCompactBranch() { return prev_instr_compact_branch_; }

  inline int UnboundLabelsCount() { return unbound_labels_count_; }

 protected:
  // Readable constants for base and offset adjustment helper, these indicate if
  // aside from offset, another value like offset + 4 should fit into int16.
  enum class OffsetAccessType : bool {
    SINGLE_ACCESS = false,
    TWO_ACCESSES = true
  };

  // Determine whether need to adjust base and offset of memroy load/store
  bool NeedAdjustBaseAndOffset(
      const MemOperand& src, OffsetAccessType = OffsetAccessType::SINGLE_ACCESS,
      int second_Access_add_to_offset = 4);

  // Helper function for memory load/store using base register and offset.
  void AdjustBaseAndOffset(
      MemOperand* src, Register scratch,
      OffsetAccessType access_type = OffsetAccessType::SINGLE_ACCESS,
      int second_access_add_to_offset = 4);

  inline static void set_target_internal_reference_encoded_at(Address pc,
                                                              Address target);

  int64_t buffer_space() const { return reloc_info_writer.pos() - pc_; }

  // Decode branch instruction at pos and return branch target pos.
  int target_at(int pos, bool is_internal);
  int RV_target_at(int pos, bool is_internal);

  // Patch branch instruction at pos to branch to given branch target pos.
  void target_at_put(int pos, int target_pos, bool is_internal);
  void RV_target_at_put(int pos, int target_pos, bool is_internal);

  // Say if we need to relocate with this mode.
  bool MustUseReg(RelocInfo::Mode rmode);

  // Record reloc info for current pc_.
  void RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data = 0);

  // Block the emission of the trampoline pool before pc_offset.
  void BlockTrampolinePoolBefore(int pc_offset) {
    if (no_trampoline_pool_before_ < pc_offset)
      no_trampoline_pool_before_ = pc_offset;
  }

  void StartBlockTrampolinePool() { trampoline_pool_blocked_nesting_++; }

  void EndBlockTrampolinePool() {
    trampoline_pool_blocked_nesting_--;
    if (trampoline_pool_blocked_nesting_ == 0) {
      CheckTrampolinePoolQuick(1);
    }
  }

  bool is_trampoline_pool_blocked() const {
    return trampoline_pool_blocked_nesting_ > 0;
  }

  bool has_exception() const { return internal_trampoline_exception_; }

  bool is_trampoline_emitted() const { return trampoline_emitted_; }

  // Temporarily block automatic assembly buffer growth.
  void StartBlockGrowBuffer() {
    DCHECK(!block_buffer_growth_);
    block_buffer_growth_ = true;
  }

  void EndBlockGrowBuffer() {
    DCHECK(block_buffer_growth_);
    block_buffer_growth_ = false;
  }

  bool is_buffer_growth_blocked() const { return block_buffer_growth_; }

  void EmitForbiddenSlotInstruction() {
    if (IsPrevInstrCompactBranch()) {
      nop();
    }
  }

  void CheckTrampolinePoolQuick(int extra_instructions = 0) {
    if (pc_offset() >= next_buffer_check_ - extra_instructions * kInstrSize) {
      CheckTrampolinePool();
    }
  }

 private:
  // Avoid overflows for displacements etc.
  static const int kMaximalBufferSize = 512 * MB;

  // Buffer size and constant pool distance are checked together at regular
  // intervals of kBufferCheckInterval emitted bytes.
  static constexpr int kBufferCheckInterval = 1 * KB / 2;

  // Code generation.
  // The relocation writer's position is at least kGap bytes below the end of
  // the generated instructions. This is so that multi-instruction sequences do
  // not have to check for overflow. The same is true for writes of large
  // relocation info entries.
  static constexpr int kGap = 64;
  STATIC_ASSERT(AssemblerBase::kMinimalBufferSize >= 2 * kGap);

  // Repeated checking whether the trampoline pool should be emitted is rather
  // expensive. By default we only check again once a number of instructions
  // has been generated.
  static constexpr int kCheckConstIntervalInst = 32;
  static constexpr int kCheckConstInterval =
      kCheckConstIntervalInst * kInstrSize;

  int next_buffer_check_;  // pc offset of next buffer check.

  // Emission of the trampoline pool may be blocked in some code sequences.
  int trampoline_pool_blocked_nesting_;  // Block emission if this is not zero.
  int no_trampoline_pool_before_;  // Block emission before this pc offset.

  // Keep track of the last emitted pool to guarantee a maximal distance.
  int last_trampoline_pool_end_;  // pc offset of the end of the last pool.

  // Automatic growth of the assembly buffer may be blocked for some sequences.
  bool block_buffer_growth_;  // Block growth when true.

  // Relocation information generation.
  // Each relocation is encoded as a variable size value.
  static constexpr int kMaxRelocSize = RelocInfoWriter::kMaxSize;
  RelocInfoWriter reloc_info_writer;

  // The bound position, before this we cannot do instruction elimination.
  int last_bound_pos_;

  // Readable constants for compact branch handling in emit()
  enum class CompactBranchType : bool { NO = false, COMPACT_BRANCH = true };

  // Code emission.
  inline void CheckBuffer();
  void GrowBuffer();
  inline void emit(Instr x,
                   CompactBranchType is_compact_branch = CompactBranchType::NO);
  inline void emit(uint64_t x);
  inline void CheckForEmitInForbiddenSlot();
  template <typename T>
  inline void EmitHelper(T x);
  inline void EmitHelper(Instr x, CompactBranchType is_compact_branch);

  void disassembleInstr(Instr instr);

  // Instruction generation.

  // ----- Top-level instruction formats match those in the ISA manual
  // (R, I, S, B, U, J). These match the formats defined in LLVM's
  // RISCVInstrFormats.td.
  void GenInstrR(uint8_t funct7, uint8_t funct3, Opcode opcode, Register rd,
                 Register rs1, Register rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, Opcode opcode, FPURegister rd,
                 FPURegister rs1, FPURegister rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, Opcode opcode, Register rd,
                 FPURegister rs1, Register rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, Opcode opcode, FPURegister rd,
                 Register rs1, Register rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, Opcode opcode, FPURegister rd,
                 FPURegister rs1, Register rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, Opcode opcode, Register rd,
                 FPURegister rs1, FPURegister rs2);
  void GenInstrR4(uint8_t funct2, Opcode opcode, Register rd, Register rs1,
                  Register rs2, Register rs3, RoundingMode frm);
  void GenInstrR4(uint8_t funct2, Opcode opcode, FPURegister rd,
                  FPURegister rs1, FPURegister rs2, FPURegister rs3,
                  RoundingMode frm);
  void GenInstrRAtomic(uint8_t funct5, bool aq, bool rl, uint8_t funct3,
                       Register rd, Register rs1, Register rs2);
  void GenInstrRFrm(uint8_t funct7, Opcode opcode, Register rd, Register rs1,
                    Register rs2, RoundingMode frm);
  void GenInstrI(uint8_t funct3, Opcode opcode, Register rd, Register rs1,
                 int16_t imm12);
  void GenInstrI(uint8_t funct3, Opcode opcode, FPURegister rd, Register rs1,
                 int16_t imm12);
  void GenInstrIShift(bool arithshift, uint8_t funct3, Opcode opcode,
                      Register rd, Register rs1, uint8_t shamt);
  void GenInstrIShiftW(bool arithshift, uint8_t funct3, Opcode opcode,
                       Register rd, Register rs1, uint8_t shamt);
  void GenInstrS(uint8_t funct3, Opcode opcode, Register rs1, Register rs2,
                 int16_t imm12);
  void GenInstrS(uint8_t funct3, Opcode opcode, Register rs1, FPURegister rs2,
                 int16_t imm12);
  void GenInstrB(uint8_t funct3, Opcode opcode, Register rs1, Register rs2,
                 int16_t imm12);
  void GenInstrU(Opcode opcode, Register rd, int32_t imm20);
  void GenInstrJ(Opcode opcode, Register rd, int32_t imm20);

  // ----- Instruction class templates match those in LLVM's RISCVInstrInfo.td
  void GenInstrBranchCC_rri(uint8_t funct3, Register rs1, Register rs2,
                            int16_t imm12);
  void GenInstrLoad_ri(uint8_t funct3, Register rd, Register rs1,
                       int16_t imm12);
  void GenInstrStore_rri(uint8_t funct3, Register rs1, Register rs2,
                         int16_t imm12);
  void GenInstrALU_ri(uint8_t funct3, Register rd, Register rs1, int16_t imm12);
  void GenInstrShift_ri(bool arithshift, uint8_t funct3, Register rd,
                        Register rs1, uint8_t shamt);
  void GenInstrALU_rr(uint8_t funct7, uint8_t funct3, Register rd, Register rs1,
                      Register rs2);
  void GenInstrCSR_ir(uint8_t funct3, Register rd, ControlStatusReg csr,
                      Register rs1);
  void GenInstrCSR_ii(uint8_t funct3, Register rd, ControlStatusReg csr,
                      uint8_t rs1);
  void GenInstrShiftW_ri(bool arithshift, uint8_t funct3, Register rd,
                         Register rs1, uint8_t shamt);
  void GenInstrALUW_rr(uint8_t funct7, uint8_t funct3, Register rd,
                       Register rs1, Register rs2);
  void GenInstrPriv(uint8_t funct7, Register rs1, Register rs2);
  void GenInstrLoadFP_ri(uint8_t funct3, FPURegister rd, Register rs1,
                         int16_t imm12);
  void GenInstrStoreFP_rri(uint8_t funct3, Register rs1, FPURegister rs2,
                           int16_t imm12);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, FPURegister rd,
                        FPURegister rs1, FPURegister rs2);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, FPURegister rd,
                        Register rs1, Register rs2);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, FPURegister rd,
                        FPURegister rs1, Register rs2);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, Register rd,
                        FPURegister rs1, Register rs2);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, Register rd,
                        FPURegister rs1, FPURegister rs2);

  // Labels.
  void print(const Label* L);
  void bind_to(Label* L, int pos);
  void next(Label* L, bool is_internal);

  // One trampoline consists of:
  // - space for trampoline slots,
  // - space for labels.
  //
  // Space for trampoline slots is equal to slot_count * 2 * kInstrSize.
  // Space for trampoline slots precedes space for labels. Each label is of one
  // instruction size, so total amount for labels is equal to
  // label_count *  kInstrSize.
  class Trampoline {
   public:
    Trampoline() {
      start_ = 0;
      next_slot_ = 0;
      free_slot_count_ = 0;
      end_ = 0;
    }
    Trampoline(int start, int slot_count) {
      start_ = start;
      next_slot_ = start;
      free_slot_count_ = slot_count;
      end_ = start + slot_count * kTrampolineSlotsSize;
    }
    int start() { return start_; }
    int end() { return end_; }
    int take_slot() {
      int trampoline_slot = kInvalidSlotPos;
      if (free_slot_count_ <= 0) {
        // We have run out of space on trampolines.
        // Make sure we fail in debug mode, so we become aware of each case
        // when this happens.
        DCHECK(0);
        // Internal exception will be caught.
      } else {
        trampoline_slot = next_slot_;
        free_slot_count_--;
        next_slot_ += kTrampolineSlotsSize;
      }
      return trampoline_slot;
    }

   private:
    int start_;
    int end_;
    int next_slot_;
    int free_slot_count_;
  };

  int32_t get_trampoline_entry(int32_t pos);
  int unbound_labels_count_;
  // After trampoline is emitted, long branches are used in generated code for
  // the forward branches whose target offsets could be beyond reach of branch
  // instruction. We use this information to trigger different mode of
  // branch instruction generation, where we use jump instructions rather
  // than regular branch instructions.
  bool trampoline_emitted_;
  static constexpr int kInvalidSlotPos = -1;

  // Internal reference positions, required for unbounded internal reference
  // labels.
  std::set<int64_t> internal_reference_positions_;
  bool is_internal_reference(Label* L) {
    return internal_reference_positions_.find(L->pos()) !=
           internal_reference_positions_.end();
  }

  void EmittedCompactBranchInstruction() { prev_instr_compact_branch_ = true; }
  void ClearCompactBranchState() { prev_instr_compact_branch_ = false; }
  bool prev_instr_compact_branch_ = false;

  Trampoline trampoline_;
  bool internal_trampoline_exception_;

  RegList scratch_register_list_;

 private:
  void AllocateAndInstallRequestedHeapObjects(Isolate* isolate);

  int WriteCodeComments();

  friend class RegExpMacroAssemblerRISCV;
  friend class RelocInfo;
  friend class BlockTrampolinePoolScope;
  friend class EnsureSpace;
};

class EnsureSpace {
 public:
  explicit inline EnsureSpace(Assembler* assembler);
};

class V8_EXPORT_PRIVATE UseScratchRegisterScope {
 public:
  explicit UseScratchRegisterScope(Assembler* assembler);
  ~UseScratchRegisterScope();

  Register Acquire();
  bool hasAvailable() const;

 private:
  RegList* available_;
  RegList old_available_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_RISCV_ASSEMBLER_RISCV_H_
