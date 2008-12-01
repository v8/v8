// Copyright 2008 the V8 project authors. All rights reserved.
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

#include <string.h>
#include "v8.h"
#include "log.h"
#include "ast.h"
#include "macro-assembler.h"
#include "regexp-macro-assembler.h"
#include "macro-assembler-ia32.h"
#include "regexp-macro-assembler-ia32.h"

namespace v8 { namespace internal {

/*
 * This assembler uses the following register assignment convention
 * - edx : current character. Must be loaded using LoadCurrentCharacter
 *         before using any of the dispatch methods.
 * - edi : current position in input, as negative offset from end of string.
 *         Please notice that this is the byte offset, not the character offset!
 * - esi : end of input (points to byte after last character in input).
 * - ebp : points to the location above the registers on the stack,
 *         as if by the "enter <register_count>" opcode.
 * - esp : points to tip of backtracking stack.
 *
 * The registers eax, ebx and ecx are free to use for computations.
 *
 * Each call to a public method should retain this convention.
 * The stack will have the following structure:
 *       - at_start           (if 1, start at start of string, if 0, don't)
 *       - int* capture_array (int[num_saved_registers_], for output).
 *       - end of input       (index of end of string, relative to *string_base)
 *       - start of input     (index of first character in string, relative
 *                            to *string_base)
 *       - void** string_base (location of a handle containing the string)
 *       - return address
 *       - backup of esi
 *       - backup of edi
 *       - backup of ebx
 * ebp-> - old ebp
 *       - register 0  ebp[-4]  (Only positions must be stored in the first
 *       - register 1  ebp[-8]   num_saved_registers_ registers)
 *       - ...
 *
 * The first num_saved_registers_ registers are initialized to point to
 * "character -1" in the string (i.e., char_size() bytes before the first
 * character of the string). The remaining registers starts out as garbage.
 *
 * The data up to the return address must be placed there by the calling
 * code, e.g., by calling the code as cast to:
 * bool (*match)(String** string_base,
 *               int start_offset,
 *               int end_offset,
 *               int* capture_output_array,
 *               bool at_start)
 */

#define __ masm_->

RegExpMacroAssemblerIA32::RegExpMacroAssemblerIA32(
    Mode mode,
    int registers_to_save)
    : masm_(new MacroAssembler(NULL, kRegExpCodeSize)),
      constants_(kRegExpConstantsSize),
      mode_(mode),
      num_registers_(registers_to_save),
      num_saved_registers_(registers_to_save),
      entry_label_(),
      start_label_(),
      success_label_(),
      exit_label_(),
      self_(Heap::undefined_value()) {
  __ jmp(&entry_label_);   // We'll write the entry code later.
  __ bind(&start_label_);  // And then continue from here.
}


RegExpMacroAssemblerIA32::~RegExpMacroAssemblerIA32() {
  delete masm_;
  // Unuse labels in case we throw away the assembler without calling GetCode.
  entry_label_.Unuse();
  start_label_.Unuse();
  success_label_.Unuse();
  exit_label_.Unuse();
}


void RegExpMacroAssemblerIA32::AdvanceCurrentPosition(int by) {
  ASSERT(by > 0);
  Label inside_string;
  __ add(Operand(edi), Immediate(by * char_size()));
}


void RegExpMacroAssemblerIA32::AdvanceRegister(int reg, int by) {
  ASSERT(reg >= 0);
  ASSERT(reg < num_registers_);
  __ add(register_location(reg), Immediate(by));
}


void RegExpMacroAssemblerIA32::Backtrack() {
  __ pop(ecx);
  __ add(Operand(ecx), Immediate(self_));
  __ jmp(Operand(ecx));
}


void RegExpMacroAssemblerIA32::Bind(Label* label) {
  __ bind(label);
}

void RegExpMacroAssemblerIA32::CheckBitmap(uc16 start,
                                           Label* bitmap,
                                           Label* on_zero) {
  UNREACHABLE();
  __ mov(eax, current_character());
  __ sub(Operand(eax), Immediate(start));
  __ cmp(eax, 64);  // FIXME: 64 = length_of_bitmap_in_bits.
  BranchOrBacktrack(greater_equal, on_zero);
  __ mov(ebx, eax);
  __ shr(ebx, 3);
  // TODO(lrn): Where is the bitmap stored? Pass the bitmap as argument instead.
  // __ mov(ecx, position_of_bitmap);
  __ movzx_b(ebx, Operand(ecx, ebx, times_1, 0));
  __ and_(eax, (1<<3)-1);
  __ bt(Operand(ebx), eax);
  __ j(carry, on_zero);
}


void RegExpMacroAssemblerIA32::CheckCharacter(uc16 c, Label* on_equal) {
  __ cmp(current_character(), c);
  BranchOrBacktrack(equal, on_equal);
}


void RegExpMacroAssemblerIA32::CheckCharacterGT(uc16 limit, Label* on_greater) {
  __ cmp(current_character(), limit);
  BranchOrBacktrack(greater, on_greater);
}


void RegExpMacroAssemblerIA32::CheckNotAtStart(Label* on_not_at_start) {
  __ cmp(Operand(ebp, kAtStart), Immediate(0));
  BranchOrBacktrack(equal, on_not_at_start);
  __ mov(eax, Operand(ebp, kInputEndOffset));
  __ add(eax, Operand(edi));
  __ cmp(eax, Operand(ebp, kInputStartOffset));
  BranchOrBacktrack(not_equal, on_not_at_start);
}


void RegExpMacroAssemblerIA32::CheckCharacterLT(uc16 limit, Label* on_less) {
  __ cmp(current_character(), limit);
  BranchOrBacktrack(less, on_less);
}


void RegExpMacroAssemblerIA32::CheckCharacters(Vector<const uc16> str,
                                               int cp_offset,
                                               Label* on_failure) {
  int byte_length = str.length() * char_size();
  int byte_offset = cp_offset * char_size();
  __ cmp(Operand(edi), Immediate(-(byte_offset + byte_length)));
  BranchOrBacktrack(greater, on_failure);

  if (str.length() <= kMaxInlineStringTests) {
    for (int i = 0; i < str.length(); i++) {
      if (mode_ == ASCII) {
        __ cmpb(Operand(esi, edi, times_1, byte_offset + i),
                static_cast<int8_t>(str[i]));
      } else {
        ASSERT(mode_ == UC16);
        __ cmpw(Operand(esi, edi, times_1, byte_offset + i * sizeof(uc16)),
                Immediate(str[i]));
      }
      __ j(not_equal, on_failure);
    }
    return;
  }

  ArraySlice constant_buffer = constants_.GetBuffer(str.length(), char_size());
  if (mode_ == ASCII) {
    for (int i = 0; i < str.length(); i++) {
      constant_buffer.at<char>(i) = static_cast<char>(str[i]);
    }
  } else {
    memcpy(constant_buffer.location(),
           str.start(),
           str.length() * sizeof(uc16));
  }

  __ mov(eax, edi);
  __ mov(ebx, esi);
  __ lea(edi, Operand(esi, edi, times_1, byte_offset));
  LoadConstantBufferAddress(esi, &constant_buffer);
  __ mov(ecx, str.length());
  if (char_size() == 1) {
    __ rep_cmpsb();
  } else {
    ASSERT(char_size() == 2);
    __ rep_cmpsw();
  }
  __ mov(esi, ebx);
  __ mov(edi, eax);
  BranchOrBacktrack(not_equal, on_failure);
}


void RegExpMacroAssemblerIA32::CheckCurrentPosition(int register_index,
                                                    Label* on_equal) {
  __ cmp(edi, register_location(register_index));
  BranchOrBacktrack(equal, on_equal);
}


void RegExpMacroAssemblerIA32::CheckNotBackReferenceIgnoreCase(
    int start_reg, Label* on_no_match) {
  Label fallthrough;
  __ mov(eax, register_location(start_reg));
  __ mov(ecx, register_location(start_reg + 1));
  __ sub(ecx, Operand(eax));  // Length to check.
  __ j(less, on_no_match);
  __ j(equal, &fallthrough);

  UNIMPLEMENTED();  // TODO(lrn): Call runtime function to do test.

  __ bind(&fallthrough);
}


void RegExpMacroAssemblerIA32::CheckNotBackReference(
    int start_reg, Label* on_no_match) {
  Label fallthrough;
  __ mov(eax, register_location(start_reg));
  __ mov(ecx, register_location(start_reg + 1));
  __ sub(ecx, Operand(eax));  // Length to check.
  __ j(less, on_no_match);
  __ j(equal, &fallthrough);
  // check that there are sufficient characters left in the input
  __ mov(ebx, edi);
  __ add(ebx, Operand(ecx));
  __ j(greater, on_no_match);
  __ mov(ebx, Operand(edi));
  __ push(esi);
  __ add(edi, Operand(esi));
  __ add(esi, Operand(eax));
  __ rep_cmpsb();
  __ pop(esi);
  __ mov(edi, Operand(ebx));
  BranchOrBacktrack(not_equal, on_no_match);
  __ bind(&fallthrough);
}


void RegExpMacroAssemblerIA32::CheckNotRegistersEqual(int reg1,
                                                      int reg2,
                                                      Label* on_not_equal) {
  __ mov(eax, register_location(reg1));
  __ cmp(eax, register_location(reg2));
  BranchOrBacktrack(not_equal, on_not_equal);
}


void RegExpMacroAssemblerIA32::CheckNotCharacter(uc16 c, Label* on_not_equal) {
  __ cmp(current_character(), c);
  BranchOrBacktrack(not_equal, on_not_equal);
}


void RegExpMacroAssemblerIA32::CheckNotCharacterAfterOr(uc16 c,
                                                        uc16 mask,
                                                        Label* on_not_equal) {
  __ mov(eax, current_character());
  __ or_(eax, mask);
  __ cmp(eax, c);
  BranchOrBacktrack(not_equal, on_not_equal);
}


void RegExpMacroAssemblerIA32::CheckNotCharacterAfterMinusOr(
    uc16 c,
    uc16 mask,
    Label* on_not_equal) {
  __ lea(eax, Operand(current_character(), -mask));
  __ or_(eax, mask);
  __ cmp(eax, c);
  BranchOrBacktrack(not_equal, on_not_equal);
}


void RegExpMacroAssemblerIA32::DispatchHalfNibbleMap(
    uc16 start,
    Label* half_nibble_map,
    const Vector<Label*>& destinations) {
  UNIMPLEMENTED();
  __ mov(eax, current_character());
  __ sub(Operand(eax), Immediate(start));

  __ mov(ecx, eax);
  __ shr(eax, 2);
  // FIXME: ecx must hold address of map
  __ movzx_b(eax, Operand(ecx, eax, times_1, 0));
  __ and_(ecx, 0x03);
  __ add(ecx, Operand(ecx));
  __ shr(eax);  // Shift right cl times

  Label second_bit_set, case_3, case_1;
  __ test(eax, Immediate(0x02));
  __ j(not_zero, &second_bit_set);
  __ test(eax, Immediate(0x01));
  __ j(not_zero, &case_1);
  // Case 0:
  __ jmp(destinations[0]);
  __ bind(&case_1);
  // Case 1:
  __ jmp(destinations[1]);
  __ bind(&second_bit_set);
  __ test(eax, Immediate(0x01));
  __ j(not_zero, &case_3);
  // Case 2
  __ jmp(destinations[2]);
  __ bind(&case_3);
  // Case 3:
  __ jmp(destinations[3]);
}


void RegExpMacroAssemblerIA32::DispatchByteMap(
    uc16 start,
    Label* byte_map,
    const Vector<Label*>& destinations) {
  UNIMPLEMENTED();

  Label fallthrough;
  __ mov(eax, current_character());
  __ sub(Operand(eax), Immediate(start));
  __ cmp(eax, 64);  // FIXME: 64 = size of map. Found somehow??
  __ j(greater_equal, &fallthrough);
  // TODO(lrn): ecx must hold address of map
  __ movzx_b(eax, Operand(ecx, eax, times_1, 0));
  // jump table: jump to destinations[eax];

  __ bind(&fallthrough);
}


void RegExpMacroAssemblerIA32::DispatchHighByteMap(
    byte start,
    Label* byte_map,
    const Vector<Label*>& destinations) {
  UNIMPLEMENTED();

  Label fallthrough;
  __ mov(eax, current_character());
  __ shr(eax, 8);
  __ sub(Operand(eax), Immediate(start));
  __ cmp(eax, destinations.length() - start);
  __ j(greater_equal, &fallthrough);

  // TODO(lrn) jumptable: jump to destinations[eax]
  __ bind(&fallthrough);
}


void RegExpMacroAssemblerIA32::EmitOrLink(Label* label) {
  UNIMPLEMENTED();  // Has no use.
}


void RegExpMacroAssemblerIA32::Fail() {
  __ xor_(eax, Operand(eax));  // zero eax.
  __ jmp(&exit_label_);
}


Handle<Object> RegExpMacroAssemblerIA32::GetCode() {
  // Finalize code - write the entry point code now we know how many
  // registers we need.

  // Entry code:
  __ bind(&entry_label_);
  // Save callee-save registers.  Order here should correspond to order of
  // kBackup_ebx etc.
  __ push(esi);
  __ push(edi);
  __ push(ebx);  // Callee-save on MacOS.
  __ enter(Immediate(num_registers_ * kPointerSize));
  // Load string length.
  __ mov(esi, Operand(ebp, kInputEndOffset));
  // Load input position.
  __ mov(edi, Operand(ebp, kInputStartOffset));
  // Set up edi to be negative offset from string end.
  __ sub(edi, Operand(esi));
  // Set up esi to be end of string.  First get location.
  __ mov(edx, Operand(ebp, kInputBuffer));
  // Dereference location to get string start.
  __ mov(edx, Operand(edx, 0));
  // Add start to length to complete esi setup.
  __ add(esi, Operand(edx));
  if (num_saved_registers_ > 0) {
    // Fill saved registers with initial value = start offset - 1
    __ mov(ecx, -num_saved_registers_);
    __ mov(eax, Operand(edi));
    __ sub(Operand(eax), Immediate(char_size()));
    Label init_loop;
    __ bind(&init_loop);
    __ mov(Operand(ebp, ecx, times_4, +0), eax);
    __ inc(ecx);
    __ j(not_equal, &init_loop);
  }
  // Load previous char as initial value of current-character.
  Label at_start;
  __ cmp(Operand(ebp, kAtStart), Immediate(0));
  __ j(not_equal, &at_start);
  LoadCurrentCharToRegister(-1);  // Load previous char.
  __ jmp(&start_label_);
  __ bind(&at_start);
  __ mov(current_character(), '\n');
  __ jmp(&start_label_);


  // Exit code:
  // Success
  __ bind(&success_label_);
  if (num_saved_registers_ > 0) {
    // copy captures to output
    __ mov(ebx, Operand(ebp, kRegisterOutput));
    __ mov(ecx, Operand(ebp, kInputEndOffset));
    __ sub(ecx, Operand(ebp, kInputStartOffset));
    for (int i = 0; i < num_saved_registers_; i++) {
      __ mov(eax, register_location(i));
      __ add(eax, Operand(ecx));  // Convert to index from start, not end.
      if (char_size() > 1) {
        ASSERT(char_size() == 2);
        __ sar(eax, 1);  // Convert to character index, not byte.
      }
      __ mov(Operand(ebx, i * kPointerSize), eax);
    }
  }
  __ mov(eax, Immediate(1));

  // Exit and return eax
  __ bind(&exit_label_);
  __ leave();
  __ pop(ebx);
  __ pop(edi);
  __ pop(esi);
  __ ret(0);

  CodeDesc code_desc;
  masm_->GetCode(&code_desc);
  Handle<Code> code = Factory::NewCode(code_desc,
                                       NULL,
                                       Code::ComputeFlags(Code::REGEXP),
                                       self_);
  LOG(CodeCreateEvent("RegExp", *code, "(Compiled RegExp)"));
  return Handle<Object>::cast(code);
}


void RegExpMacroAssemblerIA32::GoTo(Label* to) {
  __ jmp(to);
}


void RegExpMacroAssemblerIA32::IfRegisterGE(int reg,
                                            int comparand,
                                            Label* if_ge) {
  __ cmp(register_location(reg), Immediate(comparand));
  BranchOrBacktrack(greater_equal, if_ge);
}


void RegExpMacroAssemblerIA32::IfRegisterLT(int reg,
                                            int comparand,
                                            Label* if_lt) {
  __ cmp(register_location(reg), Immediate(comparand));
  BranchOrBacktrack(less, if_lt);
}


RegExpMacroAssembler::IrregexpImplementation
    RegExpMacroAssemblerIA32::Implementation() {
  return kIA32Implementation;
}


void RegExpMacroAssemblerIA32::LoadCurrentCharacter(int cp_offset,
                                                    Label* on_end_of_input) {
  ASSERT(cp_offset >= 0);
  ASSERT(cp_offset < (1<<30));  // Be sane! (And ensure negation works)
  __ cmp(edi, -cp_offset * char_size());
  BranchOrBacktrack(greater_equal, on_end_of_input);
  LoadCurrentCharToRegister(cp_offset);
}


void RegExpMacroAssemblerIA32::PopCurrentPosition() {
  __ pop(edi);
}


void RegExpMacroAssemblerIA32::PopRegister(int register_index) {
  __ pop(register_location(register_index));
}


void RegExpMacroAssemblerIA32::PushBacktrack(Label* label) {
  // CheckStackLimit();  // Not ready yet.
  __ push(label, RelocInfo::NONE);
}


void RegExpMacroAssemblerIA32::PushCurrentPosition() {
  __ push(edi);
}


void RegExpMacroAssemblerIA32::PushRegister(int register_index) {
  __ push(register_location(register_index));
}


void RegExpMacroAssemblerIA32::ReadCurrentPositionFromRegister(int reg) {
  __ mov(edi, register_location(reg));
}


void RegExpMacroAssemblerIA32::ReadStackPointerFromRegister(int reg) {
  __ mov(esp, register_location(reg));
}


void RegExpMacroAssemblerIA32::SetRegister(int register_index, int to) {
  ASSERT(register_index >= num_saved_registers_);  // Reserved for positions!
  __ mov(register_location(register_index), Immediate(to));
}


void RegExpMacroAssemblerIA32::Succeed() {
  __ jmp(&success_label_);
}


void RegExpMacroAssemblerIA32::WriteCurrentPositionToRegister(int reg) {
  __ mov(register_location(reg), edi);
}

void RegExpMacroAssemblerIA32::WriteStackPointerToRegister(int reg) {
  __ mov(register_location(reg), esp);
}


// Private methods:

Operand RegExpMacroAssemblerIA32::register_location(int register_index) {
  ASSERT(register_index < (1<<30));
  if (num_registers_ <= register_index) {
    num_registers_ = register_index + 1;
  }
  return Operand(ebp, -(register_index + 1) * kPointerSize);
}


Register RegExpMacroAssemblerIA32::current_character() {
  return edx;
}


size_t RegExpMacroAssemblerIA32::char_size() {
  return static_cast<size_t>(mode_);
}


void RegExpMacroAssemblerIA32::BranchOrBacktrack(Condition condition,
                                                 Label* to) {
  if (condition < 0) {  // No condition
    if (to == NULL) {
      Backtrack();
      return;
    }
    __ jmp(to);
    return;
  }
  if (to == NULL) {
    Label skip;
    __ j(NegateCondition(condition), &skip);
    Backtrack();
    __ bind(&skip);
    return;
  }
  __ j(condition, to);
}


void RegExpMacroAssemblerIA32::CheckStackLimit() {
  if (FLAG_check_stack) {
    // Check for preemption first.
    Label no_preempt;
    Label retry_preempt;
    // Check for preemption.
    ExternalReference stack_guard_limit =
        ExternalReference::address_of_stack_guard_limit();
    __ cmp(esp, Operand::StaticVariable(stack_guard_limit));
    __ j(above, &no_preempt, taken);

    __ push(edi);  // Current position.
    __ push(edx);  // Current character.
    // Restore original edi, esi.
    __ mov(edi, Operand(ebp, kBackup_edi));
    __ mov(esi, Operand(ebp, kBackup_esi));

    __ bind(&retry_preempt);
    // simulate stack for Runtime call.
    __ push(eax);
    __ push(Immediate(Smi::FromInt(0)));  // Dummy receiver
    __ CallRuntime(Runtime::kStackGuard, 1);
    __ pop(eax);

    __ cmp(esp, Operand::StaticVariable(stack_guard_limit));
    __ j(below_equal, &retry_preempt);

    __ pop(edx);
    __ pop(edi);
    __ mov(esi, Operand(ebp, kInputBuffer));
    __ mov(esi, Operand(esi, 0));
    __ add(esi, Operand(ebp, kInputEndOffset));

    __ bind(&no_preempt);
  }
}


void RegExpMacroAssemblerIA32::LoadCurrentCharToRegister(int cp_offset) {
  if (mode_ == ASCII) {
    __ movzx_b(current_character(), Operand(esi, edi, times_1, cp_offset));
    return;
  }
  ASSERT(mode_ == UC16);
  __ movzx_w(current_character(),
             Operand(esi, edi, times_1, cp_offset * sizeof(uc16)));
}


void RegExpMacroAssemblerIA32::LoadConstantBufferAddress(Register reg,
                                                         ArraySlice* buffer) {
  __ mov(reg, buffer->array());
  __ add(Operand(reg), Immediate(buffer->base_offset()));
}

#undef __
}}  // namespace v8::internal
