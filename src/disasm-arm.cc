// Copyright 2007-2008 Google Inc. All Rights Reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#ifndef WIN32
#include <stdint.h>
#endif

#include "v8.h"

#include "disasm.h"
#include "macro-assembler.h"
#include "platform.h"

namespace assembler { namespace arm {

namespace v8i = v8::internal;


//------------------------------------------------------------------------------

// Decoder decodes and disassembles instructions into an output buffer.
// It uses the converter to convert register names and call destinations into
// more informative description.
class Decoder {
 public:
  Decoder(const disasm::NameConverter& converter,
          char* out_buffer, const int out_buffer_size)
    : converter_(converter),
      out_buffer_(out_buffer),
      out_buffer_size_(out_buffer_size),
      out_buffer_pos_(0) {
    ASSERT(out_buffer_size_ > 0);
    out_buffer_[out_buffer_pos_] = '\0';
  }

  ~Decoder() {}

  // Writes one disassembled instruction into 'buffer' (0-terminated).
  // Returns the length of the disassembled machine instruction in bytes.
  int InstructionDecode(byte* instruction);

 private:
  const disasm::NameConverter& converter_;
  char* out_buffer_;
  const int out_buffer_size_;
  int out_buffer_pos_;

  void PrintChar(const char ch);
  void Print(const char* str);

  void PrintRegister(int reg);
  void PrintCondition(Instr* instr);
  void PrintShiftRm(Instr* instr);
  void PrintShiftImm(Instr* instr);

  int FormatOption(Instr* instr, const char* option);
  void Format(Instr* instr, const char* format);
  void Unknown(Instr* instr);

  void DecodeType0(Instr* instr);
  void DecodeType1(Instr* instr);
  void DecodeType2(Instr* instr);
  void DecodeType3(Instr* instr);
  void DecodeType4(Instr* instr);
  void DecodeType5(Instr* instr);
  void DecodeType6(Instr* instr);
  void DecodeType7(Instr* instr);
};


// Append the ch to the output buffer.
void Decoder::PrintChar(const char ch) {
  ASSERT(out_buffer_pos_ < out_buffer_size_);
  out_buffer_[out_buffer_pos_++] = ch;
}


// Append the str to the output buffer.
void Decoder::Print(const char* str) {
  char cur = *str++;
  while (cur != 0 && (out_buffer_pos_ < (out_buffer_size_-1))) {
    PrintChar(cur);
    cur = *str++;
  }
  out_buffer_[out_buffer_pos_] = 0;
}


static const char* cond_names[16] = {
"eq", "ne", "cs" , "cc" , "mi" , "pl" , "vs" , "vc" ,
"hi", "ls", "ge", "lt", "gt", "le", "", "invalid",
};


// Print the condition guarding the instruction.
void Decoder::PrintCondition(Instr* instr) {
  Print(cond_names[instr->ConditionField()]);
}


// Print the register name according to the active name converter.
void Decoder::PrintRegister(int reg) {
  Print(converter_.NameOfCPURegister(reg));
}


static const char* shift_names[4] = {
  "lsl", "lsr", "asr", "ror"
};


// Print the register shift operands for the instruction. Generally used for
// data processing instructions.
void Decoder::PrintShiftRm(Instr* instr) {
  Shift shift = instr->ShiftField();
  int shift_amount = instr->ShiftAmountField();
  int rm = instr->RmField();

  PrintRegister(rm);

  if ((instr->RegShiftField() == 0) && (shift == LSL) && (shift_amount == 0)) {
    // Special case for using rm only.
    return;
  }
  if (instr->RegShiftField() == 0) {
    // by immediate
    if ((shift == ROR) && (shift_amount == 0)) {
      Print(", RRX");
      return;
    } else if (((shift == LSR) || (shift == ASR)) && (shift_amount == 0)) {
      shift_amount = 32;
    }
    out_buffer_pos_ += v8i::OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                         out_buffer_size_ - out_buffer_pos_,
                                         ", %s #%d",
                                         shift_names[shift], shift_amount);
  } else {
    // by register
    int rs = instr->RsField();
    out_buffer_pos_ += v8i::OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                         out_buffer_size_ - out_buffer_pos_,
                                         ", %s ", shift_names[shift]);
    PrintRegister(rs);
  }
}


// Print the immediate operand for the instruction. Generally used for data
// processing instructions.
void Decoder::PrintShiftImm(Instr* instr) {
  int rotate = instr->RotateField() * 2;
  int immed8 = instr->Immed8Field();
  int imm = (immed8 >> rotate) | (immed8 << (32 - rotate));
  out_buffer_pos_ += v8i::OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                       out_buffer_size_ - out_buffer_pos_,
                                       "#%d", imm);
}


// FormatOption takes a formatting string and interprets it based on
// the current instructions. The format string points to the first
// character of the option string (the option escape has already been
// consumed by the caller.)  FormatOption returns the number of
// characters that were consumed from the formatting string.
int Decoder::FormatOption(Instr* instr, const char* format) {
  switch (format[0]) {
    case 'a': {  // 'a: accumulate multiplies
      if (instr->Bit(21) == 0) {
        Print("ul");
      } else {
        Print("la");
      }
      return 1;
      break;
    }
    case 'b': {  // 'b: byte loads or stores
      if (instr->HasB()) {
        Print("b");
      }
      return 1;
      break;
    }
    case 'c': {  // 'cond: conditional execution
      ASSERT((format[1] == 'o') && (format[2] == 'n') && (format[3] =='d'));
      PrintCondition(instr);
      return 4;
      break;
    }
    case 'h': {  // 'h: halfword operation for extra loads and stores
      if (instr->HasH()) {
        Print("h");
      } else {
        Print("b");
      }
      return 1;
      break;
    }
    case 'i': {  // 'imm: immediate value for data processing instructions
      ASSERT((format[1] == 'm') && (format[2] == 'm'));
      PrintShiftImm(instr);
      return 3;
      break;
    }
    case 'l': {  // 'l: branch and link
      if (instr->HasLink()) {
        Print("l");
      }
      return 1;
      break;
    }
    case 'm': {  // 'msg: for simulator break instructions
      if (format[1] == 'e') {
        ASSERT((format[2] == 'm') && (format[3] == 'o') && (format[4] == 'p'));
        if (instr->HasL()) {
          Print("ldr");
        } else {
          Print("str");
        }
        return 5;
      } else {
        ASSERT(format[1] == 's' && format[2] == 'g');
        byte* str =
            reinterpret_cast<byte*>(instr->InstructionBits() & 0x0fffffff);
        out_buffer_pos_ += v8i::OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                             out_buffer_size_ - out_buffer_pos_,
                                             "%s", converter_.NameInCode(str));
        return 3;
      }
      break;
    }
    case 'o': {
      ASSERT(format[1] == 'f' && format[2] == 'f');
      if (format[3] == '1') {
        // 'off12: 12-bit offset for load and store instructions
        ASSERT(format[4] == '2');
        out_buffer_pos_ += v8i::OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                             out_buffer_size_ - out_buffer_pos_,
                                             "%d", instr->Offset12Field());
        return 5;
      } else {
        // 'off8: 8-bit offset for extra load and store instructions
        ASSERT(format[3] == '8');
        int offs8 = (instr->ImmedHField() << 4) | instr->ImmedLField();
        out_buffer_pos_ += v8i::OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                             out_buffer_size_ - out_buffer_pos_,
                                             "%d", offs8);
        return 4;
      }
      break;
    }
    case 'p': {  // 'pu: P and U bits for load and store isntructions
      ASSERT(format[1] == 'u');
      switch (instr->PUField()) {
        case 0: {
          Print("da");
          break;
        }
        case 1: {
          Print("ia");
          break;
        }
        case 2: {
          Print("db");
          break;
        }
        case 3: {
          Print("ib");
          break;
        }
        default: {
          UNREACHABLE();
          break;
        }
      }
      return 2;
      break;
    }
    case 'r': {
      if (format[1] == 'n') {  // 'rn: Rn register
        int reg = instr->RnField();
        PrintRegister(reg);
        return 2;
      } else if (format[1] == 'd') {  // 'rd: Rd register
        int reg = instr->RdField();
        PrintRegister(reg);
        return 2;
      } else if (format[1] == 's') {  // 'rs: Rs register
        int reg = instr->RsField();
        PrintRegister(reg);
        return 2;
      } else if (format[1] == 'm') {  // 'rm: Rm register
        int reg = instr->RmField();
        PrintRegister(reg);
        return 2;
      } else if (format[1] == 'l') {
        // 'rlist: register list for load and store multiple instructions
        ASSERT(format[2] == 'i' && format[3] == 's' && format[4] == 't');
        int rlist = instr->RlistField();
        int reg = 0;
        Print("{");
        while (rlist != 0) {
          if ((rlist & 1) != 0) {
            PrintRegister(reg);
            if ((rlist >> 1) != 0) {
              Print(", ");
            }
          }
          reg++;
          rlist >>= 1;
        }
        Print("}");
        return 5;
      } else {
        UNREACHABLE();
      }
      UNREACHABLE();
      return -1;
      break;
    }
    case 's': {
      if (format[1] == 'h') {  // 'shift_rm: register shift operands
        ASSERT(format[2] == 'i' && format[3] == 'f' && format[4] == 't'
               && format[5] == '_' && format[6] == 'r' && format[7] == 'm');
        PrintShiftRm(instr);
        return 8;
      } else if (format[1] == 'w') {
        ASSERT(format[2] == 'i');
        SoftwareInterruptCodes swi = instr->SwiField();
        switch (swi) {
          case call_rt_r5:
            Print("call_rt_r5");
            break;
          case call_rt_r2:
            Print("call_rt_r2");
            break;
          case break_point:
            Print("break_point");
            break;
          default:
            out_buffer_pos_ += v8i::OS::SNPrintF(
                out_buffer_ + out_buffer_pos_,
                out_buffer_size_ - out_buffer_pos_,
                "%d",
                swi);
            break;
        }
        return 3;
      } else if (format[1] == 'i') {  // 'sign: signed extra loads and stores
        ASSERT(format[2] == 'g' && format[3] == 'n');
        if (instr->HasSign()) {
          Print("s");
        }
        return 4;
        break;
      } else {  // 's: S field of data processing instructions
        if (instr->HasS()) {
          Print("s");
        }
        return 1;
      }
      break;
    }
    case 't': {  // 'target: target of branch instructions
      ASSERT(format[1] == 'a' && format[2] == 'r' && format[3] == 'g'
             && format[4] == 'e' && format[5] == 't');
      int off = (instr->SImmed24Field() << 2) + 8;
      out_buffer_pos_ += v8i::OS::SNPrintF(
          out_buffer_ + out_buffer_pos_,
          out_buffer_size_ - out_buffer_pos_,
          "%+d -> %s",
          off,
          converter_.NameOfAddress(reinterpret_cast<byte*>(instr) + off));
      return 6;
      break;
    }
    case 'u': {  // 'u: signed or unsigned multiplies
      if (instr->Bit(22) == 0) {
        Print("u");
      } else {
        Print("s");
      }
      return 1;
      break;
    }
    case 'w': {  // 'w: W field of load and store instructions
      if (instr->HasW()) {
        Print("!");
      }
      return 1;
      break;
    }
    default: {
      UNREACHABLE();
      break;
    }
  }
  UNREACHABLE();
  return -1;
}


// Format takes a formatting string for a whole instruction and prints it into
// the output buffer. All escaped options are handed to FormatOption to be
// parsed further.
void Decoder::Format(Instr* instr, const char* format) {
  char cur = *format++;
  while ((cur != 0) && (out_buffer_pos_ < (out_buffer_size_ - 1))) {
    if (cur == '\'') {  // Single quote is used as the formatting escape.
      format += FormatOption(instr, format);
    } else {
      out_buffer_[out_buffer_pos_++] = cur;
    }
    cur = *format++;
  }
  ASSERT(out_buffer_pos_ < out_buffer_size_);
  out_buffer_[out_buffer_pos_]  = '\0';
}


// For currently unimplemented decodings the disassembler calls Unknown(instr)
// which will just print "unknown" of the instruction bits.
void Decoder::Unknown(Instr* instr) {
  Format(instr, "unknown");
}


void Decoder::DecodeType0(Instr* instr) {
  if (instr->IsSpecialType0()) {
    // multiply instruction or extra loads and stores
    if (instr->Bits(7, 4) == 9) {
      if (instr->Bit(24) == 0) {
        // multiply instructions
        if (instr->Bit(23) == 0) {
          if (instr->Bit(21) == 0) {
            Format(instr, "mul'cond's 'rd, 'rm, 'rs");
          } else {
            Format(instr, "mla'cond's 'rd, 'rm, 'rs, 'rn");
          }
        } else {
          Format(instr, "'um'al'cond's 'rn, 'rd, 'rs, 'rm");
        }
      } else {
        Unknown(instr);  // not used by V8
      }
    } else {
      // extra load/store instructions
      switch (instr->PUField()) {
        case 0: {
          if (instr->Bit(22) == 0) {
            Format(instr, "'memop'cond'sign'h 'rd, ['rn], -'rm");
          } else {
            Format(instr, "'memop'cond'sign'h 'rd, ['rn], #-'off8");
          }
          break;
        }
        case 1: {
          if (instr->Bit(22) == 0) {
            Format(instr, "'memop'cond'sign'h 'rd, ['rn], +'rm");
          } else {
            Format(instr, "'memop'cond'sign'h 'rd, ['rn], #+'off8");
          }
          break;
        }
        case 2: {
          if (instr->Bit(22) == 0) {
            Format(instr, "'memop'cond'sign'h 'rd, ['rn, -'rm]'w");
          } else {
            Format(instr, "'memop'cond'sign'h 'rd, ['rn, #-'off8]'w");
          }
          break;
        }
        case 3: {
          if (instr->Bit(22) == 0) {
            Format(instr, "'memop'cond'sign'h 'rd, ['rn, +'rm]'w");
          } else {
            Format(instr, "'memop'cond'sign'h 'rd, ['rn, #+'off8]'w");
          }
          break;
        }
        default: {
          // The PU field is a 2-bit field.
          UNREACHABLE();
          break;
        }
      }
      return;
    }
  } else {
    switch (instr->OpcodeField()) {
      case AND: {
        Format(instr, "and'cond's 'rd, 'rn, 'shift_rm");
        break;
      }
      case EOR: {
        Format(instr, "eor'cond's 'rd, 'rn, 'shift_rm");
        break;
      }
      case SUB: {
        Format(instr, "sub'cond's 'rd, 'rn, 'shift_rm");
        break;
      }
      case RSB: {
        Format(instr, "rsb'cond's 'rd, 'rn, 'shift_rm");
        break;
      }
      case ADD: {
        Format(instr, "add'cond's 'rd, 'rn, 'shift_rm");
        break;
      }
      case ADC: {
        Format(instr, "adc'cond's 'rd, 'rn, 'shift_rm");
        break;
      }
      case SBC: {
        Format(instr, "sbc'cond's 'rd, 'rn, 'shift_rm");
        break;
      }
      case RSC: {
        Format(instr, "rsc'cond's 'rd, 'rn, 'shift_rm");
        break;
      }
      case TST: {
        if (instr->HasS()) {
          Format(instr, "tst'cond 'rn, 'shift_rm");
        } else {
          Unknown(instr);  // not used by V8
          return;
        }
        break;
      }
      case TEQ: {
        if (instr->HasS()) {
          Format(instr, "teq'cond 'rn, 'shift_rm");
        } else {
          Unknown(instr);  // not used by V8
          return;
        }
        break;
      }
      case CMP: {
        if (instr->HasS()) {
          Format(instr, "cmp'cond 'rn, 'shift_rm");
        } else {
          Unknown(instr);  // not used by V8
          return;
        }
        break;
      }
      case CMN: {
        if (instr->HasS()) {
          Format(instr, "cmn'cond 'rn, 'shift_rm");
        } else {
          Unknown(instr);  // not used by V8
          return;
        }
        break;
      }
      case ORR: {
        Format(instr, "orr'cond's 'rd, 'rn, 'shift_rm");
        break;
      }
      case MOV: {
        Format(instr, "mov'cond's 'rd, 'shift_rm");
        break;
      }
      case BIC: {
        Format(instr, "bic'cond's 'rd, 'rn, 'shift_rm");
        break;
      }
      case MVN: {
        Format(instr, "mvn'cond's 'rd, 'shift_rm");
        break;
      }
      default: {
        // The Opcode field is a 4-bit field.
        UNREACHABLE();
        break;
      }
    }
  }
}


void Decoder::DecodeType1(Instr* instr) {
  switch (instr->OpcodeField()) {
    case AND: {
      Format(instr, "and'cond's 'rd, 'rn, 'imm");
      break;
    }
    case EOR: {
      Format(instr, "eor'cond's 'rd, 'rn, 'imm");
      break;
    }
    case SUB: {
      Format(instr, "sub'cond's 'rd, 'rn, 'imm");
      break;
    }
    case RSB: {
      Format(instr, "rsb'cond's 'rd, 'rn, 'imm");
      break;
    }
    case ADD: {
      Format(instr, "add'cond's 'rd, 'rn, 'imm");
      break;
    }
    case ADC: {
      Format(instr, "adc'cond's 'rd, 'rn, 'imm");
      break;
    }
    case SBC: {
      Format(instr, "sbc'cond's 'rd, 'rn, 'imm");
      break;
    }
    case RSC: {
      Format(instr, "rsc'cond's 'rd, 'rn, 'imm");
      break;
    }
    case TST: {
      if (instr->HasS()) {
        Format(instr, "tst'cond 'rn, 'imm");
      } else {
        Unknown(instr);  // not used by V8
        return;
      }
      break;
    }
    case TEQ: {
      if (instr->HasS()) {
        Format(instr, "teq'cond 'rn, 'imm");
      } else {
        Unknown(instr);  // not used by V8
        return;
      }
      break;
    }
    case CMP: {
      if (instr->HasS()) {
        Format(instr, "cmp'cond 'rn, 'imm");
      } else {
        Unknown(instr);  // not used by V8
        return;
      }
      break;
    }
    case CMN: {
      if (instr->HasS()) {
        Format(instr, "cmn'cond 'rn, 'imm");
      } else {
        Unknown(instr);  // not used by V8
        return;
      }
      break;
    }
    case ORR: {
      Format(instr, "orr'cond's 'rd, 'rn, 'imm");
      break;
    }
    case MOV: {
      Format(instr, "mov'cond's 'rd, 'imm");
      break;
    }
    case BIC: {
      Format(instr, "bic'cond's 'rd, 'rn, 'imm");
      break;
    }
    case MVN: {
      Format(instr, "mvn'cond's 'rd, 'imm");
      break;
    }
    default: {
      // The Opcode field is a 4-bit field.
      UNREACHABLE();
      break;
    }
  }
}


void Decoder::DecodeType2(Instr* instr) {
  switch (instr->PUField()) {
    case 0: {
      if (instr->HasW()) {
        Unknown(instr);  // not used in V8
        return;
      }
      Format(instr, "'memop'cond'b 'rd, ['rn], #-'off12");
      break;
    }
    case 1: {
      if (instr->HasW()) {
        Unknown(instr);  // not used in V8
        return;
      }
      Format(instr, "'memop'cond'b 'rd, ['rn], #+'off12");
      break;
    }
    case 2: {
      Format(instr, "'memop'cond'b 'rd, ['rn, #-'off12]'w");
      break;
    }
    case 3: {
      Format(instr, "'memop'cond'b 'rd, ['rn, #+'off12]'w");
      break;
    }
    default: {
      // The PU field is a 2-bit field.
      UNREACHABLE();
      break;
    }
  }
}


void Decoder::DecodeType3(Instr* instr) {
  switch (instr->PUField()) {
    case 0: {
      ASSERT(!instr->HasW());
      Format(instr, "'memop'cond'b 'rd, ['rn], -'shift_rm");
      break;
    }
    case 1: {
      ASSERT(!instr->HasW());
      Format(instr, "'memop'cond'b 'rd, ['rn], +'shift_rm");
      break;
    }
    case 2: {
      Format(instr, "'memop'cond'b 'rd, ['rn, -'shift_rm]'w");
      break;
    }
    case 3: {
      Format(instr, "'memop'cond'b 'rd, ['rn, +'shift_rm]'w");
      break;
    }
    default: {
      // The PU field is a 2-bit field.
      UNREACHABLE();
      break;
    }
  }
}


void Decoder::DecodeType4(Instr* instr) {
  ASSERT(instr->Bit(22) == 0);  // Privileged mode currently not supported.
  if (instr->HasL()) {
    Format(instr, "ldm'cond'pu 'rn'w, 'rlist");
  } else {
    Format(instr, "stm'cond'pu 'rn'w, 'rlist");
  }
}


void Decoder::DecodeType5(Instr* instr) {
  Format(instr, "b'l'cond 'target");
}


void Decoder::DecodeType6(Instr* instr) {
  // Coprocessor instructions currently not supported.
  Unknown(instr);
}


void Decoder::DecodeType7(Instr* instr) {
  if (instr->Bit(24) == 1) {
    Format(instr, "swi'cond 'swi");
  } else {
    // Coprocessor instructions currently not supported.
    Unknown(instr);
  }
}


// Disassemble the instruction at *instr_ptr into the output buffer.
int Decoder::InstructionDecode(byte* instr_ptr) {
  Instr* instr = Instr::At(instr_ptr);
  // Print raw instruction bytes.
  out_buffer_pos_ += v8i::OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                       out_buffer_size_ - out_buffer_pos_,
                                       "%08x       ",
                                       instr->InstructionBits());
  if (instr->ConditionField() == special_condition) {
    Format(instr, "break 'msg");
    return Instr::kInstrSize;
  }
  switch (instr->TypeField()) {
    case 0: {
      DecodeType0(instr);
      break;
    }
    case 1: {
      DecodeType1(instr);
      break;
    }
    case 2: {
      DecodeType2(instr);
      break;
    }
    case 3: {
      DecodeType3(instr);
      break;
    }
    case 4: {
      DecodeType4(instr);
      break;
    }
    case 5: {
      DecodeType5(instr);
      break;
    }
    case 6: {
      DecodeType6(instr);
      break;
    }
    case 7: {
      DecodeType7(instr);
      break;
    }
    default: {
      // The type field is 3-bits in the ARM encoding.
      UNREACHABLE();
      break;
    }
  }
  return Instr::kInstrSize;
}


} }  // namespace assembler::arm



//------------------------------------------------------------------------------

namespace disasm {

static const char* reg_names[16] = {
  "r0", "r1", "r2" , "r3" , "r4" , "r5" , "r6" , "r7" ,
  "r8", "r9", "sl", "fp", "ip", "sp", "lr", "pc",
};


const char* NameConverter::NameOfAddress(byte* addr) const {
  static char tmp_buffer[32];
#ifdef WIN32
  _snprintf(tmp_buffer, sizeof tmp_buffer, "%p", addr);
#else
  snprintf(tmp_buffer, sizeof tmp_buffer, "%p", addr);
#endif
  return tmp_buffer;
}


const char* NameConverter::NameOfConstant(byte* addr) const {
  return NameOfAddress(addr);
}


const char* NameConverter::NameOfCPURegister(int reg) const {
  const char* result;
  if ((0 <= reg) && (reg < 16)) {
    result = reg_names[reg];
  } else {
    result = "noreg";
  }
  return result;
}


const char* NameConverter::NameOfXMMRegister(int reg) const {
  UNREACHABLE();  // ARM does not have any XMM registers
  return "noxmmreg";
}


const char* NameConverter::NameInCode(byte* addr) const {
  // The default name converter is called for unknown code. So we will not try
  // to access any memory.
  return "";
}


//------------------------------------------------------------------------------

static NameConverter defaultConverter;

Disassembler::Disassembler() : converter_(defaultConverter) {}


Disassembler::Disassembler(const NameConverter& converter)
    : converter_(converter) {}


Disassembler::~Disassembler() {}


int Disassembler::InstructionDecode(char* buffer, const int buffer_size,
                                    byte* instruction) {
  assembler::arm::Decoder d(converter_, buffer, buffer_size);
  return d.InstructionDecode(instruction);
}


int Disassembler::ConstantPoolSizeAt(byte* instruction) {
  int instruction_bits = *(reinterpret_cast<int*>(instruction));
  if ((instruction_bits & 0xfff00000) == 0x03000000) {
    return instruction_bits & 0x0000ffff;
  } else {
    return -1;
  }
}


void Disassembler::Disassemble(FILE* f, byte* begin, byte* end) {
  Disassembler d;
  for (byte* pc = begin; pc < end;) {
    char buffer[128];
    buffer[0] = '\0';
    byte* prev_pc = pc;
    pc += d.InstructionDecode(buffer, sizeof buffer, pc);
    fprintf(f, "%p    %08x      %s\n",
            prev_pc, *reinterpret_cast<int32_t*>(prev_pc), buffer);
  }
}


}  // namespace disasm
