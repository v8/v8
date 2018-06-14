// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/jump-table-assembler.h"

#include "src/macro-assembler-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

void JumpTableAssembler::EmitJumpTrampoline(Address target) {
#if V8_TARGET_ARCH_X64
  movq(kScratchRegister, static_cast<uint64_t>(target));
  jmp(kScratchRegister);
#elif V8_TARGET_ARCH_ARM64
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  Mov(scratch, static_cast<uint64_t>(target));
  Br(scratch);
#elif V8_TARGET_ARCH_S390X
  mov(ip, Operand(bit_cast<intptr_t, Address>(target)));
  b(ip);
#else
  UNIMPLEMENTED();
#endif
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
