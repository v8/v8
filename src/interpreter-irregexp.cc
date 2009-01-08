// Copyright 2008 the V8 project authors. All rights reserved.
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

// A simple interpreter for the Irregexp byte code.


#include "v8.h"
#include "unicode.h"
#include "utils.h"
#include "ast.h"
#include "bytecodes-irregexp.h"
#include "interpreter-irregexp.h"


namespace v8 { namespace internal {


static unibrow::Mapping<unibrow::Ecma262Canonicalize> canonicalize;


static bool BackRefMatchesNoCase(int from,
                                 int current,
                                 int len,
                                 Vector<const uc16> subject) {
  for (int i = 0; i < len; i++) {
    unibrow::uchar old_char = subject[from++];
    unibrow::uchar new_char = subject[current++];
    if (old_char == new_char) continue;
    canonicalize.get(old_char, '\0', &old_char);
    canonicalize.get(new_char, '\0', &new_char);
    if (old_char != new_char) {
      return false;
    }
  }
  return true;
}


static bool BackRefMatchesNoCase(int from,
                                 int current,
                                 int len,
                                 Vector<const char> subject) {
  for (int i = 0; i < len; i++) {
    unsigned int old_char = subject[from++];
    unsigned int new_char = subject[current++];
    if (old_char == new_char) continue;
    if (old_char - 'A' <= 'Z' - 'A') old_char |= 0x20;
    if (new_char - 'A' <= 'Z' - 'A') new_char |= 0x20;
    if (old_char != new_char) return false;
  }
  return true;
}


#ifdef DEBUG
static void TraceInterpreter(const byte* code_base,
                             const byte* pc,
                             int stack_depth,
                             int current_position,
                             uint32_t current_char,
                             int bytecode_length,
                             const char* bytecode_name) {
  if (FLAG_trace_regexp_bytecodes) {
    bool printable = (current_char < 127 && current_char >= 32);
    const char* format =
        printable ?
        "pc = %02x, sp = %d, curpos = %d, curchar = %08x (%c), bc = %s" :
        "pc = %02x, sp = %d, curpos = %d, curchar = %08x .%c., bc = %s";
    PrintF(format,
           pc - code_base,
           stack_depth,
           current_position,
           current_char,
           printable ? current_char : '.',
           bytecode_name);
    for (int i = 1; i < bytecode_length; i++) {
      printf(", %02x", pc[i]);
    }
    printf(" ");
    for (int i = 1; i < bytecode_length; i++) {
      unsigned char b = pc[i];
      if (b < 127 && b >= 32) {
        printf("%c", b);
      } else {
        printf(".");
      }
    }
    printf("\n");
  }
}


#define BYTECODE(name)                                  \
  case BC_##name:                                       \
    TraceInterpreter(code_base,                         \
                     pc,                                \
                     backtrack_sp - backtrack_stack,    \
                     current,                           \
                     current_char,                      \
                     BC_##name##_LENGTH,                \
                     #name);
#else
#define BYTECODE(name)                                  \
  case BC_##name:
#endif



template <typename Char>
static bool RawMatch(const byte* code_base,
                     Vector<const Char> subject,
                     int* registers,
                     int current,
                     uint32_t current_char) {
  const byte* pc = code_base;
  static const int kBacktrackStackSize = 10000;
  int backtrack_stack[kBacktrackStackSize];
  int backtrack_stack_space = kBacktrackStackSize;
  int* backtrack_sp = backtrack_stack;
#ifdef DEBUG
  if (FLAG_trace_regexp_bytecodes) {
    PrintF("\n\nStart bytecode interpreter\n\n");
  }
#endif
  while (true) {
    switch (*pc) {
      BYTECODE(BREAK)
        UNREACHABLE();
        return false;
      BYTECODE(PUSH_CP)
        if (--backtrack_stack_space < 0) {
          return false;  // No match on backtrack stack overflow.
        }
        *backtrack_sp++ = current + Load32(pc + 1);
        pc += BC_PUSH_CP_LENGTH;
        break;
      BYTECODE(PUSH_BT)
        if (--backtrack_stack_space < 0) {
          return false;  // No match on backtrack stack overflow.
        }
        *backtrack_sp++ = Load32(pc + 1);
        pc += BC_PUSH_BT_LENGTH;
        break;
      BYTECODE(PUSH_REGISTER)
        if (--backtrack_stack_space < 0) {
          return false;  // No match on backtrack stack overflow.
        }
        *backtrack_sp++ = registers[pc[1]];
        pc += BC_PUSH_REGISTER_LENGTH;
        break;
      BYTECODE(SET_REGISTER)
        registers[pc[1]] = Load32(pc + 2);
        pc += BC_SET_REGISTER_LENGTH;
        break;
      BYTECODE(ADVANCE_REGISTER)
        registers[pc[1]] += Load32(pc + 2);
        pc += BC_ADVANCE_REGISTER_LENGTH;
        break;
      BYTECODE(SET_REGISTER_TO_CP)
        registers[pc[1]] = current + Load32(pc + 2);
        pc += BC_SET_REGISTER_TO_CP_LENGTH;
        break;
      BYTECODE(SET_CP_TO_REGISTER)
        current = registers[pc[1]];
        pc += BC_SET_CP_TO_REGISTER_LENGTH;
        break;
      BYTECODE(SET_REGISTER_TO_SP)
        registers[pc[1]] = backtrack_sp - backtrack_stack;
        pc += BC_SET_REGISTER_TO_SP_LENGTH;
        break;
      BYTECODE(SET_SP_TO_REGISTER)
        backtrack_sp = backtrack_stack + registers[pc[1]];
        backtrack_stack_space = kBacktrackStackSize -
                                (backtrack_sp - backtrack_stack);
        pc += BC_SET_SP_TO_REGISTER_LENGTH;
        break;
      BYTECODE(POP_CP)
        backtrack_stack_space++;
        --backtrack_sp;
        current = *backtrack_sp;
        pc += BC_POP_CP_LENGTH;
        break;
      BYTECODE(POP_BT)
        backtrack_stack_space++;
        --backtrack_sp;
        pc = code_base + *backtrack_sp;
        break;
      BYTECODE(POP_REGISTER)
        backtrack_stack_space++;
        --backtrack_sp;
        registers[pc[1]] = *backtrack_sp;
        pc += BC_POP_REGISTER_LENGTH;
        break;
      BYTECODE(FAIL)
        return false;
      BYTECODE(SUCCEED)
        return true;
      BYTECODE(ADVANCE_CP)
        current += Load32(pc + 1);
        pc += BC_ADVANCE_CP_LENGTH;
        break;
      BYTECODE(GOTO)
        pc = code_base + Load32(pc + 1);
        break;
      BYTECODE(CHECK_GREEDY)
        if (current == backtrack_sp[-1]) {
          backtrack_sp--;
          backtrack_stack_space++;
          pc = code_base + Load32(pc + 1);
        } else {
          pc += BC_CHECK_GREEDY_LENGTH;
        }
        break;
      BYTECODE(LOAD_CURRENT_CHAR) {
        int pos = current + Load32(pc + 1);
        if (pos >= subject.length()) {
          pc = code_base + Load32(pc + 5);
        } else {
          current_char = subject[pos];
          pc += BC_LOAD_CURRENT_CHAR_LENGTH;
        }
        break;
      }
      BYTECODE(LOAD_CURRENT_CHAR_UNCHECKED) {
        int pos = current + Load32(pc + 1);
        current_char = subject[pos];
        pc += BC_LOAD_CURRENT_CHAR_UNCHECKED_LENGTH;
        break;
      }
      BYTECODE(LOAD_2_CURRENT_CHARS) {
        int pos = current + Load32(pc + 1);
        if (pos + 2 > subject.length()) {
          pc = code_base + Load32(pc + 5);
        } else {
          Char next = subject[pos + 1];
          current_char =
              (subject[pos] | (next << (kBitsPerByte * sizeof(Char))));
          pc += BC_LOAD_2_CURRENT_CHARS_LENGTH;
        }
        break;
      }
      BYTECODE(LOAD_2_CURRENT_CHARS_UNCHECKED) {
        int pos = current + Load32(pc + 1);
        Char next = subject[pos + 1];
        current_char = (subject[pos] | (next << (kBitsPerByte * sizeof(Char))));
        pc += BC_LOAD_2_CURRENT_CHARS_UNCHECKED_LENGTH;
        break;
      }
      BYTECODE(LOAD_4_CURRENT_CHARS) {
        ASSERT(sizeof(Char) == 1);
        int pos = current + Load32(pc + 1);
        if (pos + 4 > subject.length()) {
          pc = code_base + Load32(pc + 5);
        } else {
          Char next1 = subject[pos + 1];
          Char next2 = subject[pos + 2];
          Char next3 = subject[pos + 3];
          current_char = (subject[pos] |
                          (next1 << 8) |
                          (next2 << 16) |
                          (next3 << 24));
          pc += BC_LOAD_4_CURRENT_CHARS_LENGTH;
        }
        break;
      }
      BYTECODE(LOAD_4_CURRENT_CHARS_UNCHECKED) {
        ASSERT(sizeof(Char) == 1);
        int pos = current + Load32(pc + 1);
        Char next1 = subject[pos + 1];
        Char next2 = subject[pos + 2];
        Char next3 = subject[pos + 3];
        current_char = (subject[pos] |
                        (next1 << 8) |
                        (next2 << 16) |
                        (next3 << 24));
        pc += BC_LOAD_4_CURRENT_CHARS_UNCHECKED_LENGTH;
        break;
      }
      BYTECODE(CHECK_CHAR) {
        uint32_t c = Load32(pc + 1);
        if (c == current_char) {
          pc = code_base + Load32(pc + 5);
        } else {
          pc += BC_CHECK_CHAR_LENGTH;
        }
        break;
      }
      BYTECODE(CHECK_NOT_CHAR) {
        uint32_t c = Load32(pc + 1);
        if (c != current_char) {
          pc = code_base + Load32(pc + 5);
        } else {
          pc += BC_CHECK_NOT_CHAR_LENGTH;
        }
        break;
      }
      BYTECODE(AND_CHECK_CHAR) {
        uint32_t c = Load32(pc + 1);
        if (c == (current_char & Load32(pc + 5))) {
          pc = code_base + Load32(pc + 9);
        } else {
          pc += BC_AND_CHECK_CHAR_LENGTH;
        }
        break;
      }
      BYTECODE(AND_CHECK_NOT_CHAR) {
        uint32_t c = Load32(pc + 1);
        if (c != (current_char & Load32(pc + 5))) {
          pc = code_base + Load32(pc + 9);
        } else {
          pc += BC_AND_CHECK_NOT_CHAR_LENGTH;
        }
        break;
      }
      BYTECODE(MINUS_AND_CHECK_NOT_CHAR) {
        uint32_t c = Load16(pc + 1);
        uint32_t minus = Load16(pc + 3);
        uint32_t mask = Load16(pc + 5);
        if (c != ((current_char - minus) & mask)) {
          pc = code_base + Load32(pc + 7);
        } else {
          pc += BC_MINUS_AND_CHECK_NOT_CHAR_LENGTH;
        }
        break;
      }
      BYTECODE(CHECK_LT) {
        uint32_t limit = Load16(pc + 1);
        if (current_char < limit) {
          pc = code_base + Load32(pc + 3);
        } else {
          pc += BC_CHECK_LT_LENGTH;
        }
        break;
      }
      BYTECODE(CHECK_GT) {
        uint32_t limit = Load16(pc + 1);
        if (current_char > limit) {
          pc = code_base + Load32(pc + 3);
        } else {
          pc += BC_CHECK_GT_LENGTH;
        }
        break;
      }
      BYTECODE(CHECK_REGISTER_LT)
        if (registers[pc[1]] < Load16(pc + 2)) {
          pc = code_base + Load32(pc + 4);
        } else {
          pc += BC_CHECK_REGISTER_LT_LENGTH;
        }
        break;
      BYTECODE(CHECK_REGISTER_GE)
        if (registers[pc[1]] >= Load16(pc + 2)) {
          pc = code_base + Load32(pc + 4);
        } else {
          pc += BC_CHECK_REGISTER_GE_LENGTH;
        }
        break;
      BYTECODE(CHECK_REGISTER_EQ_POS)
        if (registers[pc[1]] == current) {
          pc = code_base + Load32(pc + 2);
        } else {
          pc += BC_CHECK_REGISTER_EQ_POS_LENGTH;
        }
        break;
      BYTECODE(LOOKUP_MAP1) {
        // Look up character in a bitmap.  If we find a 0, then jump to the
        // location at pc + 7.  Otherwise fall through!
        int index = current_char - Load16(pc + 1);
        byte map = code_base[Load32(pc + 3) + (index >> 3)];
        map = ((map >> (index & 7)) & 1);
        if (map == 0) {
          pc = code_base + Load32(pc + 7);
        } else {
          pc += BC_LOOKUP_MAP1_LENGTH;
        }
        break;
      }
      BYTECODE(LOOKUP_MAP2) {
        // Look up character in a half-nibble map.  If we find 00, then jump to
        // the location at pc + 7.   If we find 01 then jump to location at
        // pc + 11, etc.
        int index = (current_char - Load16(pc + 1)) << 1;
        byte map = code_base[Load32(pc + 3) + (index >> 3)];
        map = ((map >> (index & 7)) & 3);
        if (map < 2) {
          if (map == 0) {
            pc = code_base + Load32(pc + 7);
          } else {
            pc = code_base + Load32(pc + 11);
          }
        } else {
          if (map == 2) {
            pc = code_base + Load32(pc + 15);
          } else {
            pc = code_base + Load32(pc + 19);
          }
        }
        break;
      }
      BYTECODE(LOOKUP_MAP8) {
        // Look up character in a byte map.  Use the byte as an index into a
        // table that follows this instruction immediately.
        int index = current_char - Load16(pc + 1);
        byte map = code_base[Load32(pc + 3) + index];
        const byte* new_pc = code_base + Load32(pc + 7) + (map << 2);
        pc = code_base + Load32(new_pc);
        break;
      }
      BYTECODE(LOOKUP_HI_MAP8) {
        // Look up high byte of this character in a byte map.  Use the byte as
        // an index into a table that follows this instruction immediately.
        int index = (current_char >> 8) - pc[1];
        byte map = code_base[Load32(pc + 2) + index];
        const byte* new_pc = code_base + Load32(pc + 6) + (map << 2);
        pc = code_base + Load32(new_pc);
        break;
      }
      BYTECODE(CHECK_NOT_REGS_EQUAL)
        if (registers[pc[1]] == registers[pc[2]]) {
          pc += BC_CHECK_NOT_REGS_EQUAL_LENGTH;
        } else {
          pc = code_base + Load32(pc + 3);
        }
        break;
      BYTECODE(CHECK_NOT_BACK_REF) {
        int from = registers[pc[1]];
        int len = registers[pc[1] + 1] - from;
        if (from < 0 || len <= 0) {
          pc += BC_CHECK_NOT_BACK_REF_LENGTH;
          break;
        }
        if (current + len > subject.length()) {
          pc = code_base + Load32(pc + 2);
          break;
        } else {
          int i;
          for (i = 0; i < len; i++) {
            if (subject[from + i] != subject[current + i]) {
              pc = code_base + Load32(pc + 2);
              break;
            }
          }
          if (i < len) break;
          current += len;
        }
        pc += BC_CHECK_NOT_BACK_REF_LENGTH;
        break;
      }
      BYTECODE(CHECK_NOT_BACK_REF_NO_CASE) {
        int from = registers[pc[1]];
        int len = registers[pc[1] + 1] - from;
        if (from < 0 || len <= 0) {
          pc += BC_CHECK_NOT_BACK_REF_NO_CASE_LENGTH;
          break;
        }
        if (current + len > subject.length()) {
          pc = code_base + Load32(pc + 2);
          break;
        } else {
          if (BackRefMatchesNoCase(from, current, len, subject)) {
            current += len;
            pc += BC_CHECK_NOT_BACK_REF_NO_CASE_LENGTH;
          } else {
            pc = code_base + Load32(pc + 2);
          }
        }
        break;
      }
      BYTECODE(CHECK_NOT_AT_START)
        if (current == 0) {
          pc += BC_CHECK_NOT_AT_START_LENGTH;
        } else {
          pc = code_base + Load32(pc + 1);
        }
        break;
      default:
        UNREACHABLE();
        break;
    }
  }
}


bool IrregexpInterpreter::Match(Handle<ByteArray> code_array,
                                Handle<String> subject,
                                int* registers,
                                int start_position) {
  ASSERT(subject->IsFlat(StringShape(*subject)));

  AssertNoAllocation a;
  const byte* code_base = code_array->GetDataStartAddress();
  StringShape subject_shape(*subject);
  uc16 previous_char = '\n';
  if (subject_shape.IsAsciiRepresentation()) {
    Vector<const char> subject_vector = subject->ToAsciiVector();
    if (start_position != 0) previous_char = subject_vector[start_position - 1];
    return RawMatch(code_base,
                    subject_vector,
                    registers,
                    start_position,
                    previous_char);
  } else {
    Vector<const uc16> subject_vector = subject->ToUC16Vector();
    if (start_position != 0) previous_char = subject_vector[start_position - 1];
    return RawMatch(code_base,
                    subject_vector,
                    registers,
                    start_position,
                    previous_char);
  }
}

} }  // namespace v8::internal
