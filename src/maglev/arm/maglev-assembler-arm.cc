// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/interface-descriptors-inl.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/maglev/maglev-assembler-inl.h"
#include "src/maglev/maglev-graph.h"

namespace v8 {
namespace internal {
namespace maglev {

#define __ masm->

void MaglevAssembler::Allocate(RegisterSnapshot register_snapshot,
                               Register object, int size_in_bytes,
                               AllocationType alloc_type,
                               AllocationAlignment alignment) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::AllocateHeapNumber(RegisterSnapshot register_snapshot,
                                         Register result,
                                         DoubleRegister value) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::StoreTaggedFieldWithWriteBarrier(
    Register object, int offset, Register value,
    RegisterSnapshot register_snapshot, ValueIsCompressed value_is_compressed,
    ValueCanBeSmi value_can_be_smi) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::ToBoolean(Register value, CheckType check_type,
                                ZoneLabelRef is_true, ZoneLabelRef is_false,
                                bool fallthrough_when_true) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::TestTypeOf(
    Register object, interpreter::TestTypeOfFlags::LiteralFlag literal,
    Label* is_true, Label::Distance true_distance, bool fallthrough_when_true,
    Label* is_false, Label::Distance false_distance,
    bool fallthrough_when_false) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::Prologue(Graph* graph) {
  ScratchRegisterScope temps(this);
  temps.Include({r4, r8});
  if (!graph->is_osr()) {
    BailoutIfDeoptimized();
  }

  CHECK_IMPLIES(graph->is_osr(), !graph->has_recursive_calls());
  if (graph->has_recursive_calls()) {
    bind(code_gen_state()->entry_label());
  }

  // Tiering support.
  // TODO(jgruber): Extract to a builtin.
  if (v8_flags.turbofan && !graph->is_osr()) {
    ScratchRegisterScope temps(this);
    Register flags = temps.Acquire();
    Register feedback_vector = temps.Acquire();

    Label* deferred_flags_need_processing = MakeDeferredCode(
        [](MaglevAssembler* masm, Register flags, Register feedback_vector) {
          ASM_CODE_COMMENT_STRING(masm, "Optimized marker check");
          // TODO(leszeks): This could definitely be a builtin that we
          // tail-call.
          __ OptimizeCodeOrTailCallOptimizedCodeSlot(flags, feedback_vector);
          __ Trap();
        },
        flags, feedback_vector);

    Move(feedback_vector,
         compilation_info()->toplevel_compilation_unit()->feedback().object());
    LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
        flags, feedback_vector, CodeKind::MAGLEV,
        deferred_flags_need_processing);
  }

  if (graph->is_osr()) {
    MAGLEV_NOT_IMPLEMENTED();
  }

  EnterFrame(StackFrame::MAGLEV);
  // Save arguments in frame.
  // TODO(leszeks): Consider eliding this frame if we don't make any calls
  // that could clobber these registers.
  Push(kContextRegister);
  Push(kJSFunctionRegister);              // Callee's JS function.
  Push(kJavaScriptCallArgCountRegister);  // Actual argument count.

  // Initialize stack slots.
  if (graph->tagged_stack_slots() > 0) {
    ASM_CODE_COMMENT_STRING(this, "Initializing stack slots");
    ScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Move(scratch, 0);

    // Magic value. Experimentally, an unroll size of 8 doesn't seem any
    // worse than fully unrolled pushes.
    const int kLoopUnrollSize = 8;
    int tagged_slots = graph->tagged_stack_slots();
    if (tagged_slots < kLoopUnrollSize) {
      // If the frame is small enough, just unroll the frame fill
      // completely.
      for (int i = 0; i < tagged_slots; ++i) {
        Push(scratch);
      }
    } else {
      // Extract the first few slots to round to the unroll size.
      int first_slots = tagged_slots % kLoopUnrollSize;
      for (int i = 0; i < first_slots; ++i) {
        Push(scratch);
      }
      Register unroll_counter = temps.Acquire();
      Move(unroll_counter, tagged_slots / kLoopUnrollSize);
      // We enter the loop unconditionally, so make sure we need to loop at
      // least once.
      DCHECK_GT(tagged_slots / kLoopUnrollSize, 0);
      Label loop;
      bind(&loop);
      for (int i = 0; i < kLoopUnrollSize; ++i) {
        Push(scratch);
      }
      sub(unroll_counter, unroll_counter, Operand(1), SetCC);
      b(kGreaterThan, &loop);
    }
  }
  if (graph->untagged_stack_slots() > 0) {
    // Extend rsp by the size of the remaining untagged part of the frame,
    // no need to initialise these.
    sub(sp, sp, Operand(graph->untagged_stack_slots() * kSystemPointerSize));
  }
}

void MaglevAssembler::MaybeEmitDeoptBuiltinsCall(size_t eager_deopt_count,
                                                 Label* eager_deopt_entry,
                                                 size_t lazy_deopt_count,
                                                 Label* lazy_deopt_entry) {
  CheckConstPool(true, false);
}

void MaglevAssembler::AllocateTwoByteString(RegisterSnapshot register_snapshot,
                                            Register result, int length) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::StringFromCharCode(RegisterSnapshot register_snapshot,
                                         Label* char_code_fits_one_byte,
                                         Register result, Register char_code,
                                         Register scratch) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::LoadSingleCharacterString(Register result,
                                                Register char_code,
                                                Register scratch) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::StringCharCodeOrCodePointAt(
    BuiltinStringPrototypeCharCodeOrCodePointAt::Mode mode,
    RegisterSnapshot& register_snapshot, Register result, Register string,
    Register index, Register instance_type, Label* result_fits_one_byte) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::TruncateDoubleToInt32(Register dst, DoubleRegister src) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::TryTruncateDoubleToInt32(Register dst, DoubleRegister src,
                                               Label* fail) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::StringLength(Register result, Register string) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::StoreFixedArrayElementWithWriteBarrier(
    Register array, Register index, Register value,
    RegisterSnapshot register_snapshot) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::StoreFixedArrayElementNoWriteBarrier(Register array,
                                                           Register index,
                                                           Register value) {
  MAGLEV_NOT_IMPLEMENTED();
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
