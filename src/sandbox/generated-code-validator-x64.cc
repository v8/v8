// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/code-kind.h"
#include "src/sandbox/generated-code-validator.h"
#include "v8-internal.h"

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
  static constexpr int cage_base_register = kPtrComprCageBaseRegister.code();
  static_assert(kRootRegister != no_reg);
  static constexpr int root_register = kRootRegister.code();

  using ViolationsReporter = GeneratedCodeValidator::ViolationsReporter;
  using Utils = GeneratedCodeValidator::Utils;
  using State = GeneratedCodeValidator::State;

 public:
  InstructionChecker(Isolate* isolate, Tagged<Code> code,
                     ViolationsReporter& violations_reproter)
      : isolate_(isolate),
        code_(code),
        expected_root_register_value_(
            ExternalReference::isolate_root(isolate_).raw()),
        violations_reporter_(violations_reproter),
        state_(isolate, code_) {}

  void Check(const uint8_t* pc, const FdInstr& instr) {
    // REGEXP code doesn't follow the V8 ABI and instead uses standard C ABI.
    if (code_->kind() != CodeKind::REGEXP) {
      CheckNoWritesToRestrictedRegisters(pc, instr);
    }
  }

 private:
  // Verifies whether the instruction writes to the pointer compression cage
  // base register (r14) or the root register (r13) directly. These registers
  // must remain read-only across all generated code to preserve sandbox
  // integrity and prevent pointer corruption. This will not block using these
  // register as a base address for a memory operand.
  void CheckNoWritesToRestrictedRegisters(const uint8_t* pc,
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
        // Check that the target is not the cage base register or root register.
        if (!IsCageBaseReg(instr, 0)) {
          return;
        }
        break;
      // Push is used by entry and deopt builtins to save the cage
      // base and root registers' values in C++ code.
      case FDI_PUSH:
        // Push isn't updating any registers.
        DCHECK_IMPLIES(IsCageBaseReg(instr, 0),
                       Utils::IsEntryCode(code_) || Utils::IsDeoptCode(code_));
        DCHECK_IMPLIES(IsRootReg(instr, 0),
                       Utils::IsEntryCode(code_) || Utils::IsDeoptCode(code_));
        return;
      // Pop is used by entry and deopt builtins to restore the cage
      // base and root registers' values in C++ code.
      case FDI_POP:
        // TODO(523128533): Verify the popped values are the same as
        // the previously pushed values. E.g. track changes to stack register
        // and check that push and pop operate on the same offset (assuming
        // calls don't violate it).
        // TODO(523128533): Deopt builtins save and restore the these
        // registers so that the deoptimizer can update register values as
        // needed. Since these registers are callee saved and should not
        // change, can we avoid saving and restoring it?
        if (Utils::IsEntryCode(code_) || Utils::IsDeoptCode(code_)) {
          if (IsCageBaseReg(instr, 0)) {
            state_.is_cage_base_reg_valid_ = false;
            return;
          }
          if (IsRootReg(instr, 0)) {
            state_.is_root_reg_valid_ = false;
            return;
          }
        }
        break;
      // Mov is used by entry builtins to initialize the cage base and root
      // registers.
      case FDI_MOV:
      case FDI_MOVABS:
        if (!IsCageBaseReg(instr, 0) && !IsRootReg(instr, 0)) {
          return;
        }
        if (Utils::IsEntryCode(code_)) {
          if (IsCageBaseReg(instr, 0) &&
              IsExpectedMemoryOperand(instr, 1, root_register, FD_REG_NONE, 0,
                                      IsolateData::cage_base_offset())) {
            DCHECK_EQ(FDI_MOV, FD_TYPE(&instr));
            CHECK(state_.is_root_reg_valid_);
            state_.is_cage_base_reg_valid_ = true;
            return;
          }
          if (IsRootReg(instr, 0) && IsValidRootRegInitialization(instr)) {
            return;
          }
        }
        break;
      default:
        // All other cases are not expected to accesses the cage base register
        // or root register directly and fall through to the generic handling
        // below.
        break;
    }

    // Check that no operand is the cage base register or the root register.
    for (int i = 0; i < kMaxOperands; i++) {
      if (IsCageBaseReg(instr, i)) {
        violations_reporter_.ReportViolationWithInstruction(
            pc, FdInstrFormatter::Format(instr),
            std::format(
                "Instruction accesses cage bage register at operand {0}", i));
      }
      if (IsRootReg(instr, i)) {
        violations_reporter_.ReportViolationWithInstruction(
            pc, FdInstrFormatter::Format(instr),
            std::format("Instruction accesses root register at operand {0}",
                        i));
      }
    }
  }

  static bool IsExpectedReg(const FdInstr& instr, int op_idx,
                            int expected_reg) {
    return FD_OP_TYPE(&instr, op_idx) == FD_OT_REG &&
           (FD_OP_REG_TYPE(&instr, op_idx) == FD_RT_GPL ||
            FD_OP_REG_TYPE(&instr, op_idx) == FD_RT_GPH) &&
           FD_OP_REG(&instr, op_idx) == expected_reg;
  }

  static bool IsRootReg(const FdInstr& instr, int op_idx) {
    return IsExpectedReg(instr, op_idx, root_register);
  }

  static bool IsCageBaseReg(const FdInstr& instr, int op_idx) {
    return IsExpectedReg(instr, op_idx, cage_base_register);
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

  bool IsValidRootRegInitialization(const FdInstr& instr) {
    DCHECK(IsRootReg(instr, 0));
    switch (FD_TYPE(&instr)) {
      case FDI_MOVABS:
        DCHECK_EQ(FD_OT_IMM, FD_OP_TYPE(&instr, 1));
        state_.current_root_reg_value_ = FD_OP_IMM(&instr, 1);
        state_.is_root_reg_valid_ =
            state_.current_root_reg_value_ == expected_root_register_value_;
        break;
      case FDI_MOV:
        state_.is_root_reg_valid_ =
            (code_->kind() == CodeKind::BUILTIN) &&
            IsExpectedReg(instr, 1, kCArgRegs[0].code());
        // Assumes builtins receive the correct value as their first
        // argument.
        state_.current_root_reg_value_ = state_.is_root_reg_valid_
                                             ? expected_root_register_value_
                                             : kNullAddress;
        break;
      default:
        break;
    }
    return state_.is_root_reg_valid_;
  }

  Isolate* const isolate_;
  const Tagged<Code> code_;
  const Address expected_root_register_value_;
  ViolationsReporter& violations_reporter_;
  State state_;
};

void GeneratedCodeValidator::ValidateImpl(Isolate* isolate, Tagged<Code> code) {
  const uint8_t* const code_start =
      reinterpret_cast<const uint8_t*>(code->instruction_start());
  const uint8_t* const code_end = code_start + code->instruction_size();

  ViolationsReporter reporter(code);

  InstructionChecker instruction_checker(isolate, code, reporter);

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
