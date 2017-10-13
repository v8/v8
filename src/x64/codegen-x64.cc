// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/x64/codegen-x64.h"

#if V8_TARGET_ARCH_X64

#include "src/codegen.h"
#include "src/macro-assembler.h"
#include "src/x64/assembler-x64-inl.h"

namespace v8 {
namespace internal {

#define __ masm.


UnaryMathFunctionWithIsolate CreateSqrtFunction(Isolate* isolate) {
  size_t actual_size;
  // Allocate buffer in executable space.
  byte* buffer = static_cast<byte*>(base::OS::Allocate(
      1 * KB, &actual_size, true, isolate->heap()->GetRandomMmapAddr()));
  if (buffer == nullptr) return nullptr;

  MacroAssembler masm(isolate, buffer, static_cast<int>(actual_size),
                      CodeObjectRequired::kNo);
  // xmm0: raw double input.
  // Move double input into registers.
  __ Sqrtsd(xmm0, xmm0);
  __ Ret();

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DCHECK(!RelocInfo::RequiresRelocation(isolate, desc));

  Assembler::FlushICache(isolate, buffer, actual_size);
  base::OS::ProtectCode(buffer, actual_size);
  return FUNCTION_CAST<UnaryMathFunctionWithIsolate>(buffer);
}

#undef __

Operand StackArgumentsAccessor::GetArgumentOperand(int index) {
  DCHECK(index >= 0);
  int receiver = (receiver_mode_ == ARGUMENTS_CONTAIN_RECEIVER) ? 1 : 0;
  int displacement_to_last_argument =
      base_reg_ == rsp ? kPCOnStackSize : kFPOnStackSize + kPCOnStackSize;
  displacement_to_last_argument += extra_displacement_to_last_argument_;
  if (argument_count_reg_ == no_reg) {
    // argument[0] is at base_reg_ + displacement_to_last_argument +
    // (argument_count_immediate_ + receiver - 1) * kPointerSize.
    DCHECK(argument_count_immediate_ + receiver > 0);
    return Operand(base_reg_, displacement_to_last_argument +
        (argument_count_immediate_ + receiver - 1 - index) * kPointerSize);
  } else {
    // argument[0] is at base_reg_ + displacement_to_last_argument +
    // argument_count_reg_ * times_pointer_size + (receiver - 1) * kPointerSize.
    return Operand(base_reg_, argument_count_reg_, times_pointer_size,
        displacement_to_last_argument + (receiver - 1 - index) * kPointerSize);
  }
}


}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_X64
