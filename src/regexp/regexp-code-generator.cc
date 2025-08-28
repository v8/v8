// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-code-generator.h"

#include "src/codegen/label.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/objects/fixed-array-inl.h"
#include "src/regexp/regexp-bytecode-iterator-inl.h"
#include "src/regexp/regexp-bytecodes-inl.h"

namespace v8 {
namespace internal {

#define __ masm_->

RegExpCodeGenerator::RegExpCodeGenerator(
    Isolate* isolate, RegExpMacroAssembler* masm,
    DirectHandle<TrustedByteArray> bytecode)
    : isolate_(isolate),
      zone_(isolate_->allocator(), ZONE_NAME),
      masm_(masm),
      bytecode_(bytecode),
      iter_(bytecode_),
      labels_(zone_.AllocateArray<Label>(bytecode_->length())),
      labels_used_(bytecode_->length(), &zone_) {}

RegExpCodeGenerator::Result RegExpCodeGenerator::Assemble(
    DirectHandle<String> source, RegExpFlags flags) {
  USE(isolate_);
  USE(masm_);
  PreVisitBytecodes();
  return Result::UnsupportedBytecode();
}

void RegExpCodeGenerator::PreVisitBytecodes() {
  iter_.ForEachBytecode([&]<RegExpBytecode bc>() {
    using Operands = RegExpBytecodeOperands<bc>;
    auto ensure_label = [&]<auto operand>() {
      const uint8_t* pc = iter_.current_address();
      uint32_t offset = Operands::template Get<operand>(pc);
      if (!labels_used_.Contains(offset)) {
        labels_used_.Add(offset);
        Label* label = &labels_[offset];
        new (label) Label();
      }
    };
    Operands::template ForEachOperandOfType<RegExpBytecodeOperandType::kLabel>(
        ensure_label);
  });
}

}  // namespace internal
}  // namespace v8
