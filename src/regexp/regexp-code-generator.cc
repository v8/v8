// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-code-generator.h"

#include "src/common/globals.h"

namespace v8 {
namespace internal {

#define __ masm_->

RegExpCodeGenerator::RegExpCodeGenerator(Isolate* isolate,
                                         RegExpMacroAssembler* masm)
    : isolate_(isolate), masm_(masm) {}

RegExpCodeGenerator::Result RegExpCodeGenerator::Assemble(
    DirectHandle<TrustedByteArray> bytecode) {
  USE(isolate_);
  USE(masm_);
  return Result::UnsupportedBytecode();
}

}  // namespace internal
}  // namespace v8
