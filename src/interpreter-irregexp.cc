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


#ifdef DEBUG
static void TraceInterpreter(const byte* code_base,
                             const byte* pc,
                             int stack_depth,
                             int current_position,
                             int bytecode_length,
                             const char* bytecode_name) {
  if (FLAG_trace_regexp_bytecodes) {
    PrintF("pc = %02x, sp = %d, current = %d, bc = %s",
            pc - code_base,
            stack_depth,
            current_position,
            bytecode_name);
    for (int i = 1; i < bytecode_length; i++) {
      printf(", %02x", pc[i]);
    }
    printf("\n");
  }
}


# define BYTECODE(name) case BC_##name:                                       \
                          TraceInterpreter(code_base,                         \
                                           pc,                                \
                                           backtrack_sp - backtrack_stack,    \
                                           current,                           \
                                           BC_##name##_LENGTH,                \
                                           #name);
#else
# define BYTECODE(name) case BC_##name:  // NOLINT
#endif



static bool RawMatch(const byte* code_base,
                     Vector<const uc16> subject,
                     int* registers,
                     int current) {
  const byte* pc = code_base;
  static const int kBacktrackStackSize = 10000;
  int backtrack_stack[kBacktrackStackSize];
  int backtrack_stack_space = kBacktrackStackSize;
  int* backtrack_sp = backtrack_stack;
  int current_char = -1;
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
      BYTECODE(CHECK_CHAR) {
        int c = Load16(pc + 1);
        if (c == current_char) {
          pc = code_base + Load32(pc + 3);
        } else {
          pc += BC_CHECK_CHAR_LENGTH;
        }
        break;
      }
      BYTECODE(CHECK_NOT_CHAR) {
        int c = Load16(pc + 1);
        if (c != current_char) {
          pc = code_base + Load32(pc + 3);
        } else {
          pc += BC_CHECK_NOT_CHAR_LENGTH;
        }
        break;
      }
      BYTECODE(OR_CHECK_NOT_CHAR) {
        int c = Load16(pc + 1);
        if (c != (current_char | Load16(pc + 3))) {
          pc = code_base + Load32(pc + 5);
        } else {
          pc += BC_OR_CHECK_NOT_CHAR_LENGTH;
        }
        break;
      }
      BYTECODE(MINUS_OR_CHECK_NOT_CHAR) {
        int c = Load16(pc + 1);
        int m = Load16(pc + 3);
        if (c != ((current_char - m) | m)) {
          pc = code_base + Load32(pc + 5);
        } else {
          pc += BC_MINUS_OR_CHECK_NOT_CHAR_LENGTH;
        }
        break;
      }
      BYTECODE(CHECK_LT) {
        int limit = Load16(pc + 1);
        if (current_char < limit) {
          pc = code_base + Load32(pc + 3);
        } else {
          pc += BC_CHECK_LT_LENGTH;
        }
        break;
      }
      BYTECODE(CHECK_GT) {
        int limit = Load16(pc + 1);
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
      BYTECODE(CHECK_NOT_BACK_REF) {
        int from = registers[pc[1]];
        int len = registers[pc[1] + 1] - from;
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
        if (current + len > subject.length()) {
          pc = code_base + Load32(pc + 2);
          break;
        } else {
          if (BackRefMatchesNoCase(from, current, len, subject)) {
            pc += BC_CHECK_NOT_BACK_REF_NO_CASE_LENGTH;
          } else {
            pc = code_base + Load32(pc + 2);
          }
        }
        break;
      }
      default:
        UNREACHABLE();
        break;
    }
  }
}


bool IrregexpInterpreter::Match(Handle<ByteArray> code_array,
                                Handle<String> subject16,
                                int* registers,
                                int start_position) {
  ASSERT(StringShape(*subject16).IsTwoByteRepresentation());
  ASSERT(subject16->IsFlat(StringShape(*subject16)));


  AssertNoAllocation a;
  const byte* code_base = code_array->GetDataStartAddress();
  return RawMatch(code_base,
                  Vector<const uc16>(subject16->GetTwoByteData(),
                                     subject16->length()),
                  registers,
                  start_position);
}

} }  // namespace v8::internal
