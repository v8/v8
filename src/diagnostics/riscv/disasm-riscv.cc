// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A Disassembler object is used to disassemble a block of code instruction by
// instruction. The default implementation of the NameConverter object can be
// overriden to modify register names or to do symbol lookup on addresses.
//
// The example below will disassemble a block of code and print it to stdout.
//
//   NameConverter converter;
//   Disassembler d(converter);
//   for (byte* pc = begin; pc < end;) {
//     v8::internal::EmbeddedVector<char, 256> buffer;
//     byte* prev_pc = pc;
//     pc += d.InstructionDecode(buffer, pc);
//     printf("%p    %08x      %s\n",
//            prev_pc, *reinterpret_cast<int32_t*>(prev_pc), buffer);
//   }
//
// The Disassembler class also has a convenience method to disassemble a block
// of code into a FILE*, meaning that the above functionality could also be
// achieved by just calling Disassembler::Disassemble(stdout, begin, end);

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if V8_TARGET_ARCH_RISCV

#include "src/base/platform/platform.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/riscv/constants-riscv.h"
#include "src/diagnostics/disasm.h"

namespace v8 {
namespace internal {

//------------------------------------------------------------------------------

// Decoder decodes and disassembles instructions into an output buffer.
// It uses the converter to convert register names and call destinations into
// more informative description.
class Decoder {
 public:
  Decoder(const disasm::NameConverter& converter,
          v8::internal::Vector<char> out_buffer)
      : converter_(converter), out_buffer_(out_buffer), out_buffer_pos_(0) {
    out_buffer_[out_buffer_pos_] = '\0';
  }

  ~Decoder() {}

  // Writes one disassembled instruction into 'buffer' (0-terminated).
  // Returns the length of the disassembled machine instruction in bytes.
  int InstructionDecode(byte* instruction);

 private:
  // Bottleneck functions to print into the out_buffer.
  void PrintChar(const char ch);
  void Print(const char* str);

  // Printing of common values.
  void PrintRegister(int reg);
  void PrintFPURegister(int freg);
  void PrintFPUStatusRegister(int freg);
  void PrintRs1(Instruction* instr);
  void PrintRs2(Instruction* instr);
  void PrintRd(Instruction* instr);
  void PrintVs1(Instruction* instr);
  void PrintFRs1(Instruction* instr);
  void PrintFRs2(Instruction* instr);
  void PrintFRs3(Instruction* instr);
  void PrintFRd(Instruction* instr);
  void PrintImm12(Instruction* instr);
  void PrintImm12X(Instruction* instr);
  void PrintImm20U(Instruction* instr);
  void PrintImm20J(Instruction* instr);
  void PrintShamt(Instruction* instr);
  void PrintShamt32(Instruction* instr);
  void PrintAcquireRelease(Instruction* instr);
  void PrintBranchOffset(Instruction* instr);
  void PrintStoreOffset(Instruction* instr);
  void PrintCSRReg(Instruction* instr);
  void PrintRoundingMode(Instruction* instr);
  void PrintMemoryOrder(Instruction* instr, bool is_pred);

  // Each of these functions decodes one particular instruction type.
  void DecodeRType(Instruction* instr);
  void DecodeR4Type(Instruction* instr);
  void DecodeRAType(Instruction* instr);
  void DecodeRFPType(Instruction* instr);
  void DecodeIType(Instruction* instr);
  void DecodeSType(Instruction* instr);
  void DecodeBType(Instruction* instr);
  void DecodeUType(Instruction* instr);
  void DecodeJType(Instruction* instr);

  // Printing of instruction name.
  void PrintInstructionName(Instruction* instr);

  // Handle formatting of instructions and their options.
  int FormatRegister(Instruction* instr, const char* option);
  int FormatFPURegisterOrRoundMode(Instruction* instr, const char* option);
  int FormatOption(Instruction* instr, const char* option);
  void Format(Instruction* instr, const char* format);
  void Unknown(Instruction* instr);

  const disasm::NameConverter& converter_;
  v8::internal::Vector<char> out_buffer_;
  int out_buffer_pos_;

  DISALLOW_COPY_AND_ASSIGN(Decoder);
};

// Support for assertions in the Decoder formatting functions.
#define STRING_STARTS_WITH(string, compare_string) \
  (strncmp(string, compare_string, strlen(compare_string)) == 0)

// Append the ch to the output buffer.
void Decoder::PrintChar(const char ch) { out_buffer_[out_buffer_pos_++] = ch; }

// Append the str to the output buffer.
void Decoder::Print(const char* str) {
  char cur = *str++;
  while (cur != '\0' && (out_buffer_pos_ < (out_buffer_.length() - 1))) {
    PrintChar(cur);
    cur = *str++;
  }
  out_buffer_[out_buffer_pos_] = 0;
}

// Print the register name according to the active name converter.
void Decoder::PrintRegister(int reg) {
  Print(converter_.NameOfCPURegister(reg));
}

void Decoder::PrintRs1(Instruction* instr) {
  int reg = instr->Rs1Value();
  PrintRegister(reg);
}

void Decoder::PrintRs2(Instruction* instr) {
  int reg = instr->Rs2Value();
  PrintRegister(reg);
}

void Decoder::PrintRd(Instruction* instr) {
  int reg = instr->RV_RdValue();
  PrintRegister(reg);
}

void Decoder::PrintVs1(Instruction* instr) {
  int val = instr->Rs1Value();
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "0x%x", val);
}

// Print the FPUregister name according to the active name converter.
void Decoder::PrintFPURegister(int freg) {
  Print(converter_.NameOfXMMRegister(freg));
}

void Decoder::PrintFRs1(Instruction* instr) {
  int reg = instr->Rs1Value();
  PrintFPURegister(reg);
}

void Decoder::PrintFRs2(Instruction* instr) {
  int reg = instr->Rs2Value();
  PrintFPURegister(reg);
}

void Decoder::PrintFRs3(Instruction* instr) {
  int reg = instr->Rs3Value();
  PrintFPURegister(reg);
}

void Decoder::PrintFRd(Instruction* instr) {
  int reg = instr->RV_RdValue();
  PrintFPURegister(reg);
}

void Decoder::PrintImm12X(Instruction* instr) {
  int32_t imm = instr->Imm12Value();
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "0x%x", imm);
}

void Decoder::PrintImm12(Instruction* instr) {
  int32_t imm = instr->Imm12Value();
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "%d", imm);
}

void Decoder::PrintBranchOffset(Instruction* instr) {
  int32_t imm = instr->BranchOffset();
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "%d", imm);
}

void Decoder::PrintStoreOffset(Instruction* instr) {
  int32_t imm = instr->StoreOffset();
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "%d", imm);
}

void Decoder::PrintImm20U(Instruction* instr) {
  int32_t imm = instr->Imm20UValue();
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "0x%x", imm);
}

void Decoder::PrintImm20J(Instruction* instr) {
  int32_t imm = instr->Imm20JValue();
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "%d", imm);
}

void Decoder::PrintShamt(Instruction* instr) {
  int32_t imm = instr->Shamt();
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "%d", imm);
}

void Decoder::PrintShamt32(Instruction* instr) {
  int32_t imm = instr->Shamt32();
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "%d", imm);
}

void Decoder::PrintAcquireRelease(Instruction* instr) {
  bool aq = instr->AqValue();
  bool rl = instr->RlValue();
  if (aq || rl) {
    out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, ".");
  }
  if (aq) {
    out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "aq");
  }
  if (rl) {
    out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "rl");
  }
}

void Decoder::PrintCSRReg(Instruction* instr) {
  int32_t csr_reg = instr->CsrValue();
  std::string s;
  switch (csr_reg) {
    case csr_fflags:  // Floating-Point Accrued Exceptions (RW)
      s = "csr_fflags";
      break;
    case csr_frm:  // Floating-Point Dynamic Rounding Mode (RW)
      s = "csr_frm";
      break;
    case csr_fcsr:  // Floating-Point Control and Status Register (RW)
      s = "csr_fcsr";
      break;
    case csr_cycle:
      s = "csr_cycle";
      break;
    case csr_time:
      s = "csr_time";
      break;
    case csr_instret:
      s = "csr_instret";
      break;
    case csr_cycleh:
      s = "csr_cycleh";
      break;
    case csr_timeh:
      s = "csr_timeh";
      break;
    case csr_instreth:
      s = "csr_instreth";
      break;
    default:
      UNREACHABLE();
  }
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "%s", s.c_str());
}

void Decoder::PrintRoundingMode(Instruction* instr) {
  int frm = instr->RoundMode();
  std::string s;
  switch (frm) {
    case RNE:
      s = "RNE";
      break;
    case RTZ:
      s = "RTZ";
      break;
    case RDN:
      s = "RDN";
      break;
    case RUP:
      s = "RUP";
      break;
    case RMM:
      s = "RMM";
      break;
    case DYN:
      s = "DYN";
      break;
    default:
      UNREACHABLE();
  }
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "%s", s.c_str());
}

void Decoder::PrintMemoryOrder(Instruction* instr, bool is_pred) {
  int memOrder = instr->MemoryOrder(is_pred);
  std::string s;
  if ((memOrder & PSI) == PSI) {
    s += "i";
  }
  if ((memOrder & PSO) == PSO) {
    s += "o";
  }
  if ((memOrder & PSR) == PSR) {
    s += "r";
  }
  if ((memOrder & PSW) == PSW) {
    s += "w";
  }
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "%s", s.c_str());
}

// Printing of instruction name.
void Decoder::PrintInstructionName(Instruction* instr) {}

// Handle all register based formatting in this function to reduce the
// complexity of FormatOption.
int Decoder::FormatRegister(Instruction* instr, const char* format) {
  DCHECK_EQ(format[0], 'r');
  if (format[1] == 's') {  // 'rs[12]: Rs register.
    if (format[2] == '1') {
      int reg = instr->Rs1Value();
      PrintRegister(reg);
      return 3;
    } else if (format[2] == '2') {
      int reg = instr->Rs2Value();
      PrintRegister(reg);
      return 3;
    }
    UNREACHABLE();
  } else if (format[1] == 'd') {  // 'rd: rd register.
    int reg = instr->RV_RdValue();
    PrintRegister(reg);
    return 2;
  }
  UNREACHABLE();
}

// Handle all FPUregister based formatting in this function to reduce the
// complexity of FormatOption.
int Decoder::FormatFPURegisterOrRoundMode(Instruction* instr,
                                          const char* format) {
  DCHECK_EQ(format[0], 'f');
  if (format[1] == 's') {  // 'fs[1-3]: Rs register.
    if (format[2] == '1') {
      int reg = instr->Rs1Value();
      PrintFPURegister(reg);
      return 3;
    } else if (format[2] == '2') {
      int reg = instr->Rs2Value();
      PrintFPURegister(reg);
      return 3;
    } else if (format[2] == '3') {
      int reg = instr->Rs3Value();
      PrintFPURegister(reg);
      return 3;
    }
    UNREACHABLE();
  } else if (format[1] == 'd') {  // 'fd: fd register.
    int reg = instr->RV_RdValue();
    PrintFPURegister(reg);
    return 2;
  } else if (format[1] == 'r') {  // 'frm
    DCHECK(STRING_STARTS_WITH(format, "frm"));
    PrintRoundingMode(instr);
    return 3;
  }
  UNREACHABLE();
}

// FormatOption takes a formatting string and interprets it based on
// the current instructions. The format string points to the first
// character of the option string (the option escape has already been
// consumed by the caller.)  FormatOption returns the number of
// characters that were consumed from the formatting string.
int Decoder::FormatOption(Instruction* instr, const char* format) {
  switch (format[0]) {
    case 'c': {  // `csr: CSR registers
      if (format[1] == 's') {
        if (format[2] == 'r') {
          PrintCSRReg(instr);
          return 3;
        }
      }
      UNREACHABLE();
    }
    case 'i': {  // 'imm12, 'imm12x, 'imm20U, or 'imm20J: Immediates.
      if (format[3] == '1') {
        if (format[4] == '2') {
          DCHECK(STRING_STARTS_WITH(format, "imm12"));
          if (format[5] == 'x') {
            PrintImm12X(instr);
            return 6;
          }
          PrintImm12(instr);
          return 5;
        }
      } else if (format[3] == '2' && format[4] == '0') {
        DCHECK(STRING_STARTS_WITH(format, "imm20"));
        switch (format[5]) {
          case 'U':
            DCHECK(STRING_STARTS_WITH(format, "imm20U"));
            PrintImm20U(instr);
            break;
          case 'J':
            DCHECK(STRING_STARTS_WITH(format, "imm20J"));
            PrintImm20J(instr);
            break;
        }
        return 6;
      }
      UNREACHABLE();
    }
    case 'o': {  // 'offB or 'offS: Offsets.
      if (format[3] == 'B') {
        DCHECK(STRING_STARTS_WITH(format, "offB"));
        PrintBranchOffset(instr);
        return 4;
      } else if (format[3] == 'S') {
        DCHECK(STRING_STARTS_WITH(format, "offS"));
        PrintStoreOffset(instr);
        return 4;
      }
      UNREACHABLE();
    }
    case 'r': {  // 'r: registers.
      return FormatRegister(instr, format);
    }
    case 'f': {  // 'f: FPUregisters or `frm
      return FormatFPURegisterOrRoundMode(instr, format);
    }
    case 'a': {  // 'a: Atomic acquire and release.
      PrintAcquireRelease(instr);
      return 1;
    }
    case 'p': {  // `pre
      DCHECK(STRING_STARTS_WITH(format, "pre"));
      PrintMemoryOrder(instr, true);
      return 3;
    }
    case 's': {  // 's32 or 's64: Shift amount.
      if (format[1] == '3') {
        DCHECK(STRING_STARTS_WITH(format, "s32"));
        PrintShamt32(instr);
        return 3;
      } else if (format[1] == '6') {
        DCHECK(STRING_STARTS_WITH(format, "s64"));
        PrintShamt(instr);
        return 3;
      } else if (format[1] == 'u') {
        DCHECK(STRING_STARTS_WITH(format, "suc"));
        PrintMemoryOrder(instr, false);
        return 3;
      }
      UNREACHABLE();
    }
    case 'v': {  // 'vs1: Raw values from register fields
      DCHECK(STRING_STARTS_WITH(format, "vs1"));
      PrintVs1(instr);
      return 3;
    }
  }
  UNREACHABLE();
}

// Format takes a formatting string for a whole instruction and prints it into
// the output buffer. All escaped options are handed to FormatOption to be
// parsed further.
void Decoder::Format(Instruction* instr, const char* format) {
  char cur = *format++;
  while ((cur != 0) && (out_buffer_pos_ < (out_buffer_.length() - 1))) {
    if (cur == '\'') {  // Single quote is used as the formatting escape.
      format += FormatOption(instr, format);
    } else {
      out_buffer_[out_buffer_pos_++] = cur;
    }
    cur = *format++;
  }
  out_buffer_[out_buffer_pos_] = '\0';
}

// For currently unimplemented decodings the disassembler calls Unknown(instr)
// which will just print "unknown" of the instruction bits.
void Decoder::Unknown(Instruction* instr) { Format(instr, "unknown"); }

// RISCV Instruction Decode Routine
void Decoder::DecodeRType(Instruction* instr) {
  switch (instr->InstructionBits() & kRTypeMask) {
    case RO_ADD:
      Format(instr, "add       'rd, 'rs1, 'rs2");
      break;
    case RO_SUB:
      if (instr->Rs1Value() == zero_reg.code())
        Format(instr, "neg       'rd, rs2");
      else
        Format(instr, "sub       'rd, 'rs1, 'rs2");
      break;
    case RO_SLL:
      Format(instr, "sll       'rd, 'rs1, 'rs2");
      break;
    case RO_SLT:
      if (instr->Rs2Value() == zero_reg.code())
        Format(instr, "sltz      'rd, 'rs1");
      else if (instr->Rs1Value() == zero_reg.code())
        Format(instr, "sgtz      'rd, 'rs2");
      else
        Format(instr, "slt       'rd, 'rs1, 'rs2");
      break;
    case RO_SLTU:
      if (instr->Rs1Value() == zero_reg.code())
        Format(instr, "snez      'rd, 'rs2");
      else
        Format(instr, "sltu      'rd, 'rs1, 'rs2");
      break;
    case RO_XOR:
      Format(instr, "xor       'rd, 'rs1, 'rs2");
      break;
    case RO_SRL:
      Format(instr, "srl       'rd, 'rs1, 'rs2");
      break;
    case RO_SRA:
      Format(instr, "sra       'rd, 'rs1, 'rs2");
      break;
    case RO_OR:
      Format(instr, "or        'rd, 'rs1, 'rs2");
      break;
    case RO_AND:
      Format(instr, "and       'rd, 'rs1, 'rs2");
      break;
#ifdef V8_TARGET_ARCH_64_BIT
    case RO_ADDW:
      Format(instr, "addw      'rd, 'rs1, 'rs2");
      break;
    case RO_SUBW:
      if (instr->Rs1Value() == zero_reg.code())
        Format(instr, "negw      'rd, 'rs2");
      else
        Format(instr, "subw      'rd, 'rs1, 'rs2");
      break;
    case RO_SLLW:
      Format(instr, "sllw      'rd, 'rs1, 'rs2");
      break;
    case RO_SRLW:
      Format(instr, "srlw      'rd, 'rs1, 'rs2");
      break;
    case RO_SRAW:
      Format(instr, "sraw      'rd, 'rs1, 'rs2");
      break;
#endif /* V8_TARGET_ARCH_64_BIT */
    // TODO: Add RISCV M extension macro
    case RO_MUL:
      Format(instr, "mul       'rd, 'rs1, 'rs2");
      break;
    case RO_MULH:
      Format(instr, "mulh      'rd, 'rs1, 'rs2");
      break;
    case RO_MULHSU:
      Format(instr, "mulhsu    'rd, 'rs1, 'rs2");
      break;
    case RO_MULHU:
      Format(instr, "mulhu     'rd, 'rs1, 'rs2");
      break;
    case RO_DIV:
      Format(instr, "div       'rd, 'rs1, 'rs2");
      break;
    case RO_DIVU:
      Format(instr, "divu      'rd, 'rs1, 'rs2");
      break;
    case RO_REM:
      Format(instr, "rem       'rd, 'rs1, 'rs2");
      break;
    case RO_REMU:
      Format(instr, "remu      'rd, 'rs1, 'rs2");
      break;
#ifdef V8_TARGET_ARCH_64_BIT
    case RO_MULW:
      Format(instr, "mulw      'rd, 'rs1, 'rs2");
      break;
    case RO_DIVW:
      Format(instr, "divw      'rd, 'rs1, 'rs2");
      break;
    case RO_DIVUW:
      Format(instr, "divuw     'rd, 'rs1, 'rs2");
      break;
    case RO_REMW:
      Format(instr, "remw      'rd, 'rs1, 'rs2");
      break;
    case RO_REMUW:
      Format(instr, "remuw     'rd, 'rs1, 'rs2");
      break;
#endif /*V8_TARGET_ARCH_64_BIT*/
    // TODO: End Add RISCV M extension macro
    default: {
      switch (instr->BaseOpcode()) {
        case AMO:
          DecodeRAType(instr);
          break;
        case OP_FP:
          DecodeRFPType(instr);
          break;
        default:
          UNSUPPORTED_RISCV();
      }
    }
  }
}

void Decoder::DecodeRAType(Instruction* instr) {
  // TODO: Add macro for RISCV A extension
  // Special handling for A extension instructions because it uses func5
  // For all A extension instruction, V8 simulator is pure sequential. No
  // Memory address lock or other synchronizaiton behaviors.
  switch (instr->InstructionBits() & kRATypeMask) {
    case RO_LR_W:
      Format(instr, "lr.w'a    'rd, ('rs1)");
      break;
    case RO_SC_W:
      Format(instr, "sc.w'a    'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOSWAP_W:
      Format(instr, "amoswap.w'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOADD_W:
      Format(instr, "amoadd.w'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOXOR_W:
      Format(instr, "amoxor.w'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOAND_W:
      Format(instr, "amoand.w'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOOR_W:
      Format(instr, "amoor.w'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOMIN_W:
      Format(instr, "amomin.w'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOMAX_W:
      Format(instr, "amomax.w'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOMINU_W:
      Format(instr, "amominu.w'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOMAXU_W:
      Format(instr, "amomaxu.w'a 'rd, 'rs2, ('rs1)");
      break;
#ifdef V8_TARGET_ARCH_64_BIT
    case RO_LR_D:
      Format(instr, "lr.d'a 'rd, ('rs1)");
      break;
    case RO_SC_D:
      Format(instr, "sc.d'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOSWAP_D:
      Format(instr, "amoswap.d'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOADD_D:
      Format(instr, "amoadd.d'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOXOR_D:
      Format(instr, "amoxor.d'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOAND_D:
      Format(instr, "amoand.d'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOOR_D:
      Format(instr, "amoor.d'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOMIN_D:
      Format(instr, "amomin.d'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOMAX_D:
      Format(instr, "amoswap.d'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOMINU_D:
      Format(instr, "amominu.d'a 'rd, 'rs2, ('rs1)");
      break;
    case RO_AMOMAXU_D:
      Format(instr, "amomaxu.d'a 'rd, 'rs2, ('rs1)");
      break;
#endif /*V8_TARGET_ARCH_64_BIT*/
    // TODO: End Add macro for RISCV A extension
    default: {
      UNSUPPORTED_RISCV();
    }
  }
}

void Decoder::DecodeRFPType(Instruction* instr) {
  // OP_FP instructions (F/D) uses func7 first. Some further uses fun3 and rs2()

  // kRATypeMask is only for func7
  switch (instr->InstructionBits() & kRFPTypeMask) {
    // TODO: Add macro for RISCV F extension
    case RO_FADD_S:
      Format(instr, "fadd.s    'fd, 'fs1, 'fs2");
      break;
    case RO_FSUB_S:
      Format(instr, "fsub.s    'fd, 'fs1, 'fs2");
      break;
    case RO_FMUL_S:
      Format(instr, "fmul.s    'fd, 'fs1, 'fs2");
      break;
    case RO_FDIV_S:
      Format(instr, "fdiv.s    'fd, 'fs1, 'fs2");
      break;
    case RO_FSQRT_S:
      Format(instr, "fsqrt.s   'fd, 'fs1");
      break;
    case RO_FSGNJ_S: {  // RO_FSGNJN_S  RO_FSGNJX_S
      switch (instr->Funct3Value()) {
        case 0b000:  // RO_FSGNJ_S
          if (instr->Rs1Value() == instr->Rs2Value())
            Format(instr, "fmv.s   'fd, 'fs1");
          else
            Format(instr, "fsgnj.s   'fd, 'fs1, 'fs2");
          break;
        case 0b001:  // RO_FSGNJN_S
          if (instr->Rs1Value() == instr->Rs2Value())
            Format(instr, "fneg.s  'fd, 'fs1");
          else
            Format(instr, "fsgnjn.s  'fd, 'fs1, 'fs2");
          break;
        case 0b010:  // RO_FSGNJX_S
          if (instr->Rs1Value() == instr->Rs2Value())
            Format(instr, "fabs.s  'fd, 'fs1");
          else
            Format(instr, "fsgnjx.s  'fd, 'fs1, 'fs2");
          break;
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
    case RO_FMIN_S: {  // RO_FMAX_S
      switch (instr->Funct3Value()) {
        case 0b000:  // RO_FMIN_S
          Format(instr, "fmin.s    'fd, 'fs1, 'fs2");
          break;
        case 0b001:  // RO_FMAX_S
          Format(instr, "fmax.s    'fd, 'fs1, 'fs2");
          break;
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
    case RO_FCVT_W_S: {  // RO_FCVT_WU_S , 64F RO_FCVT_L_S RO_FCVT_LU_S
      switch (instr->Rs2Value()) {
        case 0b00000:  // RO_FCVT_W_S
          Format(instr, "fcvt.w.s  ['frm] 'rd, 'fs1");
          break;
        case 0b00001:  // RO_FCVT_WU_S
          Format(instr, "fcvt.wu.s ['frm] 'rd, 'fs1");
          break;
#ifdef V8_TARGET_ARCH_64_BIT
        case 0b00010:  // RO_FCVT_L_S
          Format(instr, "fcvt.l.s  ['frm] 'rd, 'fs1");
          break;
        case 0b00011:  // RO_FCVT_LU_S
          Format(instr, "fcvt.lu.s ['frm] 'rd, 'fs1");
          break;
#endif /* V8_TARGET_ARCH_64_BIT */
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
    case RO_FMV: {  // RO_FCLASS_S
      if (instr->Rs2Value() != 0b00000) {
        UNSUPPORTED_RISCV();
      }
      switch (instr->Funct3Value()) {
        case 0b000:  // RO_FMV_X_W
          Format(instr, "fmv.x.w   'rd, 'fs1");
          break;
        case 0b001:  // RO_FCLASS_S
          Format(instr, "fclass.s  'rd, 'fs1");
          break;
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
    case RO_FLE_S: {  // RO_FEQ_S RO_FLT_S RO_FLE_S
      switch (instr->Funct3Value()) {
        case 0b010:  // RO_FEQ_S
          Format(instr, "feq.s     'rd, 'fs1, 'fs2");
          break;
        case 0b001:  // RO_FLT_S
          Format(instr, "flt.s     'rd, 'fs1, 'fs2");
          break;
        case 0b000:  // RO_FLE_S
          Format(instr, "fle.s     'rd, 'fs1, 'fs2");
          break;
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
    case RO_FCVT_S_W: {  // RO_FCVT_S_WU , 64F RO_FCVT_S_L RO_FCVT_S_LU
      switch (instr->Rs2Value()) {
        case 0b00000:  // RO_FCVT_S_W
          Format(instr, "fcvt.s.w  'fd, 'rs1");
          break;
        case 0b00001:  // RO_FCVT_S_WU
          Format(instr, "fcvt.s.wu 'fd, 'rs1");
          break;
#ifdef V8_TARGET_ARCH_64_BIT
        case 0b00010:  // RO_FCVT_S_L
          Format(instr, "fcvt.s.l  'fd, 'rs1");
          break;
        case 0b00011:  // RO_FCVT_S_LU
          Format(instr, "fcvt.s.lu 'fd, 'rs1");
          break;
#endif /* V8_TARGET_ARCH_64_BIT */
        default: {
          UNSUPPORTED_RISCV();
        }
      }
      break;
    }
    case RO_FMV_W_X: {
      if (instr->Funct3Value() == 0b000) {
        Format(instr, "fmv.w.x   'fd, 'rs1");
      } else {
        UNSUPPORTED_RISCV();
      }
      break;
    }
    // TODO: Add macro for RISCV D extension
    case RO_FADD_D:
      Format(instr, "fadd.d    'fd, 'fs1, 'fs2");
      break;
    case RO_FSUB_D:
      Format(instr, "fsub.d    'fd, 'fs1, 'fs2");
      break;
    case RO_FMUL_D:
      Format(instr, "fmul.d    'fd, 'fs1, 'fs2");
      break;
    case RO_FDIV_D:
      Format(instr, "fdiv.d    'fd, 'fs1, 'fs2");
      break;
    case RO_FSQRT_D: {
      if (instr->Rs2Value() == 0b00000) {
        Format(instr, "fsqrt.d   'fd, 'fs1");
      } else {
        UNSUPPORTED_RISCV();
      }
      break;
    }
    case RO_FSGNJ_D: {  // RO_FSGNJN_D RO_FSGNJX_D
      switch (instr->Funct3Value()) {
        case 0b000:  // RO_FSGNJ_D
          if (instr->Rs1Value() == instr->Rs2Value())
            Format(instr, "fmv.d   'fd, 'fs1");
          else
            Format(instr, "fsgnj.d   'fd, 'fs1, 'fs2");
          break;
        case 0b001:  // RO_FSGNJN_D
          if (instr->Rs1Value() == instr->Rs2Value())
            Format(instr, "fneg.d   'fd, 'fs1");
          else
            Format(instr, "fsgnjn.d  'fd, 'fs1, 'fs2");
          break;
        case 0b010:  // RO_FSGNJX_D
          if (instr->Rs1Value() == instr->Rs2Value())
            Format(instr, "fabs.d   'fd, 'fs1");
          else
            Format(instr, "fsgnjx.d  'fd, 'fs1, 'fs2");
          break;
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
    case RO_FMIN_D: {  // RO_FMAX_D
      switch (instr->Funct3Value()) {
        case 0b000:  // RO_FMIN_D
          Format(instr, "fmin.d    'fd, 'fs1, 'fs2");
          break;
        case 0b001:  // RO_FMAX_D
          Format(instr, "fmax.d    'fd, 'fs1, 'fs2");
          break;
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
    case (RO_FCVT_S_D & kRFPTypeMask): {
      if (instr->Rs2Value() == 0b00001) {
        Format(instr, "fcvt.s.d  ['frm] 'fd, 'rs1");
      } else {
        UNSUPPORTED_RISCV();
      }
      break;
    }
    case RO_FCVT_D_S: {
      if (instr->Rs2Value() == 0b00000) {
        Format(instr, "fcvt.d.s  'fd, 'fs1");
      } else {
        UNSUPPORTED_RISCV();
      }
      break;
    }
    case RO_FLE_D: {  // RO_FEQ_D RO_FLT_D RO_FLE_D
      switch (instr->Funct3Value()) {
        case 0b010:  // RO_FEQ_S
          Format(instr, "feq.d     'rd, 'fs1, 'fs2");
          break;
        case 0b001:  // RO_FLT_D
          Format(instr, "flt.d     'rd, 'fs1, 'fs2");
          break;
        case 0b000:  // RO_FLE_D
          Format(instr, "fle.d     'rd, 'fs1, 'fs2");
          break;
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
    case (RO_FCLASS_D & kRFPTypeMask): {  // RO_FCLASS_D , 64D RO_FMV_X_D
      if (instr->Rs2Value() != 0b00000) {
        UNSUPPORTED_RISCV();
        break;
      }
      switch (instr->Funct3Value()) {
        case 0b001:  // RO_FCLASS_D
          Format(instr, "fclass.d  'rd, 'fs1");
          break;
#ifdef V8_TARGET_ARCH_64_BIT
        case 0b000:  // RO_FMV_X_D
          Format(instr, "fmv.x.d   'rd, 'fs1");
          break;
#endif /* V8_TARGET_ARCH_64_BIT */
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
    case RO_FCVT_W_D: {  // RO_FCVT_WU_D , 64F RO_FCVT_L_D RO_FCVT_LU_D
      switch (instr->Rs2Value()) {
        case 0b00000:  // RO_FCVT_W_D
          Format(instr, "fcvt.w.d  ['frm] 'rd, 'fs1");
          break;
        case 0b00001:  // RO_FCVT_WU_D
          Format(instr, "fcvt.wu.d ['frm] 'rd, 'fs1");
          break;
#ifdef V8_TARGET_ARCH_64_BIT
        case 0b00010:  // RO_FCVT_L_D
          Format(instr, "fcvt.l.d  ['frm] 'rd, 'fs1");
          break;
        case 0b00011:  // RO_FCVT_LU_D
          Format(instr, "fcvt.lu.d ['frm] 'rd, 'fs1");
          break;
#endif /* V8_TARGET_ARCH_64_BIT */
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
    case RO_FCVT_D_W: {  // RO_FCVT_D_WU , 64F RO_FCVT_D_L RO_FCVT_D_LU
      switch (instr->Rs2Value()) {
        case 0b00000:  // RO_FCVT_D_W
          Format(instr, "fcvt.d.w  'fd, 'rs1");
          break;
        case 0b00001:  // RO_FCVT_D_WU
          Format(instr, "fcvt.d.wu 'fd, 'rs1");
          break;
#ifdef V8_TARGET_ARCH_64_BIT
        case 0b00010:  // RO_FCVT_D_L
          Format(instr, "fcvt.d.l  'fd, 'rs1");
          break;
        case 0b00011:  // RO_FCVT_D_LU
          Format(instr, "fcvt.d.lu 'fd, 'rs1");
          break;
#endif /* V8_TARGET_ARCH_64_BIT */
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
#ifdef V8_TARGET_ARCH_64_BIT
    case RO_FMV_D_X: {
      if (instr->Funct3Value() == 0b000 && instr->Rs2Value() == 0b00000) {
        Format(instr, "fmv.d.x   'fd, 'rs1");
      } else {
        UNSUPPORTED_RISCV();
      }
      break;
    }
#endif /* V8_TARGET_ARCH_64_BIT */
    default: {
      UNSUPPORTED_RISCV();
    }
  }
}

void Decoder::DecodeR4Type(Instruction* instr) {
  switch (instr->InstructionBits() & kR4TypeMask) {
    // TODO: use F Extension macro block
    case RO_FMADD_S:
      Format(instr, "fmadd.s   'fd, 'fs1, 'fs2, 'fs3");
      break;
    case RO_FMSUB_S:
      Format(instr, "fmsub.s   'fd, 'fs1, 'fs2, 'fs3");
      break;
    case RO_FNMSUB_S:
      Format(instr, "fnmsub.s   'fd, 'fs1, 'fs2, 'fs3");
      break;
    case RO_FNMADD_S:
      Format(instr, "fnmadd.s   'fd, 'fs1, 'fs2, 'fs3");
      break;
    // TODO: use F Extension macro block
    case RO_FMADD_D:
      Format(instr, "fmadd.d   'fd, 'fs1, 'fs2, 'fs3");
      break;
    case RO_FMSUB_D:
      Format(instr, "fmsub.d   'fd, 'fs1, 'fs2, 'fs3");
      break;
    case RO_FNMSUB_D:
      Format(instr, "fnmsub.d  'fd, 'fs1, 'fs2, 'fs3");
      break;
    case RO_FNMADD_D:
      Format(instr, "fnmadd.d  'fd, 'fs1, 'fs2, 'fs3");
      break;
    default:
      UNSUPPORTED_RISCV();
  }
}

void Decoder::DecodeIType(Instruction* instr) {
  switch (instr->InstructionBits() & kITypeMask) {
    case RO_JALR:
      if (instr->RV_RdValue() == zero_reg.code() &&
          instr->Rs1Value() == ra.code() && instr->Imm12Value() == 0)
        Format(instr, "ret");
      else if (instr->RV_RdValue() == zero_reg.code() &&
               instr->Imm12Value() == 0)
        Format(instr, "jr        'rs1");
      else if (instr->RV_RdValue() == ra.code() && instr->Imm12Value() == 0)
        Format(instr, "jalr      'rs1");
      else
        Format(instr, "jalr      'rd, 'imm12('rs1)");
      break;
    case RO_LB:
      Format(instr, "lb        'rd, 'imm12('rs1)");
      break;
    case RO_LH:
      Format(instr, "lh        'rd, 'imm12('rs1)");
      break;
    case RO_LW:
      Format(instr, "lw        'rd, 'imm12('rs1)");
      break;
    case RO_LBU:
      Format(instr, "lbu       'rd, 'imm12('rs1)");
      break;
    case RO_LHU:
      Format(instr, "lhu       'rd, 'imm12('rs1)");
      break;
#ifdef V8_TARGET_ARCH_64_BIT
    case RO_LWU:
      Format(instr, "lwu       'rd, 'imm12('rs1)");
      break;
    case RO_LD:
      Format(instr, "ld        'rd, 'imm12('rs1)");
      break;
#endif /*V8_TARGET_ARCH_64_BIT*/
    case RO_ADDI:
      if (instr->Imm12Value() == 0) {
        if (instr->RV_RdValue() == zero_reg.code() &&
            instr->Rs1Value() == zero_reg.code())
          Format(instr, "nop");
        else
          Format(instr, "mv        'rd, 'rs1");
      } else if (instr->Rs1Value() == zero_reg.code()) {
        Format(instr, "li        'rd, 'imm12");
      } else {
        Format(instr, "addi      'rd, 'rs1, 'imm12");
      }
      break;
    case RO_SLTI:
      Format(instr, "slti      'rd, 'rs1, 'imm12");
      break;
    case RO_SLTIU:
      if (instr->Imm12Value() == 1)
        Format(instr, "seqz      'rd, 'rs1");
      else
        Format(instr, "sltiu     'rd, 'rs1, 'imm12");
      break;
    case RO_XORI:
      if (instr->Imm12Value() == -1)
        Format(instr, "not       'rd, 'rs1");
      else
        Format(instr, "xori      'rd, 'rs1, 'imm12x");
      break;
    case RO_ORI:
      Format(instr, "ori       'rd, 'rs1, 'imm12x");
      break;
    case RO_ANDI:
      Format(instr, "andi      'rd, 'rs1, 'imm12x");
      break;
    case RO_SLLI:
      Format(instr, "slli      'rd, 'rs1, 's64");
      break;
    case RO_SRLI: {  //  RO_SRAI
      if (!instr->IsArithShift()) {
        Format(instr, "srli      'rd, 'rs1, 's64");
      } else {
        Format(instr, "srai      'rd, 'rs1, 's64");
      }
      break;
    }
#ifdef V8_TARGET_ARCH_64_BIT
    case RO_ADDIW:
      if (instr->Imm12Value() == 0)
        Format(instr, "sext.w    'rd, 'rs1");
      else
        Format(instr, "addiw     'rd, 'rs1, 'imm12");
      break;
    case RO_SLLIW:
      Format(instr, "slliw     'rd, 'rs1, 's32");
      break;
    case RO_SRLIW: {  //  RO_SRAIW
      if (!instr->IsArithShift()) {
        Format(instr, "srliw     'rd, 'rs1, 's32");
      } else {
        Format(instr, "sraiw     'rd, 'rs1, 's32");
      }
      break;
    }
#endif /*V8_TARGET_ARCH_64_BIT*/
    case RO_FENCE:
      if (instr->MemoryOrder(true) == PSIORW &&
          instr->MemoryOrder(false) == PSIORW)
        Format(instr, "fence");
      else
        Format(instr, "fence 'pre, 'suc");
      break;
    case RO_ECALL: {                   // RO_EBREAK
      if (instr->Imm12Value() == 0) {  // ECALL
        Format(instr, "ecall");
      } else if (instr->Imm12Value() == 1) {  // EBREAK
        Format(instr, "ebreak");
      } else {
        UNSUPPORTED_RISCV();
      }
      break;
    }
    // TODO: use Zifencei Standard Extension macro block
    case RO_FENCE_I:
      Format(instr, "fence.i");
      break;
    // TODO: use Zicsr Standard Extension macro block
    // FIXME(RISC-V): Add special formatting for CSR registers
    case RO_CSRRW:
      if (instr->CsrValue() == csr_fcsr) {
        if (instr->RV_RdValue() == zero_reg.code())
          Format(instr, "fscsr     'rs1");
        else
          Format(instr, "fscsr     'rd, 'rs1");
      } else if (instr->CsrValue() == csr_frm) {
        if (instr->RV_RdValue() == zero_reg.code())
          Format(instr, "fsrm      'rs1");
        else
          Format(instr, "fsrm      'rd, 'rs1");
      } else if (instr->CsrValue() == csr_fflags) {
        if (instr->RV_RdValue() == zero_reg.code())
          Format(instr, "fsflags   'rs1");
        else
          Format(instr, "fsflags   'rd, 'rs1");
      } else if (instr->RV_RdValue() == zero_reg.code()) {
        Format(instr, "csrw      'csr, 'rs1");
      } else {
        Format(instr, "csrrw     'rd, 'csr, 'rs1");
      }
      break;
    case RO_CSRRS:
      if (instr->Rs1Value() == zero_reg.code()) {
        switch (instr->CsrValue()) {
          case csr_instret:
            Format(instr, "rdinstret 'rd");
            break;
          case csr_instreth:
            Format(instr, "rdinstreth 'rd");
            break;
          case csr_time:
            Format(instr, "rdtime    'rd");
            break;
          case csr_timeh:
            Format(instr, "rdtimeh   'rd");
            break;
          case csr_cycle:
            Format(instr, "rdcycle   'rd");
            break;
          case csr_cycleh:
            Format(instr, "rdcycleh  'rd");
            break;
          case csr_fflags:
            Format(instr, "frflags   'rd");
            break;
          case csr_frm:
            Format(instr, "frrm      'rd");
            break;
          case csr_fcsr:
            Format(instr, "frcsr     'rd");
            break;
          default:
            UNREACHABLE();
        }
      } else if (instr->Rs1Value() == zero_reg.code()) {
        Format(instr, "csrr      'rd, 'csr");
      } else if (instr->RV_RdValue() == zero_reg.code()) {
        Format(instr, "csrs      'csr, 'rs1");
      } else
        Format(instr, "csrrs     'rd, 'csr, 'rs1");
      break;
    case RO_CSRRC:
      if (instr->RV_RdValue() == zero_reg.code())
        Format(instr, "csrc      'csr, 'rs1");
      else
        Format(instr, "csrrc     'rd, 'csr, 'rs1");
      break;
    case RO_CSRRWI:
      if (instr->RV_RdValue() == zero_reg.code())
        Format(instr, "csrwi     'csr, 'vs1");
      else
        Format(instr, "csrrwi    'rd, 'csr, 'vs1");
      break;
    case RO_CSRRSI:
      if (instr->RV_RdValue() == zero_reg.code())
        Format(instr, "csrsi     'csr, 'vs1");
      else
        Format(instr, "csrrsi    'rd, 'csr, 'vs1");
      break;
    case RO_CSRRCI:
      if (instr->RV_RdValue() == zero_reg.code())
        Format(instr, "csrci     'csr, 'vs1");
      else
        Format(instr, "csrrci    'rd, 'csr, 'vs1");
      break;
    // TODO: use F Extension macro block
    case RO_FLW:
      Format(instr, "flw       'fd, 'imm12('rs1)");
      break;
    // TODO: use D Extension macro block
    case RO_FLD:
      Format(instr, "fld       'fd, 'imm12('rs1)");
      break;
    default:
      UNSUPPORTED_RISCV();
  }
}

void Decoder::DecodeSType(Instruction* instr) {
  switch (instr->InstructionBits() & kSTypeMask) {
    case RO_SB:
      Format(instr, "sb        'rs2, 'offS('rs1)");
      break;
    case RO_SH:
      Format(instr, "sh        'rs2, 'offS('rs1)");
      break;
    case RO_SW:
      Format(instr, "sw        'rs2, 'offS('rs1)");
      break;
#ifdef V8_TARGET_ARCH_64_BIT
    case RO_SD:
      Format(instr, "sd        'rs2, 'offS('rs1)");
      break;
#endif /*V8_TARGET_ARCH_64_BIT*/
    // TODO: use F Extension macro block
    case RO_FSW:
      Format(instr, "fsw       'fs2, 'offS('rs1)");
      break;
    // TODO: use D Extension macro block
    case RO_FSD:
      Format(instr, "fsd       'fs2, 'offS('rs1)");
      break;
    default:
      UNSUPPORTED_RISCV();
  }
}

void Decoder::DecodeBType(Instruction* instr) {
  switch (instr->InstructionBits() & kBTypeMask) {
    case RO_BEQ:
      Format(instr, "beq       'rs1, 'rs2, 'offB");
      break;
    case RO_BNE:
      Format(instr, "bne       'rs1, 'rs2, 'offB");
      break;
    case RO_BLT:
      Format(instr, "blt       'rs1, 'rs2, 'offB");
      break;
    case RO_BGE:
      Format(instr, "bge       'rs1, 'rs2, 'offB");
      break;
    case RO_BLTU:
      Format(instr, "bltu      'rs1, 'rs2, 'offB");
      break;
    case RO_BGEU:
      Format(instr, "bgeu      'rs1, 'rs2, 'offB");
      break;
    default:
      UNSUPPORTED_RISCV();
  }
}
void Decoder::DecodeUType(Instruction* instr) {
  // U Type doesn't have additional mask
  switch (instr->BaseOpcodeFieldRaw()) {
    case RO_LUI:
      Format(instr, "lui       'rd, 'imm20U");
      break;
    case RO_AUIPC:
      Format(instr, "auipc     'rd, 'imm20U");
      break;
    default:
      UNSUPPORTED_RISCV();
  }
}
void Decoder::DecodeJType(Instruction* instr) {
  // J Type doesn't have additional mask
  switch (instr->BaseOpcodeValue()) {
    case RO_JAL:
      if (instr->RV_RdValue() == zero_reg.code())
        Format(instr, "j         'imm20J");
      else if (instr->RV_RdValue() == ra.code())
        Format(instr, "jal       'imm20J");
      else
        Format(instr, "jal       'rd, 'imm20J");
      break;
    default:
      UNSUPPORTED_RISCV();
  }
}

// Disassemble the instruction at *instr_ptr into the output buffer.
// All instructions are one word long, except for the simulator
// pseudo-instruction stop(msg). For that one special case, we return
// size larger than one kInstrSize.
int Decoder::InstructionDecode(byte* instr_ptr) {
  Instruction* instr = Instruction::At(instr_ptr);
  // Print raw instruction bytes.
  out_buffer_pos_ += SNPrintF(out_buffer_ + out_buffer_pos_, "%08x       ",
                              instr->InstructionBits());
  switch (instr->InstructionType()) {
    case Instruction::kRType:
      DecodeRType(instr);
      break;
    case Instruction::kR4Type:
      DecodeR4Type(instr);
      break;
    case Instruction::kIType:
      DecodeIType(instr);
      break;
    case Instruction::kSType:
      DecodeSType(instr);
      break;
    case Instruction::kBType:
      DecodeBType(instr);
      break;
    case Instruction::kUType:
      DecodeUType(instr);
      break;
    case Instruction::kJType:
      DecodeJType(instr);
      break;
    default:
      Format(instr, "UNSUPPORTED");
      UNSUPPORTED_RISCV();
  }
  return kInstrSize;
}

}  // namespace internal
}  // namespace v8

//------------------------------------------------------------------------------

namespace disasm {

const char* NameConverter::NameOfAddress(byte* addr) const {
  v8::internal::SNPrintF(tmp_buffer_, "%p", static_cast<void*>(addr));
  return tmp_buffer_.begin();
}

const char* NameConverter::NameOfConstant(byte* addr) const {
  return NameOfAddress(addr);
}

const char* NameConverter::NameOfCPURegister(int reg) const {
  return v8::internal::Registers::Name(reg);
}

const char* NameConverter::NameOfXMMRegister(int reg) const {
  return v8::internal::FPURegisters::Name(reg);
}

const char* NameConverter::NameOfByteCPURegister(int reg) const {
  UNREACHABLE();  // MIPS does not have the concept of a byte register.
  return "nobytereg";
}

const char* NameConverter::NameInCode(byte* addr) const {
  // The default name converter is called for unknown code. So we will not try
  // to access any memory.
  return "";
}

//------------------------------------------------------------------------------

int Disassembler::InstructionDecode(v8::internal::Vector<char> buffer,
                                    byte* instruction) {
  v8::internal::Decoder d(converter_, buffer);
  return d.InstructionDecode(instruction);
}

// The MIPS assembler does not currently use constant pools.
int Disassembler::ConstantPoolSizeAt(byte* instruction) { return -1; }

void Disassembler::Disassemble(FILE* f, byte* begin, byte* end,
                               UnimplementedOpcodeAction unimplemented_action) {
  NameConverter converter;
  Disassembler d(converter, unimplemented_action);
  for (byte* pc = begin; pc < end;) {
    v8::internal::EmbeddedVector<char, 128> buffer;
    buffer[0] = '\0';
    byte* prev_pc = pc;
    pc += d.InstructionDecode(buffer, pc);
    v8::internal::PrintF(f, "%p    %08x      %s\n", static_cast<void*>(prev_pc),
                         *reinterpret_cast<int32_t*>(prev_pc), buffer.begin());
  }
}

#undef STRING_STARTS_WITH

}  // namespace disasm

#endif  // V8_TARGET_ARCH_RISCV
