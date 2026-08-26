// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/generated-code-validator.h"

#ifdef V8_ENABLE_GENERATED_CODE_VALIDATOR

#include "src/builtins/builtins.h"
#include "src/codegen/x64/register-x64.h"
#include "src/objects/code-inl.h"
#include "third_party/fadec/src/fadec.h"

namespace v8::internal {

namespace {

struct FdInstrFormatter {
  static std::string Format(const FdInstr& instr) {
    char formatted_instruction[128] = {'\0'};
    fd_format(&instr, formatted_instruction, 128);
    return formatted_instruction;
  }

  static size_t GetInstructionSize(const FdInstr& instr) {
    return FD_SIZE(&instr);
  }
};

}  // namespace

class InstructionChecker {
  static_assert(kPtrComprCageBaseRegister != no_reg);
  static constexpr int cage_base_reg = kPtrComprCageBaseRegister.code();
  static_assert(kRootRegister != no_reg);
  static constexpr int root_register = kRootRegister.code();

  using ViolationsReporter = GeneratedCodeValidator::ViolationsReporter;
  using Utils = GeneratedCodeValidator::Utils;
  using State = GeneratedCodeValidator::State;

 public:
  InstructionChecker(Tagged<Code> code, ViolationsReporter& violations_reproter)
      : code_(code), violations_reporter_(violations_reproter), state_(code_) {}

  void Check(const uint8_t* pc, const FdInstr& instr) {
    // REGEXP code doesn't follow the V8 ABI and instead uses standard C ABI.
    if (code_->kind() != CodeKind::REGEXP) {
      CheckNoAccessesToCageBaseRegister(pc, instr);
    }
  }

 private:
  // Verifies whether the instruction accesses the pointer compression cage base
  // register (r14) directly. The cage base register must remain read-only
  // across all generated code to preserve sandbox integrity and prevent pointer
  // corruption. In practice, generated code doesn't need to even read the cage
  // base register directly, so it's easier to enforce no accesses at all rather
  // than only no reads. This will not block using the case base register as a
  // base address for a memory operand.
  void CheckNoAccessesToCageBaseRegister(const uint8_t* pc,
                                         const FdInstr& instr) {
    static constexpr int kMaxOperands = 4;
    static_assert(kMaxOperands == (sizeof(instr.operands) / sizeof(FdOp)));

    switch (FD_TYPE(&instr)) {
      // Cmp is used to assert that a register holds a heap object in the main
      // cage.
      case FDI_CMP:
      // Add and sub instructions are used for decompressing and compressing
      // sandboxed pointers.
      case FDI_ADD:
      case FDI_SUB:
      // Or and Add instructions are used for decompressing tagged pointers.
      case FDI_OR:
        // Check that the target is not the cage base register.
        if (!IsCageBaseReg(instr, 0)) {
          return;
        }
        break;
      // Push is used by entry and deopt builtins to save the cage
      // base register's value in C++ code.
      case FDI_PUSH:
        // Push isn't updating any registers.
        DCHECK_IMPLIES(IsCageBaseReg(instr, 0),
                       Utils::IsEntryCode(code_) || Utils::IsDeoptCode(code_));
        return;
      // Push is used by entry and deopt builtins to restore the cage
      // base register's value in C++ code.
      case FDI_POP:
        if (IsCageBaseReg(instr, 0) &&
            (Utils::IsEntryCode(code_) || Utils::IsDeoptCode(code_))) {
          // TODO(523128533): For pop, verify the popped value is the same as
          // the previously pushed value. E.g. track changes to stack register
          // and check that push and pop operate on the same offset (assuming
          // calls don't violate it).
          // TODO(523128533): Deopt builtins save and restore the cage base
          // register so that the deoptimizer can update register values as
          // needed. Since the cage base register is callee saved and should not
          // change, can we avoid saving and restoring it?
          state_.is_cage_base_reg_valid_ = false;
          return;
        }
        break;
      // Mov is used by entry builtins to initialize the cage base register.
      // This instructions should only appear once per entry builtin.
      case FDI_MOV:
        if (IsCageBaseReg(instr, 0) && Utils::IsEntryCode(code_) &&
            IsExpectedMemoryOperand(instr, 1, root_register, FD_REG_NONE, 0,
                                    IsolateData::cage_base_offset())) {
          state_.is_cage_base_reg_valid_ = true;
          return;
        }
        break;
      default:
        // All other cases are not expected to accesses the cage base register
        // directly and fall through to the generic handling below.
        break;
    }

    // Check that no operand is the cage base register.
    for (int i = 0; i < kMaxOperands; i++) {
      if (IsCageBaseReg(instr, i)) {
        violations_reporter_.ReportViolationWithInstruction(
            pc, FdInstrFormatter::Format(instr),
            std::format(
                "Instruction accesses cage bage register at operand {0}", i));
      }
    }
  }

  static bool IsCageBaseReg(const FdInstr& instr, int op_idx) {
    return FD_OP_TYPE(&instr, op_idx) == FD_OT_REG &&
           (FD_OP_REG_TYPE(&instr, op_idx) == FD_RT_GPL ||
            FD_OP_REG_TYPE(&instr, op_idx) == FD_RT_GPH) &&
           FD_OP_REG(&instr, op_idx) == cage_base_reg;
  }

  static bool IsExpectedMemoryOperand(const FdInstr& instr, int op_idx,
                                      int base, int index, int scale,
                                      int displacement) {
    return (FD_OP_TYPE(&instr, op_idx) == FD_OT_MEM) &&
           (FD_OP_BASE(&instr, op_idx) == base) &&
           (FD_OP_INDEX(&instr, op_idx) == index) &&
           (FD_OP_SCALE(&instr, op_idx) == scale) &&
           (FD_OP_DISP(&instr, op_idx) == displacement);
  }

  const Tagged<Code> code_;
  ViolationsReporter& violations_reporter_;
  State state_;
};

void GeneratedCodeValidator::ValidateImpl(IsolateForSandbox isolate,
                                          Tagged<Code> code) {
  const uint8_t* const code_start =
      reinterpret_cast<const uint8_t*>(code->instruction_start());
  const uint8_t* const code_end = code_start + code->instruction_size();

  ViolationsReporter reporter(code);

  InstructionChecker instruction_checker(code, reporter);

  InstructionIteratorSkippingData it(code);

  while (!it.IsDone()) {
    const uint8_t* pc = it.GetCurrent();
    FdInstr instr;
    size_t remaining_length = code_end - pc;
    int res = fd_decode(pc, remaining_length, 64, 0, &instr);
    if (res < 0) {
      static constexpr size_t kMaxInstructionSize = 15;
      reporter.ReportDisassemblyFailed(
          pc, std::min(remaining_length, kMaxInstructionSize));
      return;
    }
    DCHECK_LE(res, remaining_length);
    if (v8_flags.validate_generated_code_include_code) {
      reporter.RecordDisassembledInstruction(pc, res,
                                             FdInstrFormatter::Format(instr));
    }
    instruction_checker.Check(pc, instr);
    it.Advance(res);
  }
}

}  // namespace v8::internal

#endif  // V8_ENABLE_GENERATED_CODE_VALIDATOR
