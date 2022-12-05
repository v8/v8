
// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/deoptimizer/deoptimizer.h"
#include "src/maglev/arm64/maglev-assembler-arm64-inl.h"
#include "src/maglev/maglev-graph.h"

namespace v8 {
namespace internal {
namespace maglev {

#define __ masm->

void MaglevAssembler::Prologue(Graph* graph) {
  if (v8_flags.maglev_ool_prologue) {
    // TODO(v8:7700): Implement!
    UNREACHABLE();
  }

  CallTarget();

  BailoutIfDeoptimized();

  // Tiering support.
  // TODO(jgruber): Extract to a builtin.
  {
    UseScratchRegisterScope temps(this);
    Register flags = temps.AcquireX();
    // TODO(v8:7700): There are only 2 available scratch registers, we use x9,
    // which is a local caller saved register instead here, since
    // LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing requests a scratch
    // register as well.
    Register feedback_vector = x9;

    // Load the feedback vector.
    LoadTaggedPointerField(
        feedback_vector,
        FieldMemOperand(kJSFunctionRegister, JSFunction::kFeedbackCellOffset));
    LoadTaggedPointerField(
        feedback_vector, FieldMemOperand(feedback_vector, Cell::kValueOffset));
    AssertFeedbackVector(feedback_vector, flags);

    DeferredCodeInfo* deferred_flags_need_processing = PushDeferredCode(
        [](MaglevAssembler* masm, Register flags, Register feedback_vector) {
          ASM_CODE_COMMENT_STRING(masm, "Optimized marker check");
          // TODO(leszeks): This could definitely be a builtin that we
          // tail-call.
          __ OptimizeCodeOrTailCallOptimizedCodeSlot(flags, feedback_vector);
          __ Trap();
        },
        flags, feedback_vector);

    LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
        flags, feedback_vector, CodeKind::MAGLEV,
        &deferred_flags_need_processing->deferred_code_label);
  }

  EnterFrame(StackFrame::MAGLEV);

  // Save arguments in frame.
  // TODO(leszeks): Consider eliding this frame if we don't make any calls
  // that could clobber these registers.
  // Push the context and the JSFunction.
  Push(kContextRegister, kJSFunctionRegister);
  // Push the actual argument count and a _possible_ stack slot.
  Push(kJavaScriptCallArgCountRegister, xzr);
  int remaining_stack_slots = code_gen_state()->stack_slots() - 1;
  DCHECK_GE(remaining_stack_slots, 0);
  {
    ASM_CODE_COMMENT_STRING(this, " Stack/interrupt check");
    // Stack check. This folds the checks for both the interrupt stack limit
    // check and the real stack limit into one by just checking for the
    // interrupt limit. The interrupt limit is either equal to the real
    // stack limit or tighter. By ensuring we have space until that limit
    // after building the frame we can quickly precheck both at once.
    UseScratchRegisterScope temps(this);
    Register stack_slots_size = temps.AcquireX();
    Register interrupt_stack_limit = temps.AcquireX();
    Mov(stack_slots_size, fp);
    // Round up the stack slots and max call args separately, since both will be
    // padded by their respective uses.
    const int max_stack_slots_used = RoundUp<2>(remaining_stack_slots) +
                                     RoundUp<2>(graph->max_call_stack_args());
    Sub(stack_slots_size, stack_slots_size,
        Immediate(max_stack_slots_used * kSystemPointerSize));
    LoadStackLimit(interrupt_stack_limit, StackLimitKind::kInterruptStackLimit);
    Cmp(stack_slots_size, interrupt_stack_limit);

    ZoneLabelRef deferred_call_stack_guard_return(this);
    JumpToDeferredIf(
        lo,
        [](MaglevAssembler* masm, ZoneLabelRef done, int stack_slots) {
          ASM_CODE_COMMENT_STRING(masm, "Stack/interrupt call");
          // Save any registers that can be referenced by RegisterInput.
          // TODO(leszeks): Only push those that are used by the graph.
          __ PushAll(RegisterInput::kAllowedRegisters);
          // Push the frame size
          __ Mov(ip0, Smi::FromInt(stack_slots * kSystemPointerSize));
          __ PushArgument(ip0);
          __ CallRuntime(Runtime::kStackGuardWithGap, 1);
          __ PopAll(RegisterInput::kAllowedRegisters);
          __ B(*done);
        },
        deferred_call_stack_guard_return, remaining_stack_slots);
    bind(*deferred_call_stack_guard_return);
  }

  // Initialize stack slots.
  if (graph->tagged_stack_slots() > 0) {
    ASM_CODE_COMMENT_STRING(this, "Initializing stack slots");

    // If tagged_stack_slots is divisible by 2, we overshoot and allocate one
    // extra stack slot, otherwise we allocate exactly the right amount, since
    // one stack has already been allocated.
    int tagged_two_slots_count = graph->tagged_stack_slots() / 2;
    remaining_stack_slots -= 2 * tagged_two_slots_count;

    // Magic value. Experimentally, an unroll size of 8 doesn't seem any
    // worse than fully unrolled pushes.
    const int kLoopUnrollSize = 8;
    if (tagged_two_slots_count < kLoopUnrollSize) {
      for (int i = 0; i < tagged_two_slots_count; i++) {
        Push(xzr, xzr);
      }
    } else {
      UseScratchRegisterScope temps(this);
      Register count = temps.AcquireX();
      // Extract the first few slots to round to the unroll size.
      int first_slots = tagged_two_slots_count % kLoopUnrollSize;
      for (int i = 0; i < first_slots; ++i) {
        Push(xzr, xzr);
      }
      Move(count, tagged_two_slots_count / kLoopUnrollSize);
      // We enter the loop unconditionally, so make sure we need to loop at
      // least once.
      DCHECK_GT(tagged_two_slots_count / kLoopUnrollSize, 0);
      Label loop;
      bind(&loop);
      for (int i = 0; i < kLoopUnrollSize; ++i) {
        Push(xzr, xzr);
      }
      sub(count, count, Immediate(1));
      b(&loop, gt);
    }
  }
  if (remaining_stack_slots > 0) {
    // Round up.
    remaining_stack_slots += (remaining_stack_slots % 2);
    // Extend rsp by the size of the remaining untagged part of the frame,
    // no need to initialise these.
    sub(fp, fp, Immediate(remaining_stack_slots * kSystemPointerSize));
  }
}

void MaglevAssembler::MaybeEmitDeoptBuiltinsCall(size_t eager_deopt_count,
                                                 Label* eager_deopt_entry,
                                                 size_t lazy_deopt_count,
                                                 Label* lazy_deopt_entry) {
  ForceConstantPoolEmissionWithoutJump();

  DCHECK_GE(Deoptimizer::kLazyDeoptExitSize, Deoptimizer::kEagerDeoptExitSize);
  size_t deopt_count = eager_deopt_count + lazy_deopt_count;
  CheckVeneerPool(
      false, false,
      static_cast<int>(deopt_count) * Deoptimizer::kLazyDeoptExitSize);

  UseScratchRegisterScope scope(this);
  Register scratch = scope.AcquireX();
  if (eager_deopt_count > 0) {
    Bind(eager_deopt_entry);
    LoadEntryFromBuiltin(Builtin::kDeoptimizationEntry_Eager, scratch);
    MacroAssembler::Jump(scratch);
  }
  if (lazy_deopt_count > 0) {
    Bind(lazy_deopt_entry);
    LoadEntryFromBuiltin(Builtin::kDeoptimizationEntry_Lazy, scratch);
    MacroAssembler::Jump(scratch);
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
