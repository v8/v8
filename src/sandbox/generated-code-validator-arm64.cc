// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is goValidatened by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/generated-code-validator.h"

#ifdef V8_ENABLE_GENERATED_CODE_VALIDATOR

#include "src/codegen/arm64/instructions-arm64.h"
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

}  // namespace
void GeneratedCodeValidator::ValidateImpl(IsolateForSandbox isolate,
                                          Tagged<Code> code) {
  DCHECK_EQ(code->instruction_size() % kInstrSize, 0);

  ViolationsReporter reporter(code);

  InstructionIteratorSkippingData it(code);

  while (!it.IsDone()) {
    const uint8_t* pc = it.GetCurrent();
    // `instr` is currently unused but will be used soon for the actual
    // validation.
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
    it.Advance(kInstrSize);
  }
}

}  // namespace v8::internal

#endif  // V8_ENABLE_GENERATED_CODE_VALIDATOR
