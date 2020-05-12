// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_RISCV_CONSTANTS_RISCV_H_
#define V8_CODEGEN_RISCV_CONSTANTS_RISCV_H_

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/common/globals.h"

// UNIMPLEMENTED_ macro for RISCV.
#ifdef DEBUG
#define UNIMPLEMENTED_RISCV()                                              \
  v8::internal::PrintF("%s, \tline %d: \tfunction %s not implemented. \n", \
                       __FILE__, __LINE__, __func__)
#else
#define UNIMPLEMENTED_RISCV()
#endif

#define UNSUPPORTED_RISCV() v8::internal::PrintF("Unsupported instruction.\n")

enum Endianness { kLittle, kBig };

#if defined(V8_TARGET_LITTLE_ENDIAN)
static const Endianness kArchEndian = kLittle;
#elif defined(V8_TARGET_BIG_ENDIAN)
static const Endianness kArchEndian = kBig;
#else
#error Unknown endianness
#endif

// TODO(plind): consider renaming these ...
#if defined(__mips_hard_float) && __mips_hard_float != 0
// Use floating-point coprocessor instructions. This flag is raised when
// -mhard-float is passed to the compiler.
const bool IsMipsSoftFloatABI = false;
#elif defined(__mips_soft_float) && __mips_soft_float != 0
// This flag is raised when -msoft-float is passed to the compiler.
// Although FPU is a base requirement for v8, soft-float ABI is used
// on soft-float systems with FPU kernel emulation.
const bool IsMipsSoftFloatABI = true;
#else
const bool IsMipsSoftFloatABI = true;
#endif

#if defined(V8_TARGET_LITTLE_ENDIAN)
const uint32_t kMipsLwrOffset = 0;
const uint32_t kMipsLwlOffset = 3;
const uint32_t kMipsSwrOffset = 0;
const uint32_t kMipsSwlOffset = 3;
const uint32_t kMipsLdrOffset = 0;
const uint32_t kMipsLdlOffset = 7;
const uint32_t kMipsSdrOffset = 0;
const uint32_t kMipsSdlOffset = 7;
#elif defined(V8_TARGET_BIG_ENDIAN)
const uint32_t kMipsLwrOffset = 3;
const uint32_t kMipsLwlOffset = 0;
const uint32_t kMipsSwrOffset = 3;
const uint32_t kMipsSwlOffset = 0;
const uint32_t kMipsLdrOffset = 7;
const uint32_t kMipsLdlOffset = 0;
const uint32_t kMipsSdrOffset = 7;
const uint32_t kMipsSdlOffset = 0;
#else
#error Unknown endianness
#endif

#if defined(V8_TARGET_LITTLE_ENDIAN)
const uint32_t kLeastSignificantByteInInt32Offset = 0;
const uint32_t kLessSignificantWordInDoublewordOffset = 0;
#elif defined(V8_TARGET_BIG_ENDIAN)
const uint32_t kLeastSignificantByteInInt32Offset = 3;
const uint32_t kLessSignificantWordInDoublewordOffset = 4;
#else
#error Unknown endianness
#endif

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

// Defines constants and accessor classes to assemble, disassemble and
// simulate MIPS32 instructions.
//
// See: MIPS32 Architecture For Programmers
//      Volume II: The MIPS32 Instruction Set
// Try www.cs.cornell.edu/courses/cs3410/2008fa/MIPS_Vol2.pdf.

namespace v8 {
namespace internal {

// TODO(sigurds): Change this value once we use relative jumps.
constexpr size_t kMaxPCRelativeCodeRangeInMB = 0;

// -----------------------------------------------------------------------------
// Registers and FPURegisters.

// Number of general purpose registers.
const int kNumRegisters = 32;
const int kInvalidRegister = -1;

// Number of registers with pc.
const int kNumSimuRegisters = 33;

// In the simulator, the PC register is simulated as the 34th register.
const int kPCRegister = 34;

// Number coprocessor registers.
const int kNumFPURegisters = 32;
const int kInvalidFPURegister = -1;

// 'pref' instruction hints
const int32_t kPrefHintLoad = 0;
const int32_t kPrefHintStore = 1;
const int32_t kPrefHintLoadStreamed = 4;
const int32_t kPrefHintStoreStreamed = 5;
const int32_t kPrefHintLoadRetained = 6;
const int32_t kPrefHintStoreRetained = 7;
const int32_t kPrefHintWritebackInvalidate = 25;
const int32_t kPrefHintPrepareForStore = 30;

// Actual value of root register is offset from the root array's start
// to take advantage of negative displacement values.
// TODO(sigurds): Choose best value.
constexpr int kRootRegisterBias = 256;

// Helper functions for converting between register numbers and names.
class Registers {
 public:
  // Return the name of the register.
  static const char* Name(int reg);

  // Lookup the register number for the name provided.
  static int Number(const char* name);

  struct RegisterAlias {
    int reg;
    const char* name;
  };

  static const int64_t kMaxValue = 0x7fffffffffffffffl;
  static const int64_t kMinValue = 0x8000000000000000l;

 private:
  static const char* names_[kNumSimuRegisters];
  static const RegisterAlias aliases_[];
};

// Helper functions for converting between register numbers and names.
class FPURegisters {
 public:
  // Return the name of the register.
  static const char* Name(int reg);

  // Lookup the register number for the name provided.
  static int Number(const char* name);

  struct RegisterAlias {
    int creg;
    const char* name;
  };

 private:
  static const char* names_[kNumFPURegisters];
  static const RegisterAlias aliases_[];
};

// -----------------------------------------------------------------------------
// Instructions encoding constants.

// On RISCV all instructions are 32 bits.
using Instr = int32_t;

// Special Software Interrupt codes when used in the presence of the MIPS
// simulator.
enum SoftwareInterruptCodes {
  // Transition to C code.
  call_rt_redirected = 0xfffff
};

// On MIPS Simulator breakpoints can have different codes:
// - Breaks between 0 and kMaxWatchpointCode are treated as simple watchpoints,
//   the simulator will run through them and print the registers.
// - Breaks between kMaxWatchpointCode and kMaxStopCode are treated as stop()
//   instructions (see Assembler::stop()).
// - Breaks larger than kMaxStopCode are simple breaks, dropping you into the
//   debugger.
const uint32_t kMaxWatchpointCode = 31;
const uint32_t kMaxStopCode = 127;
STATIC_ASSERT(kMaxWatchpointCode < kMaxStopCode);

// ----- Fields offset and length.
// RISCV constants
const int kBaseOpcodeShift = 0;
const int kBaseOpcodeBits = 7;
const int kFunct7Shift = 25;
const int kFunct7Bits = 7;
const int kFunct5Shift = 27;
const int kFunct5Bits = 5;
const int kFunct3Shift = 12;
const int kFunct3Bits = 3;
const int kFunct2Shift = 25;
const int kFunct2Bits = 2;
const int kRs1Shift = 15;
const int kRs1Bits = 5;
const int kRs2Shift = 20;
const int kRs2Bits = 5;
const int kRs3Shift = 27;
const int kRs3Bits = 5;
const int RV_kRdShift = 7;
const int RV_kRdBits = 5;
const int kRlShift = 25;
const int kAqShift = 26;
const int kImm12Shift = 20;
const int kImm12Bits = 12;
const int kShamtShift = 20;
const int kShamtBits = 5;
const int kShamtWShift = 20;
const int kShamtWBits = 6;
const int kArithShiftShift = 30;
const int kImm20Shift = 12;
const int kImm20Bits = 20;
const int kCsrShift = 20;
const int kCsrBits = 12;
const int kMemOrderBits = 4;
const int kPredOrderShift = 24;
const int kSuccOrderShift = 20;

// RISCV Instruction bit masks
const int kBaseOpcodeMask = ((1 << kBaseOpcodeBits) - 1) << kBaseOpcodeShift;
const int kFunct3Mask = ((1 << kFunct3Bits) - 1) << kFunct3Shift;
const int kFunct5Mask = ((1 << kFunct5Bits) - 1) << kFunct5Shift;
const int kFunct7Mask = ((1 << kFunct7Bits) - 1) << kFunct7Shift;
const int kFunct2Mask = 0b11 << kFunct7Shift;
const int kRTypeMask = kBaseOpcodeMask | kFunct3Mask | kFunct7Mask;
const int kRATypeMask = kBaseOpcodeMask | kFunct3Mask | kFunct5Mask;
const int kRFPTypeMask = kBaseOpcodeMask | kFunct7Mask;
const int kR4TypeMask = kBaseOpcodeMask | kFunct3Mask | kFunct2Mask;
const int kITypeMask = kBaseOpcodeMask | kFunct3Mask;
const int kSTypeMask = kBaseOpcodeMask | kFunct3Mask;
const int kBTypeMask = kBaseOpcodeMask | kFunct3Mask;
const int kUTypeMask = kBaseOpcodeMask;
const int kJTypeMask = kBaseOpcodeMask;
const int kRs1FieldMask = ((1 << kRs1Bits) - 1) << kRs1Shift;
const int kRs2FieldMask = ((1 << kRs2Bits) - 1) << kRs2Shift;
const int kRs3FieldMask = ((1 << kRs3Bits) - 1) << kRs3Shift;
const int RV_kRdFieldMask = ((1 << RV_kRdBits) - 1) << RV_kRdShift;
const int kBImm12Mask = kFunct7Mask | RV_kRdFieldMask;
const int kImm20Mask = ((1 << kImm20Bits) - 1) << kImm20Shift;

// RISCV CSR related bit mask and shift
const int kFcsrFlagsBits = 5;
const int kFcsrFlagsMask = (1 << kFcsrFlagsBits) - 1;
const int kFcsrFrmBits = 3;
const int kFcsrFrmShift = kFcsrFlagsBits;
const int kFcsrFrmMask = ((1 << kFcsrFrmBits) - 1) << kFcsrFrmShift;
const int kFcsrBits = kFcsrFlagsBits + kFcsrFrmBits;
const int kFcsrMask = kFcsrFlagsMask | kFcsrFrmMask;

// Original MIPS constants
// FIXME (RISCV): to be cleaned up
const int kOpcodeShift = 26;
const int kImm16Shift = 0;
const int kImm16Bits = 16;
const int kImm26Shift = 0;
const int kImm26Bits = 26;
const int kImm28Shift = 0;
const int kImm28Bits = 28;
const int kImm16Mask = ((1 << kImm16Bits) - 1) << kImm16Shift;
const int kImm26Mask = ((1 << kImm26Bits) - 1) << kImm26Shift;
const int kImm28Mask = ((1 << kImm28Bits) - 1) << kImm28Shift;
// end of FIXME (RISCV): to be cleaned up

// ----- RISCV Base Opcodes

enum BaseOpcode : uint32_t {

};

// ----- RISC-V Opcodes and Function Fields.
enum Opcode : uint32_t {
  LOAD = 0b0000011,      // I form: LB LH LW LBU LHU
  LOAD_FP = 0b0000111,   // I form: FLW FLD FLQ
  MISC_MEM = 0b0001111,  // I special form: FENCE FENCE.I
  OP_IMM = 0b0010011,    // I form: ADDI SLTI SLTIU XORI ORI ANDI SLLI SRLI SARI
  // Note: SLLI/SRLI/SRAI I form first, then func3 001/101 => R type
  RV_AUIPC = 0b0010111,   // U form: AUIPC
  OP_IMM_32 = 0b0011011,  // I form: ADDIW SLLIW SRLIW SRAIW
  // Note:  SRLIW SRAIW I form first, then func3 101 special shift encoding
  STORE = 0b0100011,     // S form: SB SH SW SD
  STORE_FP = 0b0100111,  // S form: FSW FSD FSQ
  AMO = 0b0101111,       // R form: All A instructions
  OP = 0b0110011,      // R: ADD SUB SLL SLT SLTU XOR SRL SRA OR AND and 32M set
  RV_LUI = 0b0110111,  // U form: LUI
  OP_32 = 0b0111011,   // R: ADDW SUBW SLLW SRLW SRAW MULW DIVW DIVUW REMW REMUW
  MADD = 0b1000011,    // R4 type: FMADD.S FMADD.D FMADD.Q
  MSUB = 0b1000111,    // R4 type: FMSUB.S FMSUB.D FMSUB.Q
  NMSUB = 0b1001011,   // R4 type: FNMSUB.S FNMSUB.D FNMSUB.Q
  NMADD = 0b1001111,   // R4 type: FNMADD.S FNMADD.D FNMADD.Q
  OP_FP = 0b1010011,   // R type: Q ext
  BRANCH = 0b1100011,  // B form: BEQ BNE, BLT, BGE, BLTU BGEU
  RV_JALR = 0b1100111,  // I form: JALR
  RV_JAL = 0b1101111,   // J form: JAL
  SYSTEM = 0b1110011,   // I form: ECALL EBREAK Zicsr ext

  // Note use RO (RiscV Opcode) prefix
  // RV32I Base Instruction Set
  RO_LUI = RV_LUI,
  RO_AUIPC = RV_AUIPC,
  RO_JAL = RV_JAL,
  RO_JALR = RV_JALR | (0b000 << kFunct3Shift),
  RO_BEQ = BRANCH | (0b000 << kFunct3Shift),
  RO_BNE = BRANCH | (0b001 << kFunct3Shift),
  RO_BLT = BRANCH | (0b100 << kFunct3Shift),
  RO_BGE = BRANCH | (0b101 << kFunct3Shift),
  RO_BLTU = BRANCH | (0b110 << kFunct3Shift),
  RO_BGEU = BRANCH | (0b111 << kFunct3Shift),
  RO_LB = LOAD | (0b000 << kFunct3Shift),
  RO_LH = LOAD | (0b001 << kFunct3Shift),
  RO_LW = LOAD | (0b010 << kFunct3Shift),
  RO_LBU = LOAD | (0b100 << kFunct3Shift),
  RO_LHU = LOAD | (0b101 << kFunct3Shift),
  RO_SB = STORE | (0b000 << kFunct3Shift),
  RO_SH = STORE | (0b001 << kFunct3Shift),
  RO_SW = STORE | (0b010 << kFunct3Shift),
  RO_ADDI = OP_IMM | (0b000 << kFunct3Shift),
  RO_SLTI = OP_IMM | (0b010 << kFunct3Shift),
  RO_SLTIU = OP_IMM | (0b011 << kFunct3Shift),
  RO_XORI = OP_IMM | (0b100 << kFunct3Shift),
  RO_ORI = OP_IMM | (0b110 << kFunct3Shift),
  RO_ANDI = OP_IMM | (0b111 << kFunct3Shift),
  RO_SLLI = OP_IMM | (0b001 << kFunct3Shift),
  RO_SRLI = OP_IMM | (0b101 << kFunct3Shift),
  // RO_SRAI = OP_IMM | (0b101 << kFunct3Shift), // Same as SRLI, use func7
  RO_ADD = OP | (0b000 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SUB = OP | (0b000 << kFunct3Shift) | (0b0100000 << kFunct7Shift),
  RO_SLL = OP | (0b001 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SLT = OP | (0b010 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SLTU = OP | (0b011 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_XOR = OP | (0b100 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SRL = OP | (0b101 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SRA = OP | (0b101 << kFunct3Shift) | (0b0100000 << kFunct7Shift),
  RO_OR = OP | (0b110 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_AND = OP | (0b111 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_FENCE = MISC_MEM | (0b000 << kFunct3Shift),
  RO_ECALL = SYSTEM | (0b000 << kFunct3Shift),
  // RO_EBREAK = SYSTEM | (0b000 << kFunct3Shift), // Same as ECALL, use imm12

  // RV64I Base Instruction Set (in addition to RV32I)
  RO_LWU = LOAD | (0b110 << kFunct3Shift),
  RO_LD = LOAD | (0b011 << kFunct3Shift),
  RO_SD = STORE | (0b011 << kFunct3Shift),
  RO_ADDIW = OP_IMM_32 | (0b000 << kFunct3Shift),
  RO_SLLIW = OP_IMM_32 | (0b001 << kFunct3Shift),
  RO_SRLIW = OP_IMM_32 | (0b101 << kFunct3Shift),
  // RO_SRAIW = OP_IMM_32 | (0b101 << kFunct3Shift), // Same as SRLIW, use func7
  RO_ADDW = OP_32 | (0b000 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SUBW = OP_32 | (0b000 << kFunct3Shift) | (0b0100000 << kFunct7Shift),
  RO_SLLW = OP_32 | (0b001 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SRLW = OP_32 | (0b101 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SRAW = OP_32 | (0b101 << kFunct3Shift) | (0b0100000 << kFunct7Shift),

  // RV32/RV64 Zifencei Standard Extension
  RO_FENCE_I = MISC_MEM | (0b001 << kFunct3Shift),

  // RV32/RV64 Zicsr Standard Extension
  RO_CSRRW = SYSTEM | (0b001 << kFunct3Shift),
  RO_CSRRS = SYSTEM | (0b010 << kFunct3Shift),
  RO_CSRRC = SYSTEM | (0b011 << kFunct3Shift),
  RO_CSRRWI = SYSTEM | (0b101 << kFunct3Shift),
  RO_CSRRSI = SYSTEM | (0b110 << kFunct3Shift),
  RO_CSRRCI = SYSTEM | (0b111 << kFunct3Shift),

  // RV32M Standard Extension
  RO_MUL = OP | (0b000 << kFunct3Shift) | (0b0000001 << kFunct7Shift),
  RO_MULH = OP | (0b001 << kFunct3Shift) | (0b0000001 << kFunct7Shift),
  RO_MULHSU = OP | (0b010 << kFunct3Shift) | (0b0000001 << kFunct7Shift),
  RO_MULHU = OP | (0b011 << kFunct3Shift) | (0b0000001 << kFunct7Shift),
  RO_DIV = OP | (0b100 << kFunct3Shift) | (0b0000001 << kFunct7Shift),
  RO_DIVU = OP | (0b101 << kFunct3Shift) | (0b0000001 << kFunct7Shift),
  RO_REM = OP | (0b110 << kFunct3Shift) | (0b0000001 << kFunct7Shift),
  RO_REMU = OP | (0b111 << kFunct3Shift) | (0b0000001 << kFunct7Shift),

  // RV64M Standard Extension (in addition to RV32M)
  RO_MULW = OP_32 | (0b000 << kFunct3Shift) | (0b0000001 << kFunct7Shift),
  RO_DIVW = OP_32 | (0b100 << kFunct3Shift) | (0b0000001 << kFunct7Shift),
  RO_DIVUW = OP_32 | (0b101 << kFunct3Shift) | (0b0000001 << kFunct7Shift),
  RO_REMW = OP_32 | (0b110 << kFunct3Shift) | (0b0000001 << kFunct7Shift),
  RO_REMUW = OP_32 | (0b111 << kFunct3Shift) | (0b0000001 << kFunct7Shift),

  // RV32A Standard Extension
  RO_LR_W = AMO | (0b010 << kFunct3Shift) | (0b00010 << kFunct5Shift),
  RO_SC_W = AMO | (0b010 << kFunct3Shift) | (0b00011 << kFunct5Shift),
  RO_AMOSWAP_W = AMO | (0b010 << kFunct3Shift) | (0b00001 << kFunct5Shift),
  RO_AMOADD_W = AMO | (0b010 << kFunct3Shift) | (0b00000 << kFunct5Shift),
  RO_AMOXOR_W = AMO | (0b010 << kFunct3Shift) | (0b00100 << kFunct5Shift),
  RO_AMOAND_W = AMO | (0b010 << kFunct3Shift) | (0b01100 << kFunct5Shift),
  RO_AMOOR_W = AMO | (0b010 << kFunct3Shift) | (0b01000 << kFunct5Shift),
  RO_AMOMIN_W = AMO | (0b010 << kFunct3Shift) | (0b10000 << kFunct5Shift),
  RO_AMOMAX_W = AMO | (0b010 << kFunct3Shift) | (0b10100 << kFunct5Shift),
  RO_AMOMINU_W = AMO | (0b010 << kFunct3Shift) | (0b11000 << kFunct5Shift),
  RO_AMOMAXU_W = AMO | (0b010 << kFunct3Shift) | (0b11100 << kFunct5Shift),

  // RV64A Standard Extension (in addition to RV32A)
  RO_LR_D = AMO | (0b011 << kFunct3Shift) | (0b00010 << kFunct5Shift),
  RO_SC_D = AMO | (0b011 << kFunct3Shift) | (0b00011 << kFunct5Shift),
  RO_AMOSWAP_D = AMO | (0b011 << kFunct3Shift) | (0b00001 << kFunct5Shift),
  RO_AMOADD_D = AMO | (0b011 << kFunct3Shift) | (0b00000 << kFunct5Shift),
  RO_AMOXOR_D = AMO | (0b011 << kFunct3Shift) | (0b00100 << kFunct5Shift),
  RO_AMOAND_D = AMO | (0b011 << kFunct3Shift) | (0b01100 << kFunct5Shift),
  RO_AMOOR_D = AMO | (0b011 << kFunct3Shift) | (0b01000 << kFunct5Shift),
  RO_AMOMIN_D = AMO | (0b011 << kFunct3Shift) | (0b10000 << kFunct5Shift),
  RO_AMOMAX_D = AMO | (0b011 << kFunct3Shift) | (0b10100 << kFunct5Shift),
  RO_AMOMINU_D = AMO | (0b011 << kFunct3Shift) | (0b11000 << kFunct5Shift),
  RO_AMOMAXU_D = AMO | (0b011 << kFunct3Shift) | (0b11100 << kFunct5Shift),

  // RV32F Standard Extension
  RO_FLW = LOAD_FP | (0b010 << kFunct3Shift),
  RO_FSW = STORE_FP | (0b010 << kFunct3Shift),
  RO_FMADD_S = MADD | (0b00 << kFunct2Shift),
  RO_FMSUB_S = MSUB | (0b00 << kFunct2Shift),
  RO_FNMSUB_S = NMSUB | (0b00 << kFunct2Shift),
  RO_FNMADD_S = NMADD | (0b00 << kFunct2Shift),
  RO_FADD_S = OP_FP | (0b0000000 << kFunct7Shift),
  RO_FSUB_S = OP_FP | (0b0000100 << kFunct7Shift),
  RO_FMUL_S = OP_FP | (0b0001000 << kFunct7Shift),
  RO_FDIV_S = OP_FP | (0b0001100 << kFunct7Shift),
  RO_FSQRT_S = OP_FP | (0b0101100 << kFunct7Shift) | (0b00000 << kRs2Shift),
  RO_FSGNJ_S = OP_FP | (0b000 << kFunct3Shift) | (0b0010000 << kFunct7Shift),
  RO_FSGNJN_S = OP_FP | (0b001 << kFunct3Shift) | (0b0010000 << kFunct7Shift),
  RO_FSQNJX_S = OP_FP | (0b010 << kFunct3Shift) | (0b0010000 << kFunct7Shift),
  RO_FMIN_S = OP_FP | (0b000 << kFunct3Shift) | (0b0010100 << kFunct7Shift),
  RO_FMAX_S = OP_FP | (0b001 << kFunct3Shift) | (0b0010100 << kFunct7Shift),
  RO_FCVT_W_S = OP_FP | (0b1100000 << kFunct7Shift) | (0b00000 << kRs2Shift),
  RO_FCVT_WU_S = OP_FP | (0b1100000 << kFunct7Shift) | (0b00001 << kRs2Shift),
  RO_FMV = OP_FP | (0b1110000 << kFunct7Shift) | (0b000 << kFunct3Shift) |
           (0b00000 << kRs2Shift),
  RO_FEQ_S = OP_FP | (0b010 << kFunct3Shift) | (0b1010000 << kFunct7Shift),
  RO_FLT_S = OP_FP | (0b001 << kFunct3Shift) | (0b1010000 << kFunct7Shift),
  RO_FLE_S = OP_FP | (0b000 << kFunct3Shift) | (0b1010000 << kFunct7Shift),
  RO_FCLASS_S = OP_FP | (0b001 << kFunct3Shift) | (0b1110000 << kFunct7Shift),
  RO_FCVT_S_W = OP_FP | (0b1101000 << kFunct7Shift) | (0b00000 << kRs2Shift),
  RO_FCVT_S_WU = OP_FP | (0b1101000 << kFunct7Shift) | (0b00001 << kRs2Shift),
  RO_FMV_W_X = OP_FP | (0b000 << kFunct3Shift) | (0b1111000 << kFunct7Shift),

  // RV64F Standard Extension (in addition to RV32F)
  RO_FCVT_L_S = OP_FP | (0b1100000 << kFunct7Shift) | (0b00010 << kRs2Shift),
  RO_FCVT_LU_S = OP_FP | (0b1100000 << kFunct7Shift) | (0b00011 << kRs2Shift),
  RO_FCVT_S_L = OP_FP | (0b1101000 << kFunct7Shift) | (0b00010 << kRs2Shift),
  RO_FCVT_S_LU = OP_FP | (0b1101000 << kFunct7Shift) | (0b00011 << kRs2Shift),

  // RV32D Standard Extension
  RO_FLD = LOAD_FP | (0b011 << kFunct3Shift),
  RO_FSD = STORE_FP | (0b011 << kFunct3Shift),
  RO_FMADD_D = MADD | (0b01 << kFunct2Shift),
  RO_FMSUB_D = MSUB | (0b01 << kFunct2Shift),
  RO_FNMSUB_D = NMSUB | (0b01 << kFunct2Shift),
  RO_FNMADD_D = NMADD | (0b01 << kFunct2Shift),
  RO_FADD_D = OP_FP | (0b0000001 << kFunct7Shift),
  RO_FSUB_D = OP_FP | (0b0000101 << kFunct7Shift),
  RO_FMUL_D = OP_FP | (0b0001001 << kFunct7Shift),
  RO_FDIV_D = OP_FP | (0b0001101 << kFunct7Shift),
  RO_FSQRT_D = OP_FP | (0b0101101 << kFunct7Shift) | (0b00000 << kRs2Shift),
  RO_FSGNJ_D = OP_FP | (0b000 << kFunct3Shift) | (0b0010001 << kFunct7Shift),
  RO_FSGNJN_D = OP_FP | (0b001 << kFunct3Shift) | (0b0010001 << kFunct7Shift),
  RO_FSQNJX_D = OP_FP | (0b010 << kFunct3Shift) | (0b0010001 << kFunct7Shift),
  RO_FMIN_D = OP_FP | (0b000 << kFunct3Shift) | (0b0010101 << kFunct7Shift),
  RO_FMAX_D = OP_FP | (0b001 << kFunct3Shift) | (0b0010101 << kFunct7Shift),
  RO_FCVT_S_D = OP_FP | (0b0100000 << kFunct7Shift) | (0b00001 << kRs2Shift),
  RO_FCVT_D_S = OP_FP | (0b0100001 << kFunct7Shift) | (0b00000 << kRs2Shift),
  RO_FEQ_D = OP_FP | (0b010 << kFunct3Shift) | (0b1010001 << kFunct7Shift),
  RO_FLT_D = OP_FP | (0b001 << kFunct3Shift) | (0b1010001 << kFunct7Shift),
  RO_FLE_D = OP_FP | (0b000 << kFunct3Shift) | (0b1010001 << kFunct7Shift),
  RO_FCLASS_D = OP_FP | (0b001 << kFunct3Shift) | (0b1110001 << kFunct7Shift) |
                (0b00000 << kRs2Shift),
  RO_FCVT_W_D = OP_FP | (0b1100001 << kFunct7Shift) | (0b00000 << kRs2Shift),
  RO_FCVT_WU_D = OP_FP | (0b1100001 << kFunct7Shift) | (0b00001 << kRs2Shift),
  RO_FCVT_D_W = OP_FP | (0b1101001 << kFunct7Shift) | (0b00000 << kRs2Shift),
  RO_FCVT_D_WU = OP_FP | (0b1101001 << kFunct7Shift) | (0b00001 << kRs2Shift),

  // RV64D Standard Extension (in addition to RV32D)
  RO_FCVT_L_D = OP_FP | (0b1100001 << kFunct7Shift) | (0b00010 << kRs2Shift),
  RO_FCVT_LU_D = OP_FP | (0b1100001 << kFunct7Shift) | (0b00011 << kRs2Shift),
  RO_FMV_X_D = OP_FP | (0b000 << kFunct3Shift) | (0b1110001 << kFunct7Shift) |
               (0b00000 << kRs2Shift),
  RO_FCVT_D_L = OP_FP | (0b1101001 << kFunct7Shift) | (0b00010 << kRs2Shift),
  RO_FCVT_D_LU = OP_FP | (0b1101001 << kFunct7Shift) | (0b00011 << kRs2Shift),
  RO_FMV_D_X = OP_FP | (0b000 << kFunct3Shift) | (0b1111001 << kFunct7Shift) |
               (0b00000 << kRs2Shift),

  // Original MIPS opcodes
  // FIXME (RISCV): to be cleaned up
  SPECIAL = 0U << kOpcodeShift,
};

// FIXME (RISCV): to be cleaned up
enum SecondaryField : uint32_t {
  // SPECIAL Encoding of Function Field.
  SLL = ((0U << 3) + 0),
  BREAK = ((1U << 3) + 5),
};

// ----- Emulated conditions.
// On MIPS we use this enum to abstract from conditional branch instructions.
// The 'U' prefix is used to specify unsigned comparisons.
// Opposite conditions must be paired as odd/even numbers
// because 'NegateCondition' function flips LSB to negate condition.
enum Condition {  // Any value < 0 is considered no_condition.
  kNoCondition = -1,
  overflow = 0,
  no_overflow = 1,
  Uless = 2,
  Ugreater_equal = 3,
  Uless_equal = 4,
  Ugreater = 5,
  equal = 6,
  not_equal = 7,  // Unordered or Not Equal.
  negative = 8,
  positive = 9,
  parity_even = 10,
  parity_odd = 11,
  less = 12,
  greater_equal = 13,
  less_equal = 14,
  greater = 15,
  cc_always = 18,

  // Aliases.
  eq = equal,
  ne = not_equal,
  ge = greater_equal,
  lt = less,
  gt = greater,
  le = less_equal,
  al = cc_always,
  ult = Uless,
  uge = Ugreater_equal,
  ule = Uless_equal,
  ugt = Ugreater,
};

// Returns the equivalent of !cc.
// Negation of the default kNoCondition (-1) results in a non-default
// no_condition value (-2). As long as tests for no_condition check
// for condition < 0, this will work as expected.
inline Condition NegateCondition(Condition cc) {
  DCHECK(cc != cc_always);
  return static_cast<Condition>(cc ^ 1);
}

inline Condition NegateFpuCondition(Condition cc) {
  DCHECK(cc != cc_always);
  switch (cc) {
    case ult:
      return ge;
    case ugt:
      return le;
    case uge:
      return lt;
    case ule:
      return gt;
    case lt:
      return uge;
    case gt:
      return ule;
    case ge:
      return ult;
    case le:
      return ugt;
    case eq:
      return ne;
    case ne:
      return eq;
    default:
      return cc;
  }
}

// ----- Coprocessor conditions.
enum FPUCondition {
  kNoFPUCondition = -1,
  EQ = 0x02,  // Equal.
  LT = 0x04,  // Ordered and Less Than
  LE = 0x06,  // Ordered and Less Than or Equal
};

enum CheckForInexactConversion {
  kCheckForInexactConversion,
  kDontCheckForInexactConversion
};

enum class MaxMinKind : int { kMin = 0, kMax = 1 };

// ----------------------------------------------------------------------------
// RISCV flags

enum ControlStatusReg {
  csr_fflags = 0x001,   // Floating-Point Accrued Exceptions (RW)
  csr_frm = 0x002,      // Floating-Point Dynamic Rounding Mode (RW)
  csr_fcsr = 0x003,     // Floating-Point Control and Status Register (RW)
  csr_cycle = 0xc00,    // Cycle counter for RDCYCLE instruction (RO)
  csr_time = 0xc01,     // Timer for RDTIME instruction (RO)
  csr_instret = 0xc02,  // Insns-retired counter for RDINSTRET instruction (RO)
  csr_cycleh = 0xc80,   // Upper 32 bits of cycle, RV32I only (RO)
  csr_timeh = 0xc81,    // Upper 32 bits of time, RV32I only (RO)
  csr_instreth = 0xc82  // Upper 32 bits of instret, RV32I only (RO)
};

enum FFlagsMask {
  kInvalidOperation = 0b10000,  // NV: Invalid
  kDivideByZero = 0b1000,       // DZ:  Divide by Zero
  kOverflow = 0b100,            // OF: Overflow
  kUnderflow = 0b10,            // UF: Underflow
  kInexact = 0b1                // NX:  Inexact
};

enum RoundingMode {
  RNE = 0b000,  // Round to Nearest, ties to Even
  RTZ = 0b001,  // Round towards Zero
  RDN = 0b010,  // Round Down (towards -infinity)
  RUP = 0b011,  // Round Up (towards +infinity)
  RMM = 0b100,  // Round to Nearest, tiest to Max Magnitude
  DYN = 0b111   // In instruction's rm field, selects dynamic rounding mode;
                // In Rounding Mode register, Invalid
};

enum MemoryOdering {
  PSI = 0b1000,  // PI or SI
  PSO = 0b0100,  // PO or SO
  PSR = 0b0010,  // PR or SR
  PSW = 0b0001   // PW or SW
};

enum FClassFlag {
  kNegativeInfinity = 1,
  kNegativeNormalNumber = 1 << 1,
  kNegativeSubnormalNumber = 1 << 2,
  kNegativeZero = 1 << 3,
  kPositiveZero = 1 << 4,
  kPositiveSubnormalNumber = 1 << 5,
  kPositiveNormalNumber = 1 << 6,
  kPositiveInfinity = 1 << 7,
  kSignalingNaN = 1 << 8,
  kQuietNaN = 1 << 9
};

// -----------------------------------------------------------------------------
// Hints.

// Branch hints are not used on the MIPS.  They are defined so that they can
// appear in shared function signatures, but will be ignored in MIPS
// implementations.
enum Hint { no_hint = 0 };

inline Hint NegateHint(Hint hint) { return no_hint; }

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.
// These constants are declared in assembler-mips.cc, as they use named
// registers and other constants.

// An ECALL instruction, used for redirected real time call
const Instr rtCallRedirInstr = SYSTEM; // All other bits are 0s

constexpr uint8_t kInstrSize = 4;
constexpr uint8_t kInstrSizeLog2 = 2;

class InstructionBase {
 public:
  enum {
    // On MIPS PC cannot actually be directly accessed. We behave as if PC was
    // always the value of the current instruction being executed.
    kPCReadOffset = 0
  };

  // Instruction type.
  enum Type {
    kRType,
    kR4Type,  // Special R4 for Q extension
    kIType,
    kSType,
    kBType,
    kUType,
    kJType,
    kUnsupported = -1
  };

  // Get the raw instruction bits.
  inline Instr InstructionBits() const {
    return *reinterpret_cast<const Instr*>(this);
  }

  // Set the raw instruction bits to value.
  inline void SetInstructionBits(Instr value) {
    *reinterpret_cast<Instr*>(this) = value;
  }

  // Read one particular bit out of the instruction bits.
  inline int Bit(int nr) const { return (InstructionBits() >> nr) & 1; }

  // Read a bit field out of the instruction bits.
  inline int Bits(int hi, int lo) const {
    return (InstructionBits() >> lo) & ((2U << (hi - lo)) - 1);
  }

  // Accessors for the different named fields used in the RISC-V encoding.
  inline Opcode BaseOpcodeValue() const {
    return static_cast<Opcode>(
        Bits(kBaseOpcodeShift + kBaseOpcodeBits - 1, kBaseOpcodeShift));
  }

  // Return the fields at their original place in the instruction encoding.
  inline Opcode BaseOpcodeFieldRaw() const {
    return static_cast<Opcode>(InstructionBits() & kBaseOpcodeMask);
  }

  // Safe to call within R-type instructions
  inline int Funct7FieldRaw() const { return InstructionBits() & kFunct7Mask; }

  // Safe to call within R-, I-, S-, or B-type instructions
  inline int Funct3FieldRaw() const { return InstructionBits() & kFunct3Mask; }

  // Safe to call within R-, I-, S-, or B-type instructions
  inline int Rs1FieldRawNoAssert() const {
    return InstructionBits() & kRs1FieldMask;
  }

  // Safe to call within R-, S-, or B-type instructions
  inline int Rs2FieldRawNoAssert() const {
    return InstructionBits() & kRs2FieldMask;
  }

  // Safe to call within R4-type instructions
  inline int Rs3FieldRawNoAssert() const {
    return InstructionBits() & kRs3FieldMask;
  }

  // FIXME (RISCV): to be cleaned up
  inline int32_t ITypeBits() const { return InstructionBits() & kITypeMask; }

  // Get the encoding type of the instruction.
  inline Type InstructionType() const;

 protected:
  InstructionBase() {}
};

template <class T>
class InstructionGetters : public T {
 public:
  inline int BaseOpcode() const {
    return this->InstructionBits() & kBaseOpcodeMask;
  }

  inline int Rs1Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kRType ||
           this->InstructionType() == InstructionBase::kR4Type ||
           this->InstructionType() == InstructionBase::kIType ||
           this->InstructionType() == InstructionBase::kSType ||
           this->InstructionType() == InstructionBase::kBType);
    return this->Bits(kRs1Shift + kRs1Bits - 1, kRs1Shift);
  }

  inline int Rs2Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kRType ||
           this->InstructionType() == InstructionBase::kR4Type ||
           this->InstructionType() == InstructionBase::kSType ||
           this->InstructionType() == InstructionBase::kBType);
    return this->Bits(kRs2Shift + kRs2Bits - 1, kRs2Shift);
  }

  inline int Rs3Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kR4Type);
    return this->Bits(kRs3Shift + kRs3Bits - 1, kRs3Shift);
  }

  inline int RV_RdValue() const {
    DCHECK(this->InstructionType() == InstructionBase::kRType ||
           this->InstructionType() == InstructionBase::kR4Type ||
           this->InstructionType() == InstructionBase::kIType ||
           this->InstructionType() == InstructionBase::kUType ||
           this->InstructionType() == InstructionBase::kJType);
    return this->Bits(RV_kRdShift + RV_kRdBits - 1, RV_kRdShift);
  }

  inline int Funct7Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kRType);
    return this->Bits(kFunct7Shift + kFunct7Bits - 1, kFunct7Shift);
  }

  inline int Funct3Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kRType ||
           this->InstructionType() == InstructionBase::kIType ||
           this->InstructionType() == InstructionBase::kSType ||
           this->InstructionType() == InstructionBase::kBType);
    return this->Bits(kFunct3Shift + kFunct3Bits - 1, kFunct3Shift);
  }

  inline int Funct5Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kRType &&
           this->BaseOpcode() == OP_FP);
    return this->Bits(kFunct5Shift + kFunct5Bits - 1, kFunct5Shift);
  }

  inline int CsrValue() const {
    DCHECK(this->InstructionType() == InstructionBase::kIType &&
           this->BaseOpcode() == SYSTEM);
    return (this->Bits(kCsrShift + kCsrBits - 1, kCsrShift));
  }

  inline int RoundMode() const {
    DCHECK((this->InstructionType() == InstructionBase::kRType ||
            this->InstructionType() == InstructionBase::kR4Type) &&
           this->BaseOpcode() == OP_FP);
    return this->Bits(kFunct3Shift + kFunct3Bits - 1, kFunct3Shift);
  }

  inline int MemoryOrder(bool is_pred) const {
    DCHECK((this->InstructionType() == InstructionBase::kIType &&
            this->BaseOpcode() == MISC_MEM));
    if (is_pred) {
      return this->Bits(kPredOrderShift + kMemOrderBits - 1, kPredOrderShift);
    } else {
      return this->Bits(kSuccOrderShift + kMemOrderBits - 1, kSuccOrderShift);
    }
  }

  inline int Imm12Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kIType);
    int Value = this->Bits(kImm12Shift + kImm12Bits - 1, kImm12Shift);
    return Value << 20 >> 20;
  }

  inline int32_t Imm12SExtValue() const {
    int32_t Value = this->Imm12Value() << 20 >> 20;
    return Value;
  }

  inline int BranchOffset() const {
    DCHECK(this->InstructionType() == InstructionBase::kBType);
    // | imm[12|10:5] | rs2 | rs1 | funct3 | imm[4:1|11] | opcode |
    //  31          25                      11          7
    uint32_t Bits = this->InstructionBits();
    int16_t imm13 = ((Bits & 0xf00) >> 7) | ((Bits & 0x7e000000) >> 20) |
                    ((Bits & 0x80) << 4) | ((Bits & 0x80000000) >> 19);
    return imm13 << 19 >> 19;
  }

  inline int StoreOffset() const {
    DCHECK(this->InstructionType() == InstructionBase::kSType);
    // | imm[11:5] | rs2 | rs1 | funct3 | imm[4:0] | opcode |
    //  31       25                      11       7
    uint32_t Bits = this->InstructionBits();
    int16_t imm12 = ((Bits & 0xf80) >> 7) | ((Bits & 0xfe000000) >> 20);
    return imm12 << 20 >> 20;
  }

  inline int Imm20UValue() const {
    DCHECK(this->InstructionType() == InstructionBase::kUType);
    // | imm[31:12] | rd | opcode |
    //  31        12
    int32_t Bits = this->InstructionBits();
    return Bits >> 12;
  }

  inline int Imm20JValue() const {
    DCHECK(this->InstructionType() == InstructionBase::kJType);
    // | imm[20|10:1|11|19:12] | rd | opcode |
    //  31                   12
    uint32_t Bits = this->InstructionBits();
    int32_t imm20 = ((Bits & 0x7fe00000) >> 20) | ((Bits & 0x100000) >> 9) |
                    (Bits & 0xff000) | ((Bits & 0x80000000) >> 11);
    return imm20 << 11 >> 11;
  }

  inline bool IsArithShift() const {
    // Valid only for right shift operations
    DCHECK((this->BaseOpcode() == OP || this->BaseOpcode() == OP_32 ||
            this->BaseOpcode() == OP_IMM || this->BaseOpcode() == OP_IMM_32) &&
           this->Funct3Value() == 0b101);
    return this->InstructionBits() & 0x40000000;
  }

  inline int Shamt() const {
    // Valid only for shift instructions (SLLI, SRLI, SRAI)
    DCHECK((this->InstructionBits() & kBaseOpcodeMask) == OP_IMM &&
           (this->Funct3Value() == 0b001 || this->Funct3Value() == 0b101));
    // | 0A0000 | shamt | rs1 | funct3 | rd | opcode |
    //  31       25    20
    return this->Bits(kImm12Shift + 5, kImm12Shift);
  }

  inline int Shamt32() const {
    // Valid only for shift instructions (SLLIW, SRLIW, SRAIW)
    DCHECK((this->InstructionBits() & kBaseOpcodeMask) == OP_IMM_32 &&
           (this->Funct3Value() == 0b001 || this->Funct3Value() == 0b101));
    // | 0A00000 | shamt | rs1 | funct3 | rd | opcode |
    //  31        24   20
    return this->Bits(kImm12Shift + 4, kImm12Shift);
  }

  inline bool AqValue() const { return this->Bits(kAqShift, kAqShift); }

  inline bool RlValue() const { return this->Bits(kRlShift, kRlShift); }

  // Original MIPS methods
  // FIXME (RISCV): to be cleaned up
  static bool IsForbiddenAfterBranchInstr(Instr instr);

  // FIXME (RISCV): to be ported
  // Say if the instruction is a break or a trap.
  bool IsTrap() const;
};

class Instruction : public InstructionGetters<InstructionBase> {
 public:
  // Instructions are read of out a code stream. The only way to get a
  // reference to an instruction is to convert a pointer. There is no way
  // to allocate or create instances of class Instruction.
  // Use the At(pc) function to create references to Instruction.
  static Instruction* At(byte* pc) {
    return reinterpret_cast<Instruction*>(pc);
  }

 private:
  // We need to prevent the creation of instances of class Instruction.
  DISALLOW_IMPLICIT_CONSTRUCTORS(Instruction);
};

// -----------------------------------------------------------------------------
// MIPS assembly various constants.

// C/C++ argument slots size.
const int kCArgSlotCount = 0;

// TODO(plind): below should be based on kPointerSize
// TODO(plind): find all usages and remove the needless instructions for n64.
const int kCArgsSlotsSize = kCArgSlotCount * kInstrSize * 2;

const int kInvalidStackOffset = -1;
const int kBranchReturnOffset = 2 * kInstrSize;

static const int kNegOffset = 0x00008000;

InstructionBase::Type InstructionBase::InstructionType() const {
  // RISCV routine
  switch (InstructionBits() & kBaseOpcodeMask) {
    case LOAD:
      return kIType;
    case LOAD_FP:
      return kIType;
    case MISC_MEM:
      return kIType;
    case OP_IMM:
      return kIType;
    case RV_AUIPC:
      return kUType;
    case OP_IMM_32:
      return kIType;
    case STORE:
      return kSType;
    case STORE_FP:
      return kSType;
    case AMO:
      return kRType;
    case OP:
      return kRType;
    case RV_LUI:
      return kUType;
    case OP_32:
      return kRType;
    case MADD:
    case MSUB:
    case NMSUB:
    case NMADD:
      return kR4Type;
    case OP_FP:
      return kRType;
    case BRANCH:
      return kBType;
    case RV_JALR:
      return kIType;
    case RV_JAL:
      return kJType;
    case SYSTEM:
      return kIType;
  }
  return kUnsupported;
}

// -----------------------------------------------------------------------------
// Instructions.

// FIXME (RISCV): to be ported
template <class P>
bool InstructionGetters<P>::IsTrap() const {
  UNIMPLEMENTED();
  /*
  if (OpcodeFieldRaw() != SPECIAL) {
    return false;
  } else {
    switch (FunctionFieldRaw()) {
      case BREAK:
      case TGE:
      case TGEU:
      case TLT:
      case TLTU:
      case TEQ:
      case TNE:
        return true;
      default:
        return false;
    }
  }
  */
}

// FIXME (RISCV): to be cleaned up
// static
template <class T>
bool InstructionGetters<T>::IsForbiddenAfterBranchInstr(Instr instr) {
  UNREACHABLE();
}
}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_RISCV_CONSTANTS_RISCV_H_
