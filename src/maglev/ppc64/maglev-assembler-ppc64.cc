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

namespace {
void SubSizeAndTagObject(MaglevAssembler* masm, Register object,
                         Register size_in_bytes) {
  __ SubS64(object, object, size_in_bytes);
  __ AddS64(object, object, Operand(kHeapObjectTag));
}

void SubSizeAndTagObject(MaglevAssembler* masm, Register object,
                         int size_in_bytes) {
  DCHECK(is_int20(kHeapObjectTag - size_in_bytes));
  __ AddS64(object, object, Operand(kHeapObjectTag - size_in_bytes), r0);
}

template <typename T>
void AllocateRaw(MaglevAssembler* masm, Isolate* isolate,
                 RegisterSnapshot register_snapshot, Register object,
                 T size_in_bytes, AllocationType alloc_type,
                 AllocationAlignment alignment) {
  // TODO(victorgomes): Call the runtime for large object allocation.
  // TODO(victorgomes): Support double alignment.
  DCHECK(masm->allow_allocate());
  DCHECK_EQ(alignment, kTaggedAligned);
  if (v8_flags.single_generation) {
    alloc_type = AllocationType::kOld;
  }
  ExternalReference top = SpaceAllocationTopAddress(isolate, alloc_type);
  ExternalReference limit = SpaceAllocationLimitAddress(isolate, alloc_type);
  ZoneLabelRef done(masm);
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  // We are a bit short on registers, so we use the same register for {object}
  // and {new_top}. Once we have defined {new_top}, we don't use {object} until
  // {new_top} is used for the last time. And there (at the end of this
  // function), we recover the original {object} from {new_top} by subtracting
  // {size_in_bytes}.
  Register new_top = object;
  // Check if there is enough space.
  __ LoadU64(object, __ ExternalReferenceAsOperand(top, scratch), r0);
  __ AddS64(object, object, size_in_bytes);
  __ LoadU64(scratch, __ ExternalReferenceAsOperand(limit, scratch), r0);
  __ CmpU64(new_top, scratch);
  // Otherwise call runtime.
  __ JumpToDeferredIf(kUnsignedGreaterThanEqual, AllocateSlow<T>,
                      register_snapshot, object, AllocateBuiltin(alloc_type),
                      size_in_bytes, done);
  // Store new top and tag object.
  __ Move(__ ExternalReferenceAsOperand(top, scratch), new_top);
  SubSizeAndTagObject(masm, object, size_in_bytes);
  __ bind(*done);
}
}  // namespace

void MaglevAssembler::Allocate(RegisterSnapshot register_snapshot,
                               Register object, int size_in_bytes,
                               AllocationType alloc_type,
                               AllocationAlignment alignment) {
  AllocateRaw(this, isolate_, register_snapshot, object, size_in_bytes,
              alloc_type, alignment);
}

void MaglevAssembler::Allocate(RegisterSnapshot register_snapshot,
                               Register object, Register size_in_bytes,
                               AllocationType alloc_type,
                               AllocationAlignment alignment) {
  AllocateRaw(this, isolate_, register_snapshot, object, size_in_bytes,
              alloc_type, alignment);
}

void MaglevAssembler::OSRPrologue(Graph* graph) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();

  DCHECK(graph->is_osr());
  CHECK(!graph->has_recursive_calls());

  uint32_t source_frame_size =
      graph->min_maglev_stackslots_for_unoptimized_frame_size();

  if (v8_flags.debug_code) {
    AddS64(scratch, sp,
           Operand(source_frame_size * kSystemPointerSize +
                   StandardFrameConstants::kFixedFrameSizeFromFp),
           r0);
    CmpU64(scratch, fp);
    Assert(eq, AbortReason::kOsrUnexpectedStackSize);
  }

  uint32_t target_frame_size =
      graph->tagged_stack_slots() + graph->untagged_stack_slots();
  CHECK_LE(source_frame_size, target_frame_size);

  if (source_frame_size < target_frame_size) {
    ASM_CODE_COMMENT_STRING(this, "Growing frame for OSR");
    uint32_t additional_tagged =
        source_frame_size < graph->tagged_stack_slots()
            ? graph->tagged_stack_slots() - source_frame_size
            : 0;
    if (additional_tagged) {
      Move(scratch, 0);
    }
    for (size_t i = 0; i < additional_tagged; ++i) {
      Push(scratch);
    }
    uint32_t size_so_far = source_frame_size + additional_tagged;
    CHECK_LE(size_so_far, target_frame_size);
    if (size_so_far < target_frame_size) {
      SubS64(sp, sp,
             Operand((target_frame_size - size_so_far) * kSystemPointerSize),
             r0);
    }
  }
}

void MaglevAssembler::Prologue(Graph* graph) {
  TemporaryRegisterScope temps(this);
  temps.Include({r7, r9});
  Register scratch = temps.AcquireScratch();
  DCHECK(!graph->is_osr());

  BailoutIfDeoptimized();

  if (graph->has_recursive_calls()) {
    bind(code_gen_state()->entry_label());
  }
#ifndef V8_ENABLE_LEAPTIERING
  // Tiering support.
  if (v8_flags.turbofan) {
    using D = MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor;
    Register flags = D::GetRegisterParameter(D::kFlags);
    Register feedback_vector = D::GetRegisterParameter(D::kFeedbackVector);
    DCHECK(!AreAliased(feedback_vector, kJavaScriptCallArgCountRegister,
                       kJSFunctionRegister, kContextRegister,
                       kJavaScriptCallNewTargetRegister));
    DCHECK(!temps.Available().has(flags));
    DCHECK(!temps.Available().has(feedback_vector));
    Move(feedback_vector,
         compilation_info()->toplevel_compilation_unit()->feedback().object());

    Label flags_need_processing, done;
    LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
        flags, feedback_vector, CodeKind::MAGLEV, &flags_need_processing);
    b(&done);
    bind(&flags_need_processing);
    TailCallBuiltin(Builtin::kMaglevOptimizeCodeOrTailCallOptimizedCodeSlot);
    bind(&done);
  }
#endif  // !V8_ENABLE_LEAPTIERING

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
      Register unroll_counter = temps.AcquireScratch();
      Move(unroll_counter, tagged_slots / kLoopUnrollSize);
      // We enter the loop unconditionally, so make sure we need to loop at
      // least once.
      DCHECK_GT(tagged_slots / kLoopUnrollSize, 0);
      Label loop;
      bind(&loop);
      for (int i = 0; i < kLoopUnrollSize; ++i) {
        Push(scratch);
      }
      SubS32(unroll_counter, unroll_counter, Operand(1));
      CmpS32(unroll_counter, Operand(0), r0);
      bgt(&loop);
    }
  }
  if (graph->untagged_stack_slots() > 0) {
    // Extend rsp by the size of the remaining untagged part of the frame,
    // no need to initialise these.
    SubS64(sp, sp, Operand(graph->untagged_stack_slots() * kSystemPointerSize),
           r0);
  }
}

void MaglevAssembler::MaybeEmitDeoptBuiltinsCall(size_t eager_deopt_count,
                                                 Label* eager_deopt_entry,
                                                 size_t lazy_deopt_count,
                                                 Label* lazy_deopt_entry) {}

void MaglevAssembler::LoadSingleCharacterString(Register result,
                                                Register char_code,
                                                Register scratch) {
  DCHECK_NE(char_code, scratch);
  if (v8_flags.debug_code) {
    CmpU32(char_code, Operand(String::kMaxOneByteCharCode), r0);
    Assert(le, AbortReason::kUnexpectedValue);
  }
  Register table = scratch;
  AddS64(table, kRootRegister,
         Operand(RootRegisterOffsetForRootIndex(
             RootIndex::kFirstSingleCharacterString)),
         r0);
  ShiftLeftU64(result, char_code, Operand(kSystemPointerSizeLog2));
  LoadU64(result, MemOperand(table, result), r0);
}

void MaglevAssembler::StringFromCharCode(RegisterSnapshot register_snapshot,
                                         Label* char_code_fits_one_byte,
                                         Register result, Register char_code,
                                         Register scratch,
                                         CharCodeMaskMode mask_mode) {
  AssertZeroExtended(char_code);
  DCHECK_NE(char_code, scratch);
  ZoneLabelRef done(this);
  if (mask_mode == CharCodeMaskMode::kMustApplyMask) {
    AndU64(char_code, char_code, Operand(0xFFFF));
  }
  CmpU32(char_code, Operand(String::kMaxOneByteCharCode), r0);
  JumpToDeferredIf(
      kUnsignedGreaterThan,
      [](MaglevAssembler* masm, RegisterSnapshot register_snapshot,
         ZoneLabelRef done, Register result, Register char_code,
         Register scratch) {
        // Be sure to save {char_code}. If it aliases with {result}, use
        // the scratch register.
        // TODO(victorgomes): This is probably not needed any more, because
        // we now ensure that results registers don't alias with inputs/temps.
        // Confirm, and drop this check.
        if (char_code == result) {
          __ Move(scratch, char_code);
          char_code = scratch;
        }
        DCHECK(char_code != result);
        DCHECK(!register_snapshot.live_tagged_registers.has(char_code));
        register_snapshot.live_registers.set(char_code);
        __ AllocateTwoByteString(register_snapshot, result, 1);
        __ StoreU16(
            char_code,
            FieldMemOperand(result, OFFSET_OF_DATA_START(SeqTwoByteString)),
            r0);
        __ b(*done);
      },
      register_snapshot, done, result, char_code, scratch);
  if (char_code_fits_one_byte != nullptr) {
    bind(char_code_fits_one_byte);
  }
  LoadSingleCharacterString(result, char_code, scratch);
  bind(*done);
}

void MaglevAssembler::StringCharCodeOrCodePointAt(
    BuiltinStringPrototypeCharCodeOrCodePointAt::Mode mode,
    RegisterSnapshot& register_snapshot, Register result, Register string,
    Register index, Register instance_type, Register scratch2,
    Label* result_fits_one_byte) {
  ASM_CODE_COMMENT(this);
  ZoneLabelRef done(this);
  Label seq_string;
  Label cons_string;
  Label sliced_string;

  Label* deferred_runtime_call = MakeDeferredCode(
      [](MaglevAssembler* masm,
         BuiltinStringPrototypeCharCodeOrCodePointAt::Mode mode,
         RegisterSnapshot register_snapshot, ZoneLabelRef done, Register result,
         Register string, Register index) {
        DCHECK(!register_snapshot.live_registers.has(result));
        DCHECK(!register_snapshot.live_registers.has(string));
        DCHECK(!register_snapshot.live_registers.has(index));
        {
          SaveRegisterStateForCall save_register_state(masm, register_snapshot);
          __ SmiTag(index);
          __ Push(string, index);
          __ Move(kContextRegister, masm->native_context().object());
          // This call does not throw nor can deopt.
          if (mode ==
              BuiltinStringPrototypeCharCodeOrCodePointAt::kCodePointAt) {
            __ CallRuntime(Runtime::kStringCodePointAt);
          } else {
            DCHECK_EQ(mode,
                      BuiltinStringPrototypeCharCodeOrCodePointAt::kCharCodeAt);
            __ CallRuntime(Runtime::kStringCharCodeAt);
          }
          save_register_state.DefineSafepoint();
          __ SmiUntag(kReturnRegister0);
          __ Move(result, kReturnRegister0);
        }
        __ b(*done);
      },
      mode, register_snapshot, done, result, string, index);

  // We might need to try more than one time for ConsString, SlicedString and
  // ThinString.
  Label loop;
  bind(&loop);

  if (v8_flags.debug_code) {
    // Check if {string} is a string.
    AssertObjectTypeInRange(string, FIRST_STRING_TYPE, LAST_STRING_TYPE,
                            AbortReason::kUnexpectedValue);

    Register scratch = instance_type;

    LoadU32(scratch, FieldMemOperand(string, offsetof(String, length_)), r0);
    CmpS32(index, scratch);
    Check(lt, AbortReason::kUnexpectedValue);
  }

  // Get instance type.
  LoadInstanceType(instance_type, string);

  {
    TemporaryRegisterScope temps(this);
    Register representation = temps.AcquireScratch();

    // TODO(victorgomes): Add fast path for external strings.
    AndU32(representation, instance_type, Operand(kStringRepresentationMask),
           r0);
    CmpS32(representation, Operand(kSeqStringTag), r0);
    beq(&seq_string);
    AndU32(representation, representation, Operand(kConsStringTag), r0, SetRC);
    beq(&cons_string, cr0);
    CmpS32(representation, Operand(kSlicedStringTag), r0);
    beq(&sliced_string);
    CmpS32(representation, Operand(kThinStringTag), r0);
    bne(deferred_runtime_call);
    // Fallthrough to thin string.
  }

  // Is a thin string.
  {
    LoadTaggedField(string,
                    FieldMemOperand(string, offsetof(ThinString, actual_)));
    b(&loop);
  }

  bind(&sliced_string);
  {
    TemporaryRegisterScope temps(this);
    Register offset = temps.AcquireScratch();

    LoadAndUntagTaggedSignedField(offset, string,
                                  offsetof(SlicedString, offset_));
    LoadTaggedField(string, string, offsetof(SlicedString, parent_));
    AddS32(index, index, offset, SetRC);
    b(&loop);
  }

  bind(&cons_string);
  {
    // Reuse {instance_type} register here, since CompareRoot requires a scratch
    // register as well.
    Register second_string = instance_type;
    LoadU64(second_string,
            FieldMemOperand(string, offsetof(ConsString, second_)), r0);
    CompareRoot(second_string, RootIndex::kempty_string);
    bne(deferred_runtime_call);
    LoadTaggedField(string,
                    FieldMemOperand(string, offsetof(ConsString, first_)));
    b(&loop);  // Try again with first string.
  }

  bind(&seq_string);
  {
    Label two_byte_string;
    AndU32(instance_type, instance_type, Operand(kStringEncodingMask), r0);
    CmpS32(instance_type, Operand(kTwoByteStringTag), r0);
    beq(&two_byte_string);
    // The result of one-byte string will be the same for both modes
    // (CharCodeAt/CodePointAt), since it cannot be the first half of a
    // surrogate pair.
    SeqOneByteStringCharCodeAt(result, string, index);
    b(result_fits_one_byte);

    bind(&two_byte_string);
    // {instance_type} is unused from this point, so we can use as scratch.
    Register scratch = instance_type;
    ShiftLeftU64(scratch, index, Operand(1));
    AddS64(scratch, scratch,
           Operand(OFFSET_OF_DATA_START(SeqTwoByteString) - kHeapObjectTag),
           r0);

    if (mode == BuiltinStringPrototypeCharCodeOrCodePointAt::kCharCodeAt) {
      LoadU16(result, MemOperand(string, scratch), r0);
    } else {
      DCHECK_EQ(mode,
                BuiltinStringPrototypeCharCodeOrCodePointAt::kCodePointAt);
      Register string_backup = string;
      if (result == string) {
        string_backup = scratch2;
        Move(string_backup, string);
      }
      LoadU16(result, MemOperand(string, scratch), r0);

      Register first_code_point = scratch;
      AndU32(first_code_point, result, Operand(0xfc00));
      CmpS32(first_code_point, Operand(0xd800), r0);
      bne(*done);

      Register length = scratch;
      LoadU32(length, FieldMemOperand(string, offsetof(String, length_)), r0);
      AddS32(index, index, Operand(1));
      CmpS32(index, length);
      bge(*done);

      Register second_code_point = scratch;
      ShiftLeftU32(index, index, Operand(1));
      AddS32(index, index,
             Operand(OFFSET_OF_DATA_START(SeqTwoByteString) - kHeapObjectTag),
             r0);
      LoadU16(second_code_point, MemOperand(string_backup, index), r0);

      // {index} is not needed at this point.
      Register scratch2 = index;
      AndU32(scratch2, second_code_point, Operand(0xfc00), r0);
      CmpS32(scratch2, Operand(0xdc00), r0);
      bne(*done);

      int surrogate_offset = 0x10000 - (0xd800 << 10) - 0xdc00;
      AddS32(second_code_point, second_code_point, Operand(surrogate_offset),
             r0);
      ShiftLeftU32(result, result, Operand(10));
      AddS32(result, result, second_code_point);
    }

    // Fallthrough.
  }

  bind(*done);

  if (v8_flags.debug_code) {
    // We make sure that the user of this macro is not relying in string and
    // index to not be clobbered.
    if (result != string) {
      Move(string, 0xdeadbeef);
    }
    if (result != index) {
      Move(index, 0xdeadbeef);
    }
  }
}

void MaglevAssembler::SeqOneByteStringCharCodeAt(Register result,
                                                 Register string,
                                                 Register index) {
  ASM_CODE_COMMENT(this);
  TemporaryRegisterScope scope(this);
  Register scratch = scope.AcquireScratch();
  if (v8_flags.debug_code) {
    // Check if {string} is a string.
    AssertNotSmi(string);
    LoadMap(scratch, string);
    CompareInstanceTypeRange(scratch, scratch, FIRST_STRING_TYPE,
                             LAST_STRING_TYPE);
    Check(kUnsignedLessThanEqual, AbortReason::kUnexpectedValue);

    // Check if {string} is a sequential one-byte string.
    AndInt32(scratch, kStringRepresentationAndEncodingMask);
    CompareInt32AndAssert(scratch, kSeqOneByteStringTag, kEqual,
                          AbortReason::kUnexpectedValue);

    LoadInt32(scratch, FieldMemOperand(string, offsetof(String, length_)));
    CompareInt32AndAssert(index, scratch, kUnsignedLessThan,
                          AbortReason::kUnexpectedValue);
  }

  AddS64(scratch, string, index);
  LoadU8(result,
         FieldMemOperand(scratch, OFFSET_OF_DATA_START(SeqOneByteString)), r0);
}

void MaglevAssembler::CountLeadingZerosInt32(Register dst, Register src) {
  cntlzw(dst, src);
}

void MaglevAssembler::TruncateDoubleToInt32(Register dst, DoubleRegister src) {
  ZoneLabelRef done(this);
  Label* slow_path = MakeDeferredCode(
      [](MaglevAssembler* masm, DoubleRegister src, Register dst,
         ZoneLabelRef done) {
        __ mflr(r0);
        __ push(r0);
        __ AllocateStackSpace(kDoubleSize);
        __ StoreF64(src, MemOperand(sp));
        __ CallBuiltin(Builtin::kDoubleToI);
        __ LoadU64(dst, MemOperand(sp));
        __ addi(sp, sp, Operand(kDoubleSize));
        __ pop(r0);
        __ mtlr(r0);
        __ Jump(*done);
      },
      src, dst, done);
  TemporaryRegisterScope temps(this);
  DoubleRegister temp = temps.AcquireScratchDouble();
  TryInlineTruncateDoubleToI(dst, src, *done, temp);
  Jump(slow_path);
  bind(*done);
  // Zero extend the converted value to complete the truncation.
  ZeroExtend<int>(dst, dst);
}

void MaglevAssembler::TryTruncateDoubleToInt32(Register dst, DoubleRegister src,
                                               Label* fail) {
  TemporaryRegisterScope temps(this);
  DoubleRegister temp = temps.AcquireScratchDouble();
  Register scratch = temps.AcquireScratch();
  Label done;

  // Convert the input float64 value to int32.
  ConvertDoubleToInt64(src, dst, temp);
  SignedExtend<int>(dst, dst);

  // Convert that int32 value back to float64.
  ConvertIntToDouble(dst, temp);

  // Check that the result of the float64->int32->float64 is equal to the input
  // (i.e. that the conversion didn't truncate.
  fcmpu(src, temp);
  JumpIf(ne, fail);

  // Check if {input} is -0.
  CmpS32(dst, Operand::Zero(), r0);
  JumpIf(ne, &done);

  // In case of 0, we need to check the high bits for the IEEE -0 pattern.
  {
    MovDoubleToInt64(scratch, src);
    ShiftRightS64(scratch, scratch, Operand(63));
    CmpS64(scratch, Operand::Zero(), r0);
    JumpIf(lt, fail);
  }

  bind(&done);
}

void MaglevAssembler::TryTruncateDoubleToUint32(Register dst,
                                                DoubleRegister src,
                                                Label* fail) {
  TemporaryRegisterScope temps(this);
  DoubleRegister temp = temps.AcquireScratchDouble();
  Register scratch = temps.AcquireScratch();
  Label done;

  // Convert the input float64 value to uint32.
  ConvertDoubleToUnsignedInt64(src, dst, temp);
  ZeroExtend<int>(dst, dst);

  // Convert that uint32 value back to float64.
  ConvertUnsignedIntToDouble(dst, temp);

  // Check that the result of the float64->uint32->float64 is equal to the input
  // (i.e. that the conversion didn't truncate.
  fcmpu(src, temp);
  JumpIf(ne, fail);

  // Check if {input} is -0.
  CmpS32(dst, Operand::Zero(), r0);
  JumpIf(ne, &done);

  // In case of 0, we need to check the high bits for the IEEE -0 pattern.
  {
    MovDoubleToInt64(scratch, src);
    ShiftRightS64(scratch, scratch, Operand(63));
    CmpS64(scratch, Operand(0), r0);
    JumpIf(lt, fail);
  }

  bind(&done);
}

void MaglevAssembler::TryChangeFloat64ToIndex(Register result,
                                              DoubleRegister value,
                                              Label* success, Label* fail) {
  TemporaryRegisterScope temps(this);
  DoubleRegister temp = temps.AcquireScratchDouble();
  // Convert the input float64 value to int32.
  ConvertDoubleToInt64(value, result, temp);
  SignedExtend<int>(result, result);

  // Convert that int32 value back to float64.
  ConvertIntToDouble(result, temp);
  // Check that the result of the float64->int32->float64 is equal to
  // the input (i.e. that the conversion didn't truncate).
  fcmpu(value, temp);
  JumpIf(ne, fail);
  Jump(success);
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
