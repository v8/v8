
// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/interface-descriptors-inl.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/maglev/arm64/maglev-assembler-arm64-inl.h"
#include "src/maglev/maglev-graph.h"

namespace v8 {
namespace internal {
namespace maglev {

#define __ masm->

void MaglevAssembler::Allocate(RegisterSnapshot& register_snapshot,
                               Register object, int size_in_bytes,
                               AllocationType alloc_type,
                               AllocationAlignment alignment) {
  // TODO(victorgomes): Call the runtime for large object allocation.
  // TODO(victorgomes): Support double alignment.
  DCHECK_EQ(alignment, kTaggedAligned);
  size_in_bytes = ALIGN_TO_ALLOCATION_ALIGNMENT(size_in_bytes);
  if (v8_flags.single_generation) {
    alloc_type = AllocationType::kOld;
  }
  bool in_new_space = alloc_type == AllocationType::kYoung;
  ExternalReference top =
      in_new_space
          ? ExternalReference::new_space_allocation_top_address(isolate_)
          : ExternalReference::old_space_allocation_top_address(isolate_);
  ExternalReference limit =
      in_new_space
          ? ExternalReference::new_space_allocation_limit_address(isolate_)
          : ExternalReference::old_space_allocation_limit_address(isolate_);

  ZoneLabelRef done(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  // We are a bit short on registers, so we use the same register for {object}
  // and {new_top}. Once we have defined {new_top}, we don't use {object} until
  // {new_top} is used for the last time. And there (at the end of this
  // function), we recover the original {object} from {new_top} by subtracting
  // {size_in_bytes}.
  Register new_top = object;
  // Check if there is enough space.
  Ldr(object, ExternalReferenceAsOperand(top, scratch));
  Add(new_top, object, size_in_bytes);
  Ldr(scratch, ExternalReferenceAsOperand(limit, scratch));
  Cmp(new_top, scratch);
  // Otherwise call runtime.
  JumpToDeferredIf(
      ge,
      [](MaglevAssembler* masm, RegisterSnapshot register_snapshot,
         Register object, Builtin builtin, int size_in_bytes,
         ZoneLabelRef done) {
        // Remove {object} from snapshot, since it is the returned allocated
        // HeapObject.
        register_snapshot.live_registers.clear(object);
        register_snapshot.live_tagged_registers.clear(object);
        {
          SaveRegisterStateForCall save_register_state(masm, register_snapshot);
          using D = AllocateDescriptor;
          __ Move(D::GetRegisterParameter(D::kRequestedSize), size_in_bytes);
          __ CallBuiltin(builtin);
          save_register_state.DefineSafepoint();
          __ Move(object, kReturnRegister0);
        }
        __ jmp(*done);
      },
      register_snapshot, object,
      in_new_space ? Builtin::kAllocateRegularInYoungGeneration
                   : Builtin::kAllocateRegularInOldGeneration,
      size_in_bytes, done);
  // Store new top and tag object.
  Move(ExternalReferenceAsOperand(top, scratch), new_top);
  Add(object, object, kHeapObjectTag - size_in_bytes);
  bind(*done);
}

void MaglevAssembler::AllocateHeapNumber(RegisterSnapshot register_snapshot,
                                         Register result,
                                         DoubleRegister value) {
  // In the case we need to call the runtime, we should spill the value
  // register. Even if it is not live in the next node, otherwise the
  // allocation call might trash it.
  register_snapshot.live_double_registers.set(value);
  Allocate(register_snapshot, result, HeapNumber::kSize);
  // `Allocate` needs 2 scratch registers, so it's important to `AcquireX` after
  // `Allocate` is done and not before.
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  LoadRoot(scratch, RootIndex::kHeapNumberMap);
  StoreTaggedField(scratch, FieldMemOperand(result, HeapObject::kMapOffset));
  Str(value, FieldMemOperand(result, HeapNumber::kValueOffset));
}

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
    const int max_stack_size =
        std::max(static_cast<int>(graph->max_deopted_stack_size()),
                 max_stack_slots_used * kSystemPointerSize);
    Sub(stack_slots_size, stack_slots_size, Immediate(max_stack_size));
    LoadStackLimit(interrupt_stack_limit, StackLimitKind::kInterruptStackLimit);
    Cmp(stack_slots_size, interrupt_stack_limit);

    ZoneLabelRef deferred_call_stack_guard_return(this);
    JumpToDeferredIf(
        lo,
        [](MaglevAssembler* masm, ZoneLabelRef done, int max_stack_size) {
          ASM_CODE_COMMENT_STRING(masm, "Stack/interrupt call");
          // Save any registers that can be referenced by RegisterInput.
          // TODO(leszeks): Only push those that are used by the graph.
          __ PushAll(RegisterInput::kAllowedRegisters);
          // Push the frame size
          __ Mov(ip0, Smi::FromInt(max_stack_size * kSystemPointerSize));
          __ PushArgument(ip0);
          __ CallRuntime(Runtime::kStackGuardWithGap, 1);
          __ PopAll(RegisterInput::kAllowedRegisters);
          __ B(*done);
        },
        deferred_call_stack_guard_return, max_stack_size);
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
