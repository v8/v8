// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_INL_H_
#define V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_INL_H_

#include "src/baseline/baseline-compiler.h"
#include "src/execution/frame-constants.h"

namespace v8 {
namespace internal {
namespace baseline {

#define __ basm_.

// A builtin call/jump mode that is used then short builtin calls feature is
// not enabled.
constexpr BuiltinCallJumpMode kFallbackBuiltinCallJumpModeForBaseline =
    BuiltinCallJumpMode::kIndirect;

void BaselineCompiler::Prologue() {
  ASM_CODE_COMMENT(&masm_);
  // Enter the frame here, since CallBuiltin will override lr.
  __ masm()->EnterFrame(StackFrame::BASELINE);
  DCHECK_EQ(kJSFunctionRegister, kJavaScriptCallTargetRegister);
  int max_frame_size = bytecode_->max_frame_size();
  CallBuiltin<Builtin::kBaselineOutOfLinePrologue>(
      kContextRegister, kJSFunctionRegister, kJavaScriptCallArgCountRegister,
      max_frame_size, kJavaScriptCallNewTargetRegister, bytecode_);

  __ masm()->AssertSpAligned();
  PrologueFillFrame();
  __ masm()->AssertSpAligned();
}

void BaselineCompiler::PrologueFillFrame() {
  ASM_CODE_COMMENT(&masm_);
  // Inlined register frame fill
  if (v8_flags.debug_code) {
    __ masm()->CompareRoot(kInterpreterAccumulatorRegister,
                           RootIndex::kUndefinedValue);
    __ masm()->Assert(eq, AbortReason::kUnexpectedValue);
  }
  // BaselineOutOfLinePrologue already pushed one undefined slot (r0, or
  // alignment padding when register_count == 0) to keep SP 16-byte aligned.
  static_assert(InterpreterFrameConstants::kFixedSlotCountFromFp % 2 == 1);
  int remaining_slots = UnoptimizedFrameConstants::RegisterStackSlotCount(
                            bytecode_->register_count()) -
                        1;
  DCHECK_GE(remaining_slots, 0);
  DCHECK_EQ(remaining_slots % 2, 0);

  // Magic value
  const int kLoopUnrollSize = 8;
  if (remaining_slots < 2 * kLoopUnrollSize) {
    // If the frame is small enough, just unroll the frame fill completely.
    for (int i = 0; i < remaining_slots; i += 2) {
      __ masm()->Push(kInterpreterAccumulatorRegister,
                      kInterpreterAccumulatorRegister);
    }
  } else {
    BaselineAssembler::ScratchRegisterScope temps(&basm_);
    Register scratch = temps.AcquireScratch();

    // Extract the first few registers to round to the unroll size.
    int first_registers = remaining_slots % kLoopUnrollSize;
    for (int i = 0; i < first_registers; i += 2) {
      __ masm()->Push(kInterpreterAccumulatorRegister,
                      kInterpreterAccumulatorRegister);
    }
    __ Move(scratch, remaining_slots / kLoopUnrollSize);
    // We enter the loop unconditionally, so make sure we need to loop at least
    // once.
    DCHECK_GT(remaining_slots / kLoopUnrollSize, 0);
    Label loop;
    __ Bind(&loop);
    for (int i = 0; i < kLoopUnrollSize; i += 2) {
      __ masm()->Push(kInterpreterAccumulatorRegister,
                      kInterpreterAccumulatorRegister);
    }
    __ masm()->Subs(scratch, scratch, 1);
    __ masm()->B(gt, &loop);
  }

  interpreter::Register new_target_or_generator_register =
      bytecode_->incoming_new_target_or_generator_register();
  if (new_target_or_generator_register.is_valid()) {
    DCHECK_LT(new_target_or_generator_register.index(),
              bytecode_->register_count());
    __ StoreRegister(new_target_or_generator_register,
                     kJavaScriptCallNewTargetRegister);
  }
}

void BaselineCompiler::VerifyFrameSize() {
  ASM_CODE_COMMENT(&masm_);
  __ masm()->Add(x15, sp,
                 RoundUp(InterpreterFrameConstants::kFixedFrameSizeFromFp +
                             bytecode_->frame_size(),
                         2 * kSystemPointerSize));
  __ masm()->Cmp(x15, fp);
  __ masm()->Assert(eq, AbortReason::kUnexpectedStackPointer);
}

#undef __

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_INL_H_
