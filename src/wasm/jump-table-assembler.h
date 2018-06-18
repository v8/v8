// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_JUMP_TABLE_ASSEMBLER_H_
#define V8_WASM_JUMP_TABLE_ASSEMBLER_H_

#include "src/macro-assembler.h"

namespace v8 {
namespace internal {
namespace wasm {

class JumpTableAssembler : public TurboAssembler {
  // {JumpTableAssembler} is never used during snapshot generation, and its code
  // must be independent of the code range of any isolate anyway. So just use
  // this default {IsolateData} for each {JumpTableAssembler}.
  static constexpr IsolateData GetDefaultIsolateData() {
#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_ARM64
    return IsolateData(kSerializerDisabled, kNullAddress);
#else
    return IsolateData(kSerializerDisabled);
#endif
  }

 public:
  JumpTableAssembler() : TurboAssembler(GetDefaultIsolateData(), nullptr, 0) {}

  // Emit a trampoline to a possibly far away code target.
  void EmitJumpTrampoline(Address target);
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_JUMP_TABLE_ASSEMBLER_H_
