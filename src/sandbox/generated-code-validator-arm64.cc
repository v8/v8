// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is goValidatened by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/generated-code-validator.h"

#ifdef V8_ENABLE_GENERATED_CODE_VALIDATOR

#include "src/builtins/builtins.h"
#include "src/codegen/arm64/instructions-arm64.h"
#include "src/codegen/arm64/register-arm64.h"
#include "src/objects/code-inl.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include "third_party/disarm/src/disarm64.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace v8::internal {

namespace {

struct Da64InstFormatter {
  static std::string Format(const Da64Inst& instr) {
    char formatted_instruction[128] = {'\0'};
    da64_format(&instr, formatted_instruction);
    return formatted_instruction;
  }
};

static bool Da64OpEquals(const Da64Op& op1, const Da64Op& op2) {
  if (op1.type != op2.type) {
    return false;
  }

  switch (op1.type) {
    case DA_OP_NONE:
      return true;
    case DA_OP_IMMLARGE:
    case DA_OP_RELADDR:
    case DA_OP_IMMFLOAT:
      // These operand types keep their values in the Da64Inst struct. Comparing
      // such operand should not use this method.
      UNREACHABLE();
    case DA_OP_REGGP:
    case DA_OP_REGGPINC:
    case DA_OP_REGSP:
      return op1.reg == op2.reg && op1.reggp.sf == op2.reggp.sf;
    case DA_OP_REGGPEXT:
      return op1.reg == op2.reg && op1.reggpext.sf == op2.reggpext.sf &&
             op1.reggpext.ext == op2.reggpext.ext &&
             op1.reggpext.shift == op2.reggpext.shift;
    case DA_OP_REGFP:
      return op1.reg == op2.reg && op1.regfp.size == op2.regfp.size;
    case DA_OP_REGVEC:
      return op1.reg == op2.reg && op1.regvec.va == op2.regvec.va;
    case DA_OP_REGVTBL:
      return op1.reg == op2.reg && op1.regvtbl.va == op2.regvtbl.va &&
             op1.regvtbl.cnt == op2.regvtbl.cnt;
    case DA_OP_REGVIDX:
      return op1.reg == op2.reg && op1.regvidx.esize == op2.regvidx.esize &&
             op1.regvidx.elem == op2.regvidx.elem;
    case DA_OP_REGVTBLIDX:
      return op1.reg == op2.reg &&
             op1.regvtblidx.esize == op2.regvtblidx.esize &&
             op1.regvtblidx.elem == op2.regvtblidx.elem &&
             op1.regvtblidx.cnt == op2.regvtblidx.cnt;
    case DA_OP_MEMUOFF:
      return op1.reg == op2.reg && op1.uimm16 == op2.uimm16;
    case DA_OP_MEMSOFF:
    case DA_OP_MEMSOFFPRE:
    case DA_OP_MEMSOFFPOST:
      return op1.reg == op2.reg && op1.simm16 == op2.simm16;
    case DA_OP_MEMREG:
    case DA_OP_MEMREGPOST:
      return op1.reg == op2.reg && op1.memreg.sc == op2.memreg.sc &&
             op1.memreg.ext == op2.memreg.ext &&
             op1.memreg.shift == op2.memreg.shift &&
             op1.memreg.offreg == op2.memreg.offreg;
    case DA_OP_MEMINC:
      return op1.reg == op2.reg;
    case DA_OP_COND:
      return op1.cond == op2.cond;
    case DA_OP_PRFOP:
      return op1.prfop == op2.prfop;
    case DA_OP_SYSREG:
      return op1.sysreg == op2.sysreg;
    case DA_OP_IMMSMALL:
    case DA_OP_UIMM:
      return op1.uimm16 == op2.uimm16;
    case DA_OP_SIMM:
      return op1.simm16 == op2.simm16;
    case DA_OP_UIMMSHIFT:
      return op1.uimm16 == op2.uimm16 &&
             op1.immshift.mask == op2.immshift.mask &&
             op1.immshift.shift == op2.immshift.shift;
  }

  UNREACHABLE();
}

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
  InstructionChecker(Tagged<Code> code, ViolationsReporter& violations_reporter)
      : code_(code), violations_reporter_(violations_reporter), state_(code_) {}

  void Check(const uint8_t* pc, const Da64Inst& instr) {
    // REGEXP code doesn't follow the V8 ABI and instead uses standard C ABI.
    if (code_->kind() != CodeKind::REGEXP) {
      CheckNoAccessesToCageBaseRegister(pc, instr);
    }
  }

 private:
  // Verifies whether the instruction accesses the pointer compression cage base
  // register (x28) directly. The cage base register must remain read-only
  // across all generated code to preserve sandbox integrity and prevent pointer
  // corruption. In practice, generated code doesn't need to even read the cage
  // base register directly, so it's easier to enforce no accesses at all rather
  // than only no reads. This will not block using the case base register as a
  // base address for a memory operand.
  void CheckNoAccessesToCageBaseRegister(const uint8_t* pc,
                                         const Da64Inst& instr) {
    static constexpr int kMaxOperands = 5;
    static_assert(kMaxOperands == (sizeof(instr.ops) / sizeof(Da64Op)));

    switch (instr.mnem) {
      // Subs is used as a comparison to assert that a register holds a heap
      // object in the main cage.
      case DA64I_SUBS_SHIFT:
      // Add and sub instructions are used for decompressing and compressing
      // sandboxed pointers.
      case DA64I_ADD_SHIFT:
      case DA64I_SUB_SHIFT:
      // Or and Add instructions are used for decompressing tagged pointers.
      case DA64I_ADD_IMM:
      case DA64I_ORR_IMM:
      case DA64I_ORR_SHIFT:
        // Check that the target is not the cage base register.
        if (!IsCageBaseReg(instr.ops[0])) {
          return;
        }
        break;
      // Stp is used by entry and deopt builtins to save the cage base
      // register's value in C++ code.
      case DA64I_STPX:
      case DA64I_STPX_PRE:
      case DA64I_STPX_POST:
        // Stp isn't updating any registers.
        DCHECK_IMPLIES(IsCageBaseReg(instr.ops[0]),
                       Utils::IsEntryCode(code_) || Utils::IsDeoptCode(code_));
        return;
      // Ldp is used by entry and deopt builtins to restore the cage base
      // register's value in C++ code.
      case DA64I_LDPX:
      case DA64I_LDPX_PRE:
      case DA64I_LDPX_POST:
        if ((IsCageBaseReg(instr.ops[0]) || IsCageBaseReg(instr.ops[1])) &&
            (Utils::IsEntryCode(code_) || Utils::IsDeoptCode(code_))) {
          // TODO(523128533): verify the loaded value is the same as
          // the previously stored value. E.g. track changes to stack register
          // and check that stp and ldp operate on the same offset (assuming
          // calls don't violate it).
          // TODO(523128533): Deopt builtins save and restore the cage base
          // register so that the deoptimizer can update register values as
          // needed. Since the cage base register is callee saved and should not
          // change, can we avoid saving and restoring it?
          state_.is_cage_base_reg_valid_ = false;
          return;
        }
        break;
      // Ldur is used by entry builtins to initialize the cage base register.
      case DA64I_LDURX:
        if (IsCageBaseReg(instr.ops[0]) && Utils::IsEntryCode(code_) &&
            IsExpectedOperand(instr.ops[1],
                              {.type = DA_OP_MEMSOFF,
                               .reg = root_register,
                               .simm16 = IsolateData::cage_base_offset()})) {
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
      if (IsCageBaseReg(instr.ops[i])) {
        violations_reporter_.ReportViolationWithInstruction(
            pc, Da64InstFormatter::Format(instr),
            std::format(
                "Instruction accesses cage bage register at operand {0}", i));
      }
    }
  }

  // Same as IsCageBaseExpectedReg but accepts all GP register types.
  static bool IsCageBaseReg(const Da64Op& op) {
    return ((op.type == DA_OP_REGGP) || (op.type == DA_OP_REGGPEXT) ||
            (op.type == DA_OP_REGGPINC)) &&
           (op.reg == cage_base_reg);
  }

  static bool IsExpectedOperand(const Da64Op& op, const Da64Op& expected) {
    return Da64OpEquals(op, expected);
  }

  const Tagged<Code> code_;
  ViolationsReporter& violations_reporter_;
  State state_;
};

void GeneratedCodeValidator::ValidateImpl(IsolateForSandbox isolate,
                                          Tagged<Code> code) {
  DCHECK_EQ(code->instruction_size() % kInstrSize, 0);

  ViolationsReporter reporter(code);

  InstructionChecker instruction_checker(code, reporter);

  InstructionIteratorSkippingData it(code);

  while (!it.IsDone()) {
    const uint8_t* pc = it.GetCurrent();
    Da64Inst instr;
    da64_decode(*reinterpret_cast<const uint32_t*>(pc), &instr);

    if (instr.mnem == DA64I_UNKNOWN) {
      reporter.ReportDisassemblyFailed(pc, kInstrSize);
      return;
    }
    if (v8_flags.validate_generated_code_include_code) {
      reporter.RecordDisassembledInstruction(pc, kInstrSize,
                                             Da64InstFormatter::Format(instr));
    }
    instruction_checker.Check(pc, instr);
    it.Advance(kInstrSize);
  }
}

}  // namespace v8::internal

#endif  // V8_ENABLE_GENERATED_CODE_VALIDATOR
