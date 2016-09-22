// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_X87

#include "src/ic/ic.h"
#include "src/ic/ic-compiler.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)


void PropertyICCompiler::GenerateRuntimeSetProperty(
    MacroAssembler* masm, LanguageMode language_mode) {
  STATIC_ASSERT(StoreWithVectorDescriptor::kStackArgumentsCount == 3);
  // Current stack layout:
  // - esp[12]   -- value
  // - esp[8]    -- slot
  // - esp[4]    -- vector
  // - esp[0]    -- return address

  __ mov(Operand(esp, 12), StoreDescriptor::ReceiverRegister());
  __ mov(Operand(esp, 8), StoreDescriptor::NameRegister());
  __ mov(Operand(esp, 4), StoreDescriptor::ValueRegister());
  __ pop(ebx);
  __ push(Immediate(Smi::FromInt(language_mode)));
  __ push(ebx);  // return address

  // Do tail-call to runtime routine.
  __ TailCallRuntime(Runtime::kSetProperty);
}


#undef __
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_X87
