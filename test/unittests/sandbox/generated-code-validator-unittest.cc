// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/generated-code-validator.h"

#ifdef V8_ENABLE_GENERATED_CODE_VALIDATOR

#include "src/codegen/macro-assembler-inl.h"
#include "src/heap/factory-inl.h"
#include "test/common/assembler-tester.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

using GeneratedCodeValidatorTest = TestWithContext;

namespace {

void CheckValidationSucceeds(Isolate* i_isolate, MacroAssembler& masm) {
  // Delay validation until we're ready to call it explicitly.
  v8_flags.validate_generated_code = false;
  CodeDesc desc;
  masm.GetCode(i_isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(i_isolate, desc, CodeKind::FOR_TESTING).Build();

  // This should not crash.
  v8_flags.validate_generated_code = true;
  v8_flags.validate_generated_code_non_fatal = false;
  GeneratedCodeValidator::Validate(i_isolate, *code);
}

void CheckValidationFails(Isolate* i_isolate, MacroAssembler& masm,
                          std::string expected_error) {
  // Delay validation until we're ready to call it explicitly.
  v8_flags.validate_generated_code = false;
  CodeDesc desc;
  masm.GetCode(i_isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(i_isolate, desc, CodeKind::FOR_TESTING).Build();

  // This should not crash.
  v8_flags.validate_generated_code = true;
  v8_flags.validate_generated_code_non_fatal = false;
  ASSERT_DEATH_IF_SUPPORTED(GeneratedCodeValidator::Validate(i_isolate, *code),
                            expected_error);
}

}  // namespace

#define __ masm.

TEST_F(GeneratedCodeValidatorTest, DisassembleValidCode) {
  // This validation until we're ready to call it explicitly.
  v8_flags.validate_generated_code = false;
  Isolate* i_isolate = this->i_isolate();
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(i_isolate, CodeObjectRequired{false},
                      buffer->CreateView());

#if V8_TARGET_ARCH_X64
  __ nop();
  __ ret(0);
#elif V8_TARGET_ARCH_ARM64
  __ nop();
  __ ret();
#else
#error "Unsupported architecture for GeneratedCodeValidatorTest"
#endif

  CheckValidationSucceeds(i_isolate, masm);
}

TEST_F(GeneratedCodeValidatorTest, DisassembleInvalidCode) {
  Isolate* i_isolate = this->i_isolate();
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(i_isolate, CodeObjectRequired{false},
                      buffer->CreateView());

#if V8_TARGET_ARCH_X64
  // Write a partial instruction (LOCK prefix only) to trigger decode error.
  __ db(0xF0);
#elif V8_TARGET_ARCH_ARM64
  // Write an instruction that is DA64I_UNKNOWN.
  // 0x0b205400 is known to be decoded as DA64I_UNKNOWN.
  __ db(0x00);
  __ db(0x54);
  __ db(0x20);
  __ db(0x0b);
#else
#error "Unsupported architecture for GeneratedCodeValidatorTest"
#endif

  CheckValidationFails(i_isolate, masm,
                       "Failed to disassemble invalid instruction");
}

TEST_F(GeneratedCodeValidatorTest, ValidateCageBaseModificationFails) {
  static_assert(kPtrComprCageBaseRegister != no_reg);
  Isolate* i_isolate = this->i_isolate();
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(i_isolate, CodeObjectRequired{false},
                      buffer->CreateView());

#if V8_TARGET_ARCH_X64
  __ movq(kPtrComprCageBaseRegister, Immediate(0));
  __ ret(0);
#elif V8_TARGET_ARCH_ARM64
  __ Mov(kPtrComprCageBaseRegister, 0);
  __ ret();
#else
#error "Unsupported architecture for GeneratedCodeValidatorTest"
#endif

  CheckValidationFails(i_isolate, masm,
                       "Instruction accesses cage bage register at operand 0");
}

TEST_F(GeneratedCodeValidatorTest, ValidateDecompressionPasses) {
  static_assert(kPtrComprCageBaseRegister != no_reg);
  Isolate* i_isolate = this->i_isolate();
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(i_isolate, CodeObjectRequired{false},
                      buffer->CreateView());

#if V8_TARGET_ARCH_X64
  __ orq(rcx, kPtrComprCageBaseRegister);
  __ ret(0);
#elif V8_TARGET_ARCH_ARM64
  __ Orr(x13, kPtrComprCageBaseRegister, x13);
  __ ret();
#else
#error "Unsupported architecture for GeneratedCodeValidatorTest"
#endif

  CheckValidationSucceeds(i_isolate, masm);
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_GENERATED_CODE_VALIDATOR
