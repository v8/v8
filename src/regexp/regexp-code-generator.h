// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_CODE_GENERATOR_H_
#define V8_REGEXP_REGEXP_CODE_GENERATOR_H_

#include "src/handles/handles.h"
#include "src/objects/fixed-array.h"
#include "src/regexp/regexp-bytecode-iterator.h"
#include "src/regexp/regexp-bytecodes.h"
#include "src/regexp/regexp-error.h"
#include "src/regexp/regexp-macro-assembler.h"
#include "src/utils/bit-vector.h"

namespace v8 {
namespace internal {

class RegExpCodeGenerator final {
 public:
  RegExpCodeGenerator(Isolate* isolate, RegExpMacroAssembler* masm,
                      DirectHandle<TrustedByteArray> bytecode);

  struct Result final {
    explicit Result(DirectHandle<Code> code) : code_(code) {}

    static Result UnsupportedBytecode() {
      return Result(RegExpError::kUnsupportedBytecode);
    }

    bool Succeeded() const { return error_ == RegExpError::kNone; }
    RegExpError error() const { return error_; }
    DirectHandle<Code> code() const { return code_; }

   private:
    explicit Result(RegExpError err) : error_(err) {}

    RegExpError error_ = RegExpError::kNone;
    DirectHandle<Code> code_;
  };

  V8_NODISCARD Result Assemble(DirectHandle<String> source, RegExpFlags flags);

 private:
  // Visit all bytecodes before any code is emmited.
  // Allocates labels for all jump targets to support forward jumps.
  void PreVisitBytecodes();

  Isolate* isolate_;
  Zone zone_;
  RegExpMacroAssembler* masm_;
  DirectHandle<TrustedByteArray> bytecode_;
  RegExpBytecodeIterator iter_;
  // Zone allocated Array of Labels for each offset. Access is only valid for
  // offsets that are jump targets (indicated by labels_used_).
  Label* labels_;
  // BitVector indicating if a label for a specific offset is allocated.
  // Labels are allocated for all offsets that are jump targets.
  BitVector labels_used_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_CODE_GENERATOR_H_
