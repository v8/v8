// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/d8/memory-access-information.h"

#ifdef V8_ENABLE_HARDWARE_WATCHPOINT_SUPPORT

#include <stdlib.h>
#include <string.h>
#include <sys/user.h>

#include <initializer_list>

#include "src/base/logging.h"

namespace v8 {

MemoryAccessInformation ParseMemoryAccessInformationFromInstruction(
    const char* insn_pos, struct user_regs_struct& regs) {
  const char* space_pos = strchr(insn_pos, ' ');
  CHECK_NOT_NULL(space_pos);
  size_t mnem_len = space_pos - insn_pos;

  int access_width = 8;
  MemoryAccessInformation::Extension extension =
      MemoryAccessInformation::kNoExtend;

  char suffix;
  if (mnem_len >= 6 && (memcmp(insn_pos, "movzx", 5) == 0 ||
                        memcmp(insn_pos, "movsx", 5) == 0)) {
    extension = (insn_pos[3] == 'z') ? MemoryAccessInformation::kZeroExtend
                                     : MemoryAccessInformation::kSignExtend;
    suffix = insn_pos[5];
  } else {
    DCHECK_GT(mnem_len, 0);
    suffix = insn_pos[mnem_len - 1];
    // On x64, instructions writing to 32-bit sub-registers ('l') implicitly
    // zero-extend across the full 64-bit destination register, whereas 8-bit
    // ('b') and 16-bit ('w') sub-register operations leave the upper bits
    // unchanged.
    if (suffix == 'l') {
      extension = MemoryAccessInformation::kZeroExtend;
    }
  }

  if (suffix == 'b') {
    access_width = 1;
  } else if (suffix == 'w') {
    access_width = 2;
  } else if (suffix == 'l' || suffix == 'd') {
    access_width = 4;
  } else if (suffix == 'q') {
    access_width = 8;
  }

  if (memcmp(insn_pos, "cmp", 3) == 0 || memcmp(insn_pos, "test", 4) == 0) {
    if (memcmp(insn_pos, "cmpxchg", 7) == 0) {
      return {.kind = MemoryAccessInformation::kCmpxchg,
              .result_reg = nullptr,
              .xmm_reg_index = -1,
              .access_width = access_width,
              .extension = extension};
    }
    return {.kind = MemoryAccessInformation::kCmp,
            .result_reg = nullptr,
            .xmm_reg_index = -1,
            .access_width = access_width,
            .extension = extension};
  }
  // TODO(clemensb): Implement more instructions if necessary.
  if (memcmp(insn_pos, "mov", 3) != 0 && memcmp(insn_pos, "sub", 3) != 0 &&
      memcmp(insn_pos, "add", 3) != 0 && memcmp(insn_pos, "vmov", 4) != 0 &&
      memcmp(insn_pos, "or", 2) != 0 && memcmp(insn_pos, "xor", 3) != 0 &&
      memcmp(insn_pos, "and", 3) != 0) {
    FATAL("Not a recognized instruction: %s\n", insn_pos);
  }

  const char* comma_pos = strchr(space_pos + 1, ',');
  CHECK_NOT_NULL(comma_pos);
  bool mem_op_on_lhs = space_pos[1] == '[';
  bool mem_op_on_rhs = comma_pos[1] == '[';
  // Be extra careful to interpret the disassembly correctly.
  CHECK_EQ(mem_op_on_lhs, comma_pos[-1] == ']');
  CHECK_EQ(mem_op_on_rhs, space_pos[strlen(space_pos) - 1] == ']');
  CHECK_EQ(mem_op_on_lhs, !mem_op_on_rhs);
  if (mem_op_on_lhs) {
    return {.kind = MemoryAccessInformation::kWrite,
            .result_reg = nullptr,
            .xmm_reg_index = -1,
            .access_width = access_width,
            .extension = extension};
  }
  const char* op = space_pos + 1;
  auto match_reg = [&](std::initializer_list<const char*> aliases) {
    for (const char* alias : aliases) {
      size_t len = strlen(alias);
      if (memcmp(op, alias, len) == 0 &&
          (op[len] == ',' || op[len] == ' ' || op[len] == '\0')) {
        return true;
      }
    }
    return false;
  };

  reg_value_type* matched_reg = nullptr;
  // clang-format off
  if (match_reg({"rax", "eax", "ax", "al", "ah"})) matched_reg = &regs.rax;
  else if (match_reg({"rcx", "ecx", "cx", "cl", "ch"})) matched_reg = &regs.rcx;
  else if (match_reg({"rdx", "edx", "dx", "dl", "dh"})) matched_reg = &regs.rdx;
  else if (match_reg({"rbx", "ebx", "bx", "bl", "bh"})) matched_reg = &regs.rbx;
  else if (match_reg({"rsp", "esp", "sp", "spl"})) matched_reg = &regs.rsp;
  else if (match_reg({"rbp", "ebp", "bp", "bpl"})) matched_reg = &regs.rbp;
  else if (match_reg({"rsi", "esi", "si", "sil"})) matched_reg = &regs.rsi;
  else if (match_reg({"rdi", "edi", "di", "dil"})) matched_reg = &regs.rdi;
  else if (match_reg({"r8", "r8d", "r8w", "r8b"})) matched_reg = &regs.r8;
  else if (match_reg({"r9", "r9d", "r9w", "r9b"})) matched_reg = &regs.r9;
  else if (match_reg({"r10", "r10d", "r10w", "r10b"})) matched_reg = &regs.r10;
  else if (match_reg({"r11", "r11d", "r11w", "r11b"})) matched_reg = &regs.r11;
  else if (match_reg({"r12", "r12d", "r12w", "r12b"})) matched_reg = &regs.r12;
  else if (match_reg({"r13", "r13d", "r13w", "r13b"})) matched_reg = &regs.r13;
  else if (match_reg({"r14", "r14d", "r14w", "r14b"})) matched_reg = &regs.r14;
  else if (match_reg({"r15", "r15d", "r15w", "r15b"})) matched_reg = &regs.r15;
  // clang-format on

  if (matched_reg != nullptr) {
    return {.kind = MemoryAccessInformation::kRead,
            .result_reg = matched_reg,
            .xmm_reg_index = -1,
            .access_width = access_width,
            .extension = extension};
  }

  if (memcmp(space_pos + 1, "xmm", 3) == 0 ||
      memcmp(space_pos + 1, "ymm", 3) == 0) {
    int reg_num = atoi(space_pos + 4);
    CHECK_LE(0, reg_num);
    CHECK_LE(reg_num, 15);
    return {.kind = MemoryAccessInformation::kRead,
            .result_reg = nullptr,
            .xmm_reg_index = reg_num,
            .access_width = access_width,
            .extension = extension};
  }

  FATAL("Could not read register name: %s", insn_pos);
}

}  // namespace v8

#endif  // V8_ENABLE_HARDWARE_WATCHPOINT_SUPPORT
