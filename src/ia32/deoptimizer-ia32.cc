// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_IA32

#include "src/assembler-inl.h"
#include "src/deoptimizer.h"
#include "src/frame-constants.h"
#include "src/register-configuration.h"
#include "src/safepoint-table.h"

namespace v8 {
namespace internal {

const int Deoptimizer::table_entry_size_ = 10;

#define __ masm()->

void Deoptimizer::TableEntryGenerator::Generate() {
  Assembler::SupportsRootRegisterScope supports_root_register(masm());

  GeneratePrologue();

  // Save all general purpose registers before messing with them.
  const int kNumberOfRegisters = Register::kNumRegisters;

  const int kDoubleRegsSize = kDoubleSize * XMMRegister::kNumRegisters;
  __ sub(esp, Immediate(kDoubleRegsSize));
  const RegisterConfiguration* config = RegisterConfiguration::Default();
  for (int i = 0; i < config->num_allocatable_double_registers(); ++i) {
    int code = config->GetAllocatableDoubleCode(i);
    XMMRegister xmm_reg = XMMRegister::from_code(code);
    int offset = code * kDoubleSize;
    __ movsd(Operand(esp, offset), xmm_reg);
  }

  STATIC_ASSERT(kFloatSize == kPointerSize);
  const int kFloatRegsSize = kFloatSize * XMMRegister::kNumRegisters;
  __ sub(esp, Immediate(kFloatRegsSize));
  for (int i = 0; i < config->num_allocatable_float_registers(); ++i) {
    int code = config->GetAllocatableFloatCode(i);
    XMMRegister xmm_reg = XMMRegister::from_code(code);
    int offset = code * kFloatSize;
    __ movss(Operand(esp, offset), xmm_reg);
  }

  __ pushad();

  static constexpr Register scratch0 = esi;
  static constexpr Register scratch1 = ecx;
  static constexpr Register scratch2 = edx;
  static constexpr Register scratch3 = eax;
  static constexpr Register scratch4 = edi;

  ExternalReference c_entry_fp_address =
      ExternalReference::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  __ mov(masm()->StaticVariable(c_entry_fp_address), ebp);

  const int kSavedRegistersAreaSize =
      kNumberOfRegisters * kPointerSize + kDoubleRegsSize + kFloatRegsSize;

  // Get the bailout id from the stack.
  __ mov(scratch0, Operand(esp, kSavedRegistersAreaSize));

  // Get the address of the location in the code object
  // and compute the fp-to-sp delta in register scratch2.
  __ mov(scratch1, Operand(esp, kSavedRegistersAreaSize + 1 * kPointerSize));
  __ lea(scratch2, Operand(esp, kSavedRegistersAreaSize + 2 * kPointerSize));

  __ sub(scratch2, ebp);
  __ neg(scratch2);

  // Allocate a new deoptimizer object.
  __ PrepareCallCFunction(6, scratch3);
  __ mov(scratch3, Immediate(0));
  Label context_check;
  __ mov(scratch4,
         Operand(ebp, CommonFrameConstants::kContextOrFrameTypeOffset));
  __ JumpIfSmi(scratch4, &context_check);
  __ mov(scratch3, Operand(ebp, JavaScriptFrameConstants::kFunctionOffset));
  __ bind(&context_check);
  __ mov(Operand(esp, 0 * kPointerSize), scratch3);  // Function.
  __ mov(Operand(esp, 1 * kPointerSize),
         Immediate(static_cast<int>(deopt_kind())));
  __ mov(Operand(esp, 2 * kPointerSize), scratch0);  // Bailout id.
  __ mov(Operand(esp, 3 * kPointerSize), scratch1);  // Code address or 0.
  __ mov(Operand(esp, 4 * kPointerSize), scratch2);  // Fp-to-sp delta.
  __ mov(Operand(esp, 5 * kPointerSize),
         Immediate(ExternalReference::isolate_address(isolate())));
  {
    AllowExternalCallThatCantCauseGC scope(masm());
    __ CallCFunction(ExternalReference::new_deoptimizer_function(), 6);
  }

  // Preserve deoptimizer object in register scratch3 and get the input
  // frame descriptor pointer.
  __ mov(scratch0, Operand(scratch3, Deoptimizer::input_offset()));

  // Fill in the input registers.
  for (int i = kNumberOfRegisters - 1; i >= 0; i--) {
    int offset = (i * kPointerSize) + FrameDescription::registers_offset();
    __ pop(Operand(scratch0, offset));
  }

  int float_regs_offset = FrameDescription::float_registers_offset();
  // Fill in the float input registers.
  for (int i = 0; i < XMMRegister::kNumRegisters; i++) {
    int dst_offset = i * kFloatSize + float_regs_offset;
    __ pop(Operand(scratch0, dst_offset));
  }

  int double_regs_offset = FrameDescription::double_registers_offset();
  // Fill in the double input registers.
  for (int i = 0; i < config->num_allocatable_double_registers(); ++i) {
    int code = config->GetAllocatableDoubleCode(i);
    int dst_offset = code * kDoubleSize + double_regs_offset;
    int src_offset = code * kDoubleSize;
    __ movsd(xmm0, Operand(esp, src_offset));
    __ movsd(Operand(scratch0, dst_offset), xmm0);
  }

  // Clear FPU all exceptions.
  // TODO(ulan): Find out why the TOP register is not zero here in some cases,
  // and check that the generated code never deoptimizes with unbalanced stack.
  __ fnclex();

  // Remove the bailout id, return address and the double registers.
  __ add(esp, Immediate(kDoubleRegsSize + 2 * kPointerSize));

  // Compute a pointer to the unwinding limit in register scratch1; that is
  // the first stack slot not part of the input frame.
  __ mov(scratch1, Operand(scratch0, FrameDescription::frame_size_offset()));
  __ add(scratch1, esp);

  // Unwind the stack down to - but not including - the unwinding
  // limit and copy the contents of the activation frame to the input
  // frame description.
  __ lea(scratch2, Operand(scratch0, FrameDescription::frame_content_offset()));
  Label pop_loop_header;
  __ jmp(&pop_loop_header);
  Label pop_loop;
  __ bind(&pop_loop);
  __ pop(Operand(scratch2, 0));
  __ add(scratch2, Immediate(sizeof(uint32_t)));
  __ bind(&pop_loop_header);
  __ cmp(scratch1, esp);
  __ j(not_equal, &pop_loop);

  // Compute the output frame in the deoptimizer.
  __ push(scratch3);
  __ PrepareCallCFunction(1, scratch0);
  __ mov(Operand(esp, 0 * kPointerSize), scratch3);
  {
    AllowExternalCallThatCantCauseGC scope(masm());
    __ CallCFunction(ExternalReference::compute_output_frames_function(), 1);
  }
  __ pop(scratch3);

  __ mov(esp, Operand(scratch3, Deoptimizer::caller_frame_top_offset()));

  // Replace the current (input) frame with the output frames.
  Label outer_push_loop, inner_push_loop,
      outer_loop_header, inner_loop_header;
  // Outer loop state: scratch3 = current FrameDescription**, scratch2 = one
  // past the last FrameDescription**.
  __ mov(scratch2, Operand(scratch3, Deoptimizer::output_count_offset()));
  __ mov(scratch3, Operand(eax, Deoptimizer::output_offset()));
  __ lea(scratch2, Operand(scratch3, edx, times_4, 0));
  __ jmp(&outer_loop_header);
  __ bind(&outer_push_loop);
  // Inner loop state: scratch0 = current FrameDescription*, scratch1 = loop
  // index.
  __ mov(scratch0, Operand(scratch3, 0));
  __ mov(scratch1, Operand(scratch0, FrameDescription::frame_size_offset()));
  __ jmp(&inner_loop_header);
  __ bind(&inner_push_loop);
  __ sub(scratch1, Immediate(sizeof(uint32_t)));
  __ push(Operand(scratch0, scratch1, times_1,
                  FrameDescription::frame_content_offset()));
  __ bind(&inner_loop_header);
  __ test(scratch1, scratch1);
  __ j(not_zero, &inner_push_loop);
  __ add(scratch3, Immediate(kPointerSize));
  __ bind(&outer_loop_header);
  __ cmp(scratch3, scratch2);
  __ j(below, &outer_push_loop);

  // In case of a failed STUB, we have to restore the XMM registers.
  for (int i = 0; i < config->num_allocatable_double_registers(); ++i) {
    int code = config->GetAllocatableDoubleCode(i);
    XMMRegister xmm_reg = XMMRegister::from_code(code);
    int src_offset = code * kDoubleSize + double_regs_offset;
    __ movsd(xmm_reg, Operand(scratch0, src_offset));
  }

  // Push pc and continuation from the last output frame.
  __ push(Operand(scratch0, FrameDescription::pc_offset()));
  __ push(Operand(scratch0, FrameDescription::continuation_offset()));

  // Push the registers from the last output frame.
  for (int i = 0; i < kNumberOfRegisters; i++) {
    int offset = (i * kPointerSize) + FrameDescription::registers_offset();
    __ push(Operand(scratch0, offset));
  }

  // Restore the registers from the stack.
  Assembler::AllowExplicitEbxAccessScope restoring_spilled_value(masm());
  __ popad();

  // Return to the continuation point.
  __ ret(0);
}


void Deoptimizer::TableEntryGenerator::GeneratePrologue() {
  // Create a sequence of deoptimization entries.
  Label done;
  for (int i = 0; i < count(); i++) {
    int start = masm()->pc_offset();
    USE(start);
    __ push_imm32(i);
    __ jmp(&done);
    DCHECK(masm()->pc_offset() - start == table_entry_size_);
  }
  __ bind(&done);
}

bool Deoptimizer::PadTopOfStackRegister() { return false; }

void FrameDescription::SetCallerPc(unsigned offset, intptr_t value) {
  SetFrameSlot(offset, value);
}


void FrameDescription::SetCallerFp(unsigned offset, intptr_t value) {
  SetFrameSlot(offset, value);
}


void FrameDescription::SetCallerConstantPool(unsigned offset, intptr_t value) {
  // No embedded constant pool support.
  UNREACHABLE();
}


#undef __


}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_IA32
