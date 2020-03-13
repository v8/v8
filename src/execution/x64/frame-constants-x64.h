// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_X64_FRAME_CONSTANTS_X64_H_
#define V8_EXECUTION_X64_FRAME_CONSTANTS_X64_H_

#include "src/base/bits.h"
#include "src/base/macros.h"
#include "src/execution/frame-constants.h"

namespace v8 {
namespace internal {

class EntryFrameConstants : public AllStatic {
 public:
#ifdef V8_TARGET_OS_WIN
  static constexpr int kCalleeSaveXMMRegisters = 10;
  static constexpr int kXMMRegisterSize = 16;
  static constexpr int kXMMRegistersBlockSize =
      kXMMRegisterSize * kCalleeSaveXMMRegisters;

  // This is the offset to where JSEntry pushes the current value of
  // Isolate::c_entry_fp onto the stack.
  // On x64, there are 7 pushq() and 3 Push() calls between setting up rbp and
  // pushing the c_entry_fp, plus we manually allocate kXMMRegistersBlockSize
  // bytes on the stack.
  static constexpr int kCallerFPOffset = -3 * kSystemPointerSize +
                                         -7 * kSystemPointerSize -
                                         kXMMRegistersBlockSize;

  // Stack offsets for arguments passed to JSEntry.
  static constexpr int kArgcOffset = 6 * kSystemPointerSize;
  static constexpr int kArgvOffset = 7 * kSystemPointerSize;
#else
  // This is the offset to where JSEntry pushes the current value of
  // Isolate::c_entry_fp onto the stack.
  // On x64, there are 5 pushq() and 3 Push() calls between setting up rbp and
  // pushing the c_entry_fp.
  static constexpr int kCallerFPOffset =
      -3 * kSystemPointerSize + -5 * kSystemPointerSize;
#endif
};

class WasmCompileLazyFrameConstants : public TypedFrameConstants {
 public:
  static constexpr int kNumberOfSavedGpParamRegs = 6;
  static constexpr int kNumberOfSavedFpParamRegs = 6;

  // FP-relative.
  static constexpr int kWasmInstanceOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(0);
  static constexpr int kFixedFrameSizeFromFp =
      TypedFrameConstants::kFixedFrameSizeFromFp +
      kNumberOfSavedGpParamRegs * kSystemPointerSize +
      kNumberOfSavedFpParamRegs * kSimd128Size;
};

// Frame constructed by the {WasmDebugBreak} builtin.
// After pushing the frame type marker, the builtin pushes all Liftoff cache
// registers (see liftoff-assembler-defs.h).
class WasmDebugBreakFrameConstants : public TypedFrameConstants {
 public:
  // {rax, rcx, rdx, rbx, rsi, rdi, r9}
  static constexpr uint32_t kPushedGpRegs = 0b1011001111;
  // {xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7}
  static constexpr uint32_t kPushedFpRegs = 0b11111111;

  static constexpr int kNumPushedGpRegisters =
      base::bits::CountPopulation(kPushedGpRegs);
  static constexpr int kNumPushedFpRegisters =
      base::bits::CountPopulation(kPushedFpRegs);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_X64_FRAME_CONSTANTS_X64_H_
