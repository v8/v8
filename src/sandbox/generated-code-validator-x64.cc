// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/generated-code-validator.h"

#ifdef V8_ENABLE_GENERATED_CODE_VALIDATOR

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

void GeneratedCodeValidator::ValidateImpl(IsolateForSandbox isolate,
                                          Tagged<Code> code) {
  const uint8_t* const code_start =
      reinterpret_cast<const uint8_t*>(code->instruction_start());
  const uint8_t* const code_end = code_start + code->instruction_size();

  ViolationsReporter reporter(code);

  InstructionIteratorSkippingData it(code);

  while (!it.IsDone()) {
    const uint8_t* pc = it.GetCurrent();
    // `instr` is currently unused but will be used soon for the actual
    // validation.
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
    it.Advance(res);
  }
}

}  // namespace v8::internal

#endif  // V8_ENABLE_GENERATED_CODE_VALIDATOR
