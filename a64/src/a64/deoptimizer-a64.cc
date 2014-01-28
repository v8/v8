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


// The back edge bookkeeping code matches the pattern:
//
//  <decrement profiling counter>
//  .. .. .. ..       b.pl ok
//  .. .. .. ..       ldr x16, pc+<interrupt stub address>
//  .. .. .. ..       blr x16
//  ok-label
//
// We patch the code to the following form:
//
//  <decrement profiling counter>
//  .. .. .. ..       mov x0, x0 (NOP)
//  .. .. .. ..       ldr x16, pc+<on-stack replacement address>
//  .. .. .. ..       blr x16
void Deoptimizer::PatchInterruptCodeAt(Code* unoptimized_code,
                                       Address pc_after,
                                       Code* interrupt_code,
                                       Code* replacement_code) {
  ASSERT(!InterruptCodeIsPatched(unoptimized_code,
                                 pc_after,
                                 interrupt_code,
                                 replacement_code));

  // Turn the jump into a nop.
  Instruction* jump = Instruction::Cast(pc_after)->preceding(3);
  PatchingAssembler patcher(jump, 1);
  patcher.nop(Assembler::INTERRUPT_CODE_NOP);

  // Replace the call address.
  Instruction* load = Instruction::Cast(pc_after)->preceding(2);
  Address interrupt_address_pointer =
      reinterpret_cast<Address>(load) + load->ImmPCOffset();
  Memory::uint64_at(interrupt_address_pointer) =
      reinterpret_cast<uint64_t>(replacement_code->entry());

  unoptimized_code->GetHeap()->incremental_marking()->RecordCodeTargetPatch(
      unoptimized_code, pc_after - 2 * kInstructionSize, replacement_code);
}


void Deoptimizer::RevertInterruptCodeAt(Code* unoptimized_code,
                                        Address pc_after,
                                        Code* interrupt_code,
                                        Code* replacement_code) {
  ASSERT(InterruptCodeIsPatched(unoptimized_code,
                                pc_after,
                                interrupt_code,
                                replacement_code));

  // Turn the nop into a jump.
  Instruction* jump = Instruction::Cast(pc_after)->preceding(3);
  PatchingAssembler patcher(jump, 1);
  patcher.b(6, pl);  // The ok label is 6 instructions later.

  // Replace the call address.
  Instruction* load = Instruction::Cast(pc_after)->preceding(2);
  Address interrupt_address_pointer =
      reinterpret_cast<Address>(load) + load->ImmPCOffset();
  Memory::uint64_at(interrupt_address_pointer) =
      reinterpret_cast<uint64_t>(interrupt_code->entry());

  interrupt_code->GetHeap()->incremental_marking()->RecordCodeTargetPatch(
      unoptimized_code, pc_after - 2 * kInstructionSize, interrupt_code);
}


#ifdef DEBUG
bool Deoptimizer::InterruptCodeIsPatched(Code* unoptimized_code,
                                         Address pc_after,
                                         Code* interrupt_code,
                                         Code* replacement_code) {
  Instruction* jump_or_nop = Instruction::Cast(pc_after)->preceding(3);
  return jump_or_nop->IsNop(Assembler::INTERRUPT_CODE_NOP);
}
#endif


static int LookupBailoutId(DeoptimizationInputData* data, BailoutId ast_id) {
  ByteArray* translations = data->TranslationByteArray();
  int length = data->DeoptCount();
  for (int i = 0; i < length; i++) {
    if (data->AstId(i) == ast_id) {
      TranslationIterator it(translations,  data->TranslationIndex(i)->value());
      int value = it.Next();
      ASSERT(Translation::BEGIN == static_cast<Translation::Opcode>(value));
      // Read the number of frames.
      value = it.Next();
      if (value == 1) return i;
    }
  }
  UNREACHABLE();
  return -1;
}


void Deoptimizer::DoComputeOsrOutputFrame() {
  DeoptimizationInputData* data = DeoptimizationInputData::cast(
      compiled_code_->deoptimization_data());
  unsigned ast_id = data->OsrAstId()->value();

  int bailout_id = LookupBailoutId(data, BailoutId(ast_id));
  unsigned translation_index = data->TranslationIndex(bailout_id)->value();
  ByteArray* translations = data->TranslationByteArray();

  TranslationIterator iterator(translations, translation_index);
  Translation::Opcode opcode =
      static_cast<Translation::Opcode>(iterator.Next());
  ASSERT(Translation::BEGIN == opcode);
  USE(opcode);
  int count = iterator.Next();
  iterator.Skip(1);  // Drop JS frame count.
  ASSERT(count == 1);
  USE(count);

  opcode = static_cast<Translation::Opcode>(iterator.Next());
  USE(opcode);
  ASSERT(Translation::JS_FRAME == opcode);
  unsigned node_id = iterator.Next();
  USE(node_id);
  ASSERT(node_id == ast_id);
  int closure_id = iterator.Next();
  USE(closure_id);
  ASSERT_EQ(Translation::kSelfLiteralId, closure_id);
  unsigned height = iterator.Next();
  unsigned height_in_bytes = height * kPointerSize;
  USE(height_in_bytes);

  unsigned fixed_size = ComputeFixedSize(function_);
  unsigned input_frame_size = input_->GetFrameSize();
  ASSERT(fixed_size + height_in_bytes == input_frame_size);

  unsigned stack_slot_size = compiled_code_->stack_slots() * kPointerSize;
  unsigned outgoing_height = data->ArgumentsStackHeight(bailout_id)->value();
  unsigned outgoing_size = outgoing_height * kPointerSize;
  unsigned output_frame_size = fixed_size + stack_slot_size + outgoing_size;
  ASSERT(outgoing_size == 0);  // OSR does not happen in the middle of a call.

  if (FLAG_trace_osr) {
    PrintF("[on-stack replacement: begin 0x%08" V8PRIxPTR " ",
           reinterpret_cast<intptr_t>(function_));
    PrintFunctionName();
    PrintF(" => node=%u, frame=%d->%d]\n",
           ast_id,
           input_frame_size,
           output_frame_size);
  }

  // There's only one output frame in the OSR case.
  output_count_ = 1;
  output_ = new FrameDescription*[1];
  output_[0] = new(output_frame_size) FrameDescription(
      output_frame_size, function_);
  output_[0]->SetFrameType(StackFrame::JAVA_SCRIPT);

  // Clear the incoming parameters in the optimized frame to avoid
  // confusing the garbage collector.
  unsigned output_offset = output_frame_size - kPointerSize;
  int parameter_count = function_->shared()->formal_parameter_count() + 1;
  for (int i = 0; i < parameter_count; ++i) {
    output_[0]->SetFrameSlot(output_offset, 0);
    output_offset -= kPointerSize;
  }

  // Translate the incoming parameters. This may overwrite some of the
  // incoming argument slots we've just cleared.
  int input_offset = input_frame_size - kPointerSize;
  bool ok = true;
  int limit = input_offset - (parameter_count * kPointerSize);
  while (ok && input_offset > limit) {
    ok = DoOsrTranslateCommand(&iterator, &input_offset);
  }

  // There are no translation commands for the caller's pc and fp, the
  // context, and the function.  Set them up explicitly.
  for (int i =  StandardFrameConstants::kCallerPCOffset;
       ok && i >=  StandardFrameConstants::kMarkerOffset;
       i -= kPointerSize) {
    uint32_t input_value = input_->GetFrameSlot(input_offset);
    if (FLAG_trace_osr) {
      const char* name = "UNKNOWN";
      switch (i) {
        case StandardFrameConstants::kCallerPCOffset:
          name = "caller's pc";
          break;
        case StandardFrameConstants::kCallerFPOffset:
          name = "fp";
          break;
        case StandardFrameConstants::kContextOffset:
          name = "context";
          break;
        case StandardFrameConstants::kMarkerOffset:
          name = "function";
          break;
      }
      PrintF("    [sp + %d] <- 0x%08x ; [sp + %d] (fixed part - %s)\n",
             output_offset,
             input_value,
             input_offset,
             name);
    }

    output_[0]->SetFrameSlot(output_offset, input_->GetFrameSlot(input_offset));
    input_offset -= kPointerSize;
    output_offset -= kPointerSize;
  }

  // Translate the rest of the frame.
  while (ok && input_offset >= 0) {
    ok = DoOsrTranslateCommand(&iterator, &input_offset);
  }

  // If translation of any command failed, continue using the input frame.
  if (!ok) {
    delete output_[0];
    output_[0] = input_;
    output_[0]->SetPc(reinterpret_cast<uint64_t>(from_));
  } else {
    // Set up the frame pointer and the context pointer.
    output_[0]->SetRegister(fp.code(), input_->GetRegister(fp.code()));
    output_[0]->SetRegister(cp.code(), input_->GetRegister(cp.code()));

    unsigned pc_offset = data->OsrPcOffset()->value();
    uint64_t pc = reinterpret_cast<uint64_t>(
        compiled_code_->entry() + pc_offset);
    output_[0]->SetPc(pc);
  }
  Code* continuation = isolate_->builtins()->builtin(Builtins::kNotifyOSR);
  output_[0]->SetContinuation(
      reinterpret_cast<uint64_t>(continuation->entry()));

  if (FLAG_trace_osr) {
    PrintF("[on-stack replacement translation %s: 0x%08" V8PRIxPTR " ",
           ok ? "finished" : "aborted",
           reinterpret_cast<intptr_t>(function_));
    PrintFunctionName();
    PrintF(" => pc=0x%0lx]\n", output_[0]->GetPc());
  }
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

  Register code_object = x3;
  Register fp_to_sp = x4;
  // Get the address of the location in the code object. This is the return
  // address for lazy deoptimization.
  __ Mov(code_object, lr);
  // Compute the fp-to-sp delta, and correct one word for bailout id.
  __ Add(fp_to_sp, masm()->StackPointer(),
         kSavedRegistersAreaSize + (1 * kPointerSize));
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

  // Remove the bailout id and the saved registers from the stack.
  __ Drop(1 + (kSavedRegistersAreaSize / kXRegSizeInBytes));

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
const int Deoptimizer::table_entry_size_ = 2 * kInstructionSize;


void Deoptimizer::TableEntryGenerator::GeneratePrologue() {
  // Create a sequence of deoptimization entries.
  // Note that registers are still live when jumping to an entry.
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
