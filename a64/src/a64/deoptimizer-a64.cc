// Copyright 2013 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "codegen.h"
#include "deoptimizer.h"
#include "full-codegen.h"
#include "safepoint-table.h"


namespace v8 {
namespace internal {


int Deoptimizer::patch_size() {
  // Size of the code used to patch lazy bailout points.
  // Patching is done by Deoptimizer::DeoptimizeFunction.
  return 4 * kInstructionSize;
}


void Deoptimizer::DeoptimizeFunctionWithPreparedFunctionList(
    JSFunction* function) {
  Isolate* isolate = function->GetIsolate();
  HandleScope scope(isolate);
  DisallowHeapAllocation no_allocation;

  ASSERT(function->IsOptimized());
  ASSERT(function->FunctionsInFunctionListShareSameCode());

  // Get the optimized code.
  Code* code = function->code();

  // The optimized code is going to be patched, so we cannot use it any more.
  function->shared()->EvictFromOptimizedCodeMap(code, "deoptimized function");

  // Invalidate the relocation information, as it will become invalid by the
  // code patching below, and is not needed any more.
  code->InvalidateRelocation();

  // For each LLazyBailout instruction insert a call to the corresponding
  // deoptimization entry.
  DeoptimizationInputData* deopt_data =
      DeoptimizationInputData::cast(code->deoptimization_data());
  Address code_start_address = code->instruction_start();
#ifdef DEBUG
  Address prev_call_address = NULL;
#endif

  for (int i = 0; i < deopt_data->DeoptCount(); i++) {
    if (deopt_data->Pc(i)->value() == -1) continue;

    Address call_address = code_start_address + deopt_data->Pc(i)->value();
    Address deopt_entry = GetDeoptimizationEntry(isolate, i, LAZY);

    PatchingAssembler patcher(call_address, patch_size() / kInstructionSize);
    patcher.LoadLiteral(ip0, 2 * kInstructionSize);
    patcher.blr(ip0);
    patcher.dc64(reinterpret_cast<intptr_t>(deopt_entry));

    ASSERT((prev_call_address == NULL) ||
           (call_address >= prev_call_address + patch_size()));
    ASSERT(call_address + patch_size() <= code->instruction_end());
#ifdef DEBUG
    prev_call_address = call_address;
#endif
  }

  // Add the deoptimizing code to the list.
  DeoptimizingCodeListNode* node = new DeoptimizingCodeListNode(code);
  DeoptimizerData* data = isolate->deoptimizer_data();
  node->set_next(data->deoptimizing_code_list_);
  data->deoptimizing_code_list_ = node;

  // We might be in the middle of incremental marking with compaction.
  // Tell collector to treat this code object in a special way and
  // ignore all slots that might have been recorded on it.
  isolate->heap()->mark_compact_collector()->InvalidateCode(code);

  ReplaceCodeForRelatedFunctions(function, code);

  if (FLAG_trace_deopt) {
    PrintF("[forced deoptimization: ");
    function->PrintName();
    PrintF(" / %" V8PRIxPTR "]\n", reinterpret_cast<intptr_t>(function));
  }
}


void Deoptimizer::PatchInterruptCodeAt(Code* unoptimized_code,
                                       Address pc_after,
                                       Code* interrupt_code,
                                       Code* replacement_code) {
  UNIMPLEMENTED();
  ASSERT(!InterruptCodeIsPatched(unoptimized_code,
                                 pc_after,
                                 interrupt_code,
                                 replacement_code));
}


void Deoptimizer::RevertInterruptCodeAt(Code* unoptimized_code,
                                        Address pc_after,
                                        Code* interrupt_code,
                                        Code* replacement_code) {
  UNIMPLEMENTED();
  ASSERT(InterruptCodeIsPatched(unoptimized_code,
                                pc_after,
                                interrupt_code,
                                replacement_code));
}


#ifdef DEBUG
bool Deoptimizer::InterruptCodeIsPatched(Code* unoptimized_code,
                                         Address pc_after,
                                         Code* interrupt_code,
                                         Code* replacement_code) {
  UNIMPLEMENTED();
  return false;
}
#endif


void Deoptimizer::DoComputeOsrOutputFrame() {
  UNIMPLEMENTED();
}


void Deoptimizer::FillInputFrame(Address tos, JavaScriptFrame* frame) {
  // Set the register values. The values are not important as there are no
  // callee saved registers in JavaScript frames, so all registers are
  // spilled. Registers fp and sp are set to the correct values though.
  for (int i = 0; i < Register::NumRegisters(); i++) {
    input_->SetRegister(i, 0);
  }

  // TODO(all): Do we also need to set a value to csp?
  input_->SetRegister(jssp.code(), reinterpret_cast<intptr_t>(frame->sp()));
  input_->SetRegister(fp.code(), reinterpret_cast<intptr_t>(frame->fp()));

  for (int i = 0; i < DoubleRegister::NumAllocatableRegisters(); i++) {
    input_->SetDoubleRegister(i, 0.0);
  }

  // Fill the frame content from the actual data on the frame.
  for (unsigned i = 0; i < input_->GetFrameSize(); i += kPointerSize) {
    input_->SetFrameSlot(i, Memory::uint64_at(tos + i));
  }
}


bool Deoptimizer::HasAlignmentPadding(JSFunction* function) {
  // There is no dynamic alignment padding on A64 in the input frame.
  return false;
}


void Deoptimizer::SetPlatformCompiledStubRegisters(
    FrameDescription* output_frame, CodeStubInterfaceDescriptor* descriptor) {
  ApiFunction function(descriptor->deoptimization_handler_);
  ExternalReference xref(&function, ExternalReference::BUILTIN_CALL, isolate_);
  intptr_t handler = reinterpret_cast<intptr_t>(xref.address());
  int params = descriptor->register_param_count_;
  if (descriptor->stack_parameter_count_ != NULL) {
    params++;
  }
  output_frame->SetRegister(x0.code(), params);
  output_frame->SetRegister(x1.code(), handler);
}


void Deoptimizer::CopyDoubleRegisters(FrameDescription* output_frame) {
  for (int i = 0; i < DoubleRegister::kMaxNumRegisters; ++i) {
    double double_value = input_->GetDoubleRegister(i);
    output_frame->SetDoubleRegister(i, double_value);
  }
}


#define __ masm()->

void Deoptimizer::EntryGenerator::Generate() {
  GeneratePrologue();

  // TODO(all): This code needs to be revisited. We probably only need to save
  // caller-saved registers here. Callee-saved registers can be stored directly
  // in the input frame.

  // Save all allocatable floating point registers.
  CPURegList saved_fp_registers(CPURegister::kFPRegister, kDRegSize,
                                0, FPRegister::NumAllocatableRegisters());
  __ PushCPURegList(saved_fp_registers);

  // We save all the registers expcept jssp, sp and lr.
  CPURegList saved_registers(CPURegister::kRegister, kXRegSize, 0, 27);
  saved_registers.Combine(fp);
  __ PushCPURegList(saved_registers);

  const int kSavedRegistersAreaSize =
      (saved_registers.Count() * kXRegSizeInBytes) +
      (saved_fp_registers.Count() * kDRegSizeInBytes);

  // Floating point registers are saved on the stack above core registers.
  const int kFPRegistersOffset = saved_registers.Count() * kXRegSizeInBytes;

  // Get the bailout id from the stack.
  Register bailout_id = x2;
  __ Peek(bailout_id, kSavedRegistersAreaSize);

  // Get the address of the location in the code object if possible
  // and compute the fp-to-sp delta.
  Register code_object = x3;
  Register fp_to_sp = x4;
  if ((type() == EAGER) || (type() == SOFT)) {
    __ Mov(code_object, 0);
    // Correct one word for bailout id.
    __ Add(fp_to_sp,
           masm()->StackPointer(),
           kSavedRegistersAreaSize + (1 * kPointerSize));
  } else if (type() == OSR) {
    UNIMPLEMENTED();
  } else {
    __ Mov(code_object, lr);
    // Correct two words for bailout id and return address.
    __ Add(fp_to_sp,
           masm()->StackPointer(),
           kSavedRegistersAreaSize + (2 * kPointerSize));
  }
  __ Sub(fp_to_sp, fp, fp_to_sp);

  // Allocate a new deoptimizer object.
  __ Ldr(x0, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  __ Mov(x1, type());
  // Following arguments are already loaded:
  //  - x2: bailout id
  //  - x3: code object address
  //  - x4: fp-to-sp delta
  __ Mov(x5, Operand(ExternalReference::isolate_address(isolate())));

  {
    // Call Deoptimizer::New().
    AllowExternalCallThatCantCauseGC scope(masm());
    __ CallCFunction(ExternalReference::new_deoptimizer_function(isolate()), 6);
  }

  // Preserve "deoptimizer" object in register x0.
  Register deoptimizer = x0;

  // Get the input frame descriptor pointer.
  __ Ldr(x1, MemOperand(deoptimizer, Deoptimizer::input_offset()));

  // Copy core registers into the input frame.
  CPURegList copy_to_input = saved_registers;
  for (int i = 0; i < saved_registers.Count(); i++) {
    // TODO(all): Look for opportunities to optimize this by using ldp/stp.
    __ Peek(x2, i * kPointerSize);
    CPURegister current_reg = copy_to_input.PopLowestIndex();
    int offset = (current_reg.code() * kPointerSize) +
        FrameDescription::registers_offset();
    __ Str(x2, MemOperand(x1, offset));
  }

  // Copy FP registers to the input frame.
  for (int i = 0; i < saved_fp_registers.Count(); i++) {
    // TODO(all): Look for opportunities to optimize this by using ldp/stp.
    int dst_offset = FrameDescription::double_registers_offset() +
        (i * kDoubleSize);
    int src_offset = kFPRegistersOffset + (i * kDoubleSize);
    __ Peek(x2, src_offset);
    __ Str(x2, MemOperand(x1, dst_offset));
  }

  // Remove the bailout id, eventually return address, and the saved registers
  // from the stack.
  if ((type() == EAGER) || (type() == SOFT) || (type() == OSR)) {
    __ Drop(1 + (kSavedRegistersAreaSize / kXRegSizeInBytes));
  } else {
    // Also remove return address for lazy deopt.
    __ Drop(2 + (kSavedRegistersAreaSize / kXRegSizeInBytes));
  }

  // Compute a pointer to the unwinding limit in register x2; that is
  // the first stack slot not part of the input frame.
  Register unwind_limit = x2;
  __ Ldr(unwind_limit, MemOperand(x1, FrameDescription::frame_size_offset()));
  __ Add(unwind_limit, unwind_limit, __ StackPointer());

  // Unwind the stack down to - but not including - the unwinding
  // limit and copy the contents of the activation frame to the input
  // frame description.
  __ Add(x3, x1, FrameDescription::frame_content_offset());
  Label pop_loop;
  Label pop_loop_header;
  __ B(&pop_loop_header);
  __ Bind(&pop_loop);
  __ Pop(x4);
  __ Str(x4, MemOperand(x3, kPointerSize, PostIndex));
  __ Bind(&pop_loop_header);
  __ Cmp(unwind_limit, __ StackPointer());
  __ B(ne, &pop_loop);

  // Compute the output frame in the deoptimizer.
  __ Push(x0);  // Preserve deoptimizer object across call.

  {
    // Call Deoptimizer::ComputeOutputFrames().
    AllowExternalCallThatCantCauseGC scope(masm());
    __ CallCFunction(
        ExternalReference::compute_output_frames_function(isolate()), 1);
  }
  __ Pop(x0);  // Restore deoptimizer object (class Deoptimizer).

  // Replace the current (input) frame with the output frames.
  Label outer_push_loop, inner_push_loop,
      outer_loop_header, inner_loop_header;
  __ Ldrsw(x1, MemOperand(x0, Deoptimizer::output_count_offset()));
  __ Ldr(x0, MemOperand(x0, Deoptimizer::output_offset()));
  __ Add(x1, x0, Operand(x1, LSL, kPointerSizeLog2));
  __ B(&outer_loop_header);

  __ Bind(&outer_push_loop);
  Register current_frame = x2;
  __ Ldr(current_frame, MemOperand(x0, 0));
  __ Ldr(x3, MemOperand(current_frame, FrameDescription::frame_size_offset()));
  __ B(&inner_loop_header);

  __ Bind(&inner_push_loop);
  __ Sub(x3, x3, kPointerSize);
  __ Add(x6, current_frame, x3);
  __ Ldr(x7, MemOperand(x6, FrameDescription::frame_content_offset()));
  __ Push(x7);
  __ Bind(&inner_loop_header);
  __ Cbnz(x3, &inner_push_loop);

  __ Add(x0, x0, kPointerSize);
  __ Bind(&outer_loop_header);
  __ Cmp(x0, x1);
  __ B(lt, &outer_push_loop);

  // TODO(jbramley): The ARM code restores FP registers here.
  TODO_UNIMPLEMENTED("EntryGenerator::Generate: Restore FP registers.");

  // Push state, pc, and continuation from the last output frame.
  if (type() != OSR) {
    __ Ldr(x6, MemOperand(current_frame, FrameDescription::state_offset()));
    __ Push(x6);
  }

  // TODO(all): This code needs to be revisited, We probably don't need to
  // restore all the registers as fullcodegen does not keep live values in
  // registers (note that at least fp must be restored though).

  // Restore registers from the last output frame.
  // Note that lr is not in the list of saved_registers and will be restored
  // later. We can use it to hold the address of last output frame while
  // reloading the other registers.
  ASSERT(!saved_registers.IncludesAliasOf(lr));
  Register last_output_frame = lr;
  __ Mov(last_output_frame, current_frame);

  // We don't need to restore x7 as it will be clobbered later to hold the
  // continuation address.
  Register continuation = x7;
  saved_registers.Remove(continuation);

  while (!saved_registers.IsEmpty()) {
    // TODO(all): Look for opportunities to optimize this by using ldp.
    CPURegister current_reg = saved_registers.PopLowestIndex();
    int offset = (current_reg.code() * kPointerSize) +
        FrameDescription::registers_offset();
    __ Ldr(current_reg, MemOperand(last_output_frame, offset));
  }

  __ Ldr(continuation, MemOperand(last_output_frame,
                                  FrameDescription::continuation_offset()));
  __ Ldr(lr, MemOperand(last_output_frame, FrameDescription::pc_offset()));
  __ InitializeRootRegister();
  __ Br(continuation);
}


// Size of an entry of the second level deopt table.
// This is the code size generated by GeneratePrologue for one entry.
const int Deoptimizer::table_entry_size_ = 4 * kInstructionSize;


void Deoptimizer::TableEntryGenerator::GeneratePrologue() {
  // Create a sequence of deoptimization entries.
  // Note that any registers may be still live.
  Label done;
  {
    InstructionAccurateScope scope(masm());

    // The number of entry will never exceed kMaxNumberOfEntries.
    // As long as kMaxNumberOfEntries is a valid 16 bits immediate you can use
    // a movz instruction to load the entry id.
    ASSERT(is_uint16(Deoptimizer::kMaxNumberOfEntries));

    for (int i = 0; i < count(); i++) {
      int start = masm()->pc_offset();
      USE(start);
      if ((type() == EAGER) || (type() == SOFT)) {
        // These nops are fillers; all entries must be the same size.
        __ nop();
        __ nop();
      } else {
        // Push lr on the stack.
        // We cannot use Push from the MacroAssembler here since we are in an
        // instruction accurate scope.
        __ sub(csp, jssp, kPointerSize);
        __ str(lr, MemOperand(jssp, -kPointerSize, PreIndex));
      }

      __ movz(masm()->Tmp0(), i);
      __ b(&done);
      ASSERT(masm()->pc_offset() - start == table_entry_size_);
    }
  }
  __ Bind(&done);
  // TODO(all): We need to add some kind of assertion to verify that Tmp0()
  // is not clobbered by Push.
  __ Push(masm()->Tmp0());
}

#undef __

} }  // namespace v8::internal
