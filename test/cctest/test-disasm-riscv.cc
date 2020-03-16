// Copyright 2012 the V8 project authors. All rights reserved.
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
//

#include <stdlib.h>

#include "src/init/v8.h"

#include "src/codegen/macro-assembler.h"
#include "src/debug/debug.h"
#include "src/diagnostics/disasm.h"
#include "src/diagnostics/disassembler.h"
#include "src/execution/frames-inl.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

bool prev_instr_compact_branch = false;

bool DisassembleAndCompare(byte* pc, const char* compare_string) {
  disasm::NameConverter converter;
  disasm::Disassembler disasm(converter);
  EmbeddedVector<char, 128> disasm_buffer;

  if (prev_instr_compact_branch) {
    disasm.InstructionDecode(disasm_buffer, pc);
    pc += 4;
  }

  disasm.InstructionDecode(disasm_buffer, pc);

  if (strcmp(compare_string, disasm_buffer.begin()) != 0) {
    fprintf(stderr,
            "expected: \n"
            "%s\n"
            "disassembled: \n"
            "%s\n\n",
            compare_string, disasm_buffer.begin());
    return false;
  }
  return true;
}


// Set up V8 to a state where we can at least run the assembler and
// disassembler. Declare the variables and allocate the data structures used
// in the rest of the macros.
#define SET_UP()                                             \
  CcTest::InitializeVM();                                    \
  Isolate* isolate = CcTest::i_isolate();                    \
  HandleScope scope(isolate);                                \
  byte* buffer = reinterpret_cast<byte*>(malloc(4 * 1024));  \
  Assembler assm(AssemblerOptions{},                         \
                 ExternalAssemblerBuffer(buffer, 4 * 1024)); \
  bool failure = false;

// This macro assembles one instruction using the preallocated assembler and
// disassembles the generated instruction, comparing the output to the expected
// value. If the comparison fails an error message is printed, but the test
// continues to run until the end.
#define COMPARE(asm_, compare_string) \
  { \
    int pc_offset = assm.pc_offset(); \
    byte *progcounter = &buffer[pc_offset]; \
    assm.asm_; \
    if (!DisassembleAndCompare(progcounter, compare_string)) failure = true; \
  }


// Verify that all invocations of the COMPARE macro passed successfully.
// Exit with a failure if at least one of the tests failed.
#define VERIFY_RUN()                            \
  if (failure) {                                \
    FATAL("MIPS Disassembler tests failed.\n"); \
  }

#define COMPARE_PC_REL_COMPACT(asm_, compare_string, offset)                   \
  {                                                                            \
    int pc_offset = assm.pc_offset();                                          \
    byte *progcounter = &buffer[pc_offset];                                    \
    char str_with_address[100];                                                \
    prev_instr_compact_branch = assm.IsPrevInstrCompactBranch();               \
    if (prev_instr_compact_branch) {                                           \
      snprintf(str_with_address, sizeof(str_with_address), "%s -> %p",         \
               compare_string,                                                 \
               static_cast<void *>(progcounter + 8 + (offset * 4)));           \
    } else {                                                                   \
      snprintf(str_with_address, sizeof(str_with_address), "%s -> %p",         \
               compare_string,                                                 \
               static_cast<void *>(progcounter + 4 + (offset * 4)));           \
    }                                                                          \
    assm.asm_;                                                                 \
    if (!DisassembleAndCompare(progcounter, str_with_address)) failure = true; \
  }

#define COMPARE_PC_REL(asm_, compare_string, offset)                           \
  {                                                                            \
    int pc_offset = assm.pc_offset();                                          \
    byte *progcounter = &buffer[pc_offset];                                    \
    char str_with_address[100];                                                \
    snprintf(str_with_address, sizeof(str_with_address), "%s -> %p",           \
             compare_string, static_cast<void *>(progcounter + (offset * 4))); \
    assm.asm_;                                                                 \
    if (!DisassembleAndCompare(progcounter, str_with_address)) failure = true; \
  }

#define COMPARE_MSA_BRANCH(asm_, compare_string, offset)                       \
  {                                                                            \
    int pc_offset = assm.pc_offset();                                          \
    byte* progcounter = &buffer[pc_offset];                                    \
    char str_with_address[100];                                                \
    snprintf(str_with_address, sizeof(str_with_address), "%s -> %p",           \
             compare_string,                                                   \
             static_cast<void*>(progcounter + 4 + (offset * 4)));              \
    assm.asm_;                                                                 \
    if (!DisassembleAndCompare(progcounter, str_with_address)) failure = true; \
  }

#define COMPARE_PC_JUMP(asm_, compare_string, target)                          \
  {                                                                            \
    int pc_offset = assm.pc_offset();                                          \
    byte* progcounter = &buffer[pc_offset];                                    \
    char str_with_address[100];                                                \
    int instr_index = (target >> 2) & kImm26Mask;                              \
    snprintf(                                                                  \
        str_with_address, sizeof(str_with_address), "%s %p -> %p",             \
        compare_string, reinterpret_cast<void*>(target),                       \
        reinterpret_cast<void*>(((uint64_t)(progcounter + 1) & ~0xFFFFFFF) |   \
                                (instr_index << 2)));                          \
    assm.asm_;                                                                 \
    if (!DisassembleAndCompare(progcounter, str_with_address)) failure = true; \
  }

#define GET_PC_REGION(pc_region)                                         \
  {                                                                      \
    int pc_offset = assm.pc_offset();                                    \
    byte* progcounter = &buffer[pc_offset];                              \
    pc_region = reinterpret_cast<int64_t>(progcounter + 4) & ~0xFFFFFFF; \
  }

TEST(Type0) {
  SET_UP();

  COMPARE(RV_addw(a0, a1, a2),
          "00c5853b       addw    a0, a1, a2");
  COMPARE(RV_add(a0, a1, a2),
          "00c58533       add     a0, a1, a2");
  COMPARE(RV_addw(a6, a7, t0),
          "0058883b       addw    a6, a7, t0");
  COMPARE(RV_add(a6, a7, t0),
          "00588833       add     a6, a7, t0");
  COMPARE(RV_addw(t4, t6, fp),
          "008f8ebb       addw    t4, t6, s0");
  COMPARE(RV_add(s3, s9, s11),
          "01bc89b3       add     s3, s9, s11");

  // TODO: Add more tests!

  VERIFY_RUN();
}

}  // namespace internal
}  // namespace v8
