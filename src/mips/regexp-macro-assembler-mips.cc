// Copyright 2006-2010 the V8 project authors. All rights reserved.
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

#if defined(V8_TARGET_ARCH_MIPS)

#include "unicode.h"
#include "log.h"
#include "code-stubs.h"
#include "regexp-stack.h"
#include "macro-assembler.h"
#include "regexp-macro-assembler.h"
#include "mips/regexp-macro-assembler-mips.h"

namespace v8 {
namespace internal {

#ifndef V8_INTERPRETED_REGEXP
/*
 * This assembler uses the following register assignment convention
 * - t1 : Pointer to current code object (Code*) including heap object tag.
 * - t2 : Current position in input, as negative offset from end of string.
 *        Please notice that this is the byte offset, not the character offset!
 * - t3 : Currently loaded character. Must be loaded using
 *        LoadCurrentCharacter before using any of the dispatch methods.
 * - t4 : points to tip of backtrack stack
 * - t5 : Unused.
 * - t6 : End of input (points to byte after last character in input).
 * - fp : Frame pointer. Used to access arguments, local variables and
 *         RegExp registers.
 * - sp : points to tip of C stack.
 *
 * The remaining registers are free for computations.
 *
 * Each call to a public method should retain this convention.
 * The stack will have the following structure:
 *       - direct_call        (if 1, direct call from JavaScript code, if 0 call
 *                             through the runtime system)
 *       - stack_area_base    (High end of the memory area to use as
 *                             backtracking stack)
 *       - int* capture_array (int[num_saved_registers_], for output).
 *       - stack frame header (16 bytes in size)
 *       --- sp when called ---
 *       - link address
 *       - backup of registers s0..s7
 *       - end of input       (Address of end of string)
 *       - start of input     (Address of first character in string)
 *       - start index        (character index of start)
 *       --- frame pointer ----
 *       - void* input_string (location of a handle containing the string)
 *       - Offset of location before start of input (effectively character
 *         position -1). Used to initialize capture registers to a non-position.
 *       - At start (if 1, we are starting at the start of the
 *         string, otherwise 0)
 *       - register 0         (Only positions must be stored in the first
 *       - register 1          num_saved_registers_ registers)
 *       - ...
 *       - register num_registers-1
 *       --- sp ---
 *
 * The first num_saved_registers_ registers are initialized to point to
 * "character -1" in the string (i.e., char_size() bytes before the first
 * character of the string). The remaining registers start out as garbage.
 *
 * The data up to the return address must be placed there by the calling
 * code, by calling the code entry as cast to a function with the signature:
 * int (*match)(String* input_string,
 *              int start_index,
 *              Address start,
 *              Address end,
 *              int* capture_output_array,
 *              bool at_start,
 *              byte* stack_area_base,
 *              bool direct_call)
 * The call is performed by NativeRegExpMacroAssembler::Execute()
 * (in regexp-macro-assembler.cc).
 */

#define __ ACCESS_MASM(masm_)

RegExpMacroAssemblerMIPS::RegExpMacroAssemblerMIPS(
    Mode mode,
    int registers_to_save)
    : masm_(new MacroAssembler(NULL, kRegExpCodeSize)),
      mode_(mode),
      num_registers_(registers_to_save),
      num_saved_registers_(registers_to_save),
      entry_label_(),
      start_label_(),
      success_label_(),
      backtrack_label_(),
      exit_label_() {
  ASSERT_EQ(0, registers_to_save % 2);
  __ jmp(&entry_label_);   // We'll write the entry code later.
  __ bind(&start_label_);  // And then continue from here.
}


RegExpMacroAssemblerMIPS::~RegExpMacroAssemblerMIPS() {
  delete masm_;
  // Unuse labels in case we throw away the assembler without calling GetCode.
  entry_label_.Unuse();
  start_label_.Unuse();
  success_label_.Unuse();
  backtrack_label_.Unuse();
  exit_label_.Unuse();
  check_preempt_label_.Unuse();
  stack_overflow_label_.Unuse();
}


int RegExpMacroAssemblerMIPS::stack_limit_slack()  {
  return RegExpStack::kStackLimitSlack;
}


void RegExpMacroAssemblerMIPS::AdvanceCurrentPosition(int by) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::AdvanceRegister(int reg, int by) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::Backtrack() {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::Bind(Label* label) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckCharacter(uint32_t c, Label* on_equal) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckCharacterGT(uc16 limit, Label* on_greater) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckAtStart(Label* on_at_start) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckNotAtStart(Label* on_not_at_start) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckCharacterLT(uc16 limit, Label* on_less) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckCharacters(Vector<const uc16> str,
                                              int cp_offset,
                                              Label* on_failure,
                                              bool check_end_of_string) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckGreedyLoop(Label* on_equal) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckNotBackReferenceIgnoreCase(
    int start_reg,
    Label* on_no_match) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckNotBackReference(
    int start_reg,
    Label* on_no_match) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckNotRegistersEqual(int reg1,
                                                      int reg2,
                                                      Label* on_not_equal) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckNotCharacter(uint32_t c,
                                                Label* on_not_equal) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckCharacterAfterAnd(uint32_t c,
                                                     uint32_t mask,
                                                     Label* on_equal) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckNotCharacterAfterAnd(uint32_t c,
                                                        uint32_t mask,
                                                        Label* on_not_equal) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckNotCharacterAfterMinusAnd(
    uc16 c,
    uc16 minus,
    uc16 mask,
    Label* on_not_equal) {
  UNIMPLEMENTED_MIPS();
}


bool RegExpMacroAssemblerMIPS::CheckSpecialCharacterClass(uc16 type,
                                                         Label* on_no_match) {
  UNIMPLEMENTED_MIPS();
  return false;
}


void RegExpMacroAssemblerMIPS::Fail() {
  UNIMPLEMENTED_MIPS();
}


Handle<HeapObject> RegExpMacroAssemblerMIPS::GetCode(Handle<String> source) {
  UNIMPLEMENTED_MIPS();
  return Handle<HeapObject>::null();
}


void RegExpMacroAssemblerMIPS::GoTo(Label* to) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::IfRegisterGE(int reg,
                                           int comparand,
                                           Label* if_ge) {
  __ lw(a0, register_location(reg));
    BranchOrBacktrack(if_ge, ge, a0, Operand(comparand));
}


void RegExpMacroAssemblerMIPS::IfRegisterLT(int reg,
                                           int comparand,
                                           Label* if_lt) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::IfRegisterEqPos(int reg,
                                              Label* if_eq) {
  UNIMPLEMENTED_MIPS();
}


RegExpMacroAssembler::IrregexpImplementation
    RegExpMacroAssemblerMIPS::Implementation() {
  return kMIPSImplementation;
}


void RegExpMacroAssemblerMIPS::LoadCurrentCharacter(int cp_offset,
                                                   Label* on_end_of_input,
                                                   bool check_bounds,
                                                   int characters) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::PopCurrentPosition() {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::PopRegister(int register_index) {
  UNIMPLEMENTED_MIPS();
}



void RegExpMacroAssemblerMIPS::PushBacktrack(Label* label) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::PushCurrentPosition() {
  Push(current_input_offset());
}


void RegExpMacroAssemblerMIPS::PushRegister(int register_index,
                                           StackCheckFlag check_stack_limit) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::ReadCurrentPositionFromRegister(int reg) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::ReadStackPointerFromRegister(int reg) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::SetCurrentPositionFromEnd(int by) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::SetRegister(int register_index, int to) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::Succeed() {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::WriteCurrentPositionToRegister(int reg,
                                                             int cp_offset) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::ClearRegisters(int reg_from, int reg_to) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::WriteStackPointerToRegister(int reg) {
  UNIMPLEMENTED_MIPS();
}


// Private methods:

void RegExpMacroAssemblerMIPS::CallCheckStackGuardState(Register scratch) {
  UNIMPLEMENTED_MIPS();
}


// Helper function for reading a value out of a stack frame.
template <typename T>
static T& frame_entry(Address re_frame, int frame_offset) {
  return reinterpret_cast<T&>(Memory::int32_at(re_frame + frame_offset));
}


int RegExpMacroAssemblerMIPS::CheckStackGuardState(Address* return_address,
                                                  Code* re_code,
                                                  Address re_frame) {
  UNIMPLEMENTED_MIPS();
  return 0;
}


MemOperand RegExpMacroAssemblerMIPS::register_location(int register_index) {
  UNIMPLEMENTED_MIPS();
  return MemOperand(zero_reg, 0);
}


void RegExpMacroAssemblerMIPS::CheckPosition(int cp_offset,
                                            Label* on_outside_input) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::BranchOrBacktrack(Label* to,
                                                 Condition condition,
                                                 Register rs,
                                                 const Operand& rt) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::SafeCall(Label* to, Condition cond, Register rs,
                                           const Operand& rt) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::SafeReturn() {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::SafeCallTarget(Label* name) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::Push(Register source) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::Pop(Register target) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckPreemption() {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CheckStackLimit() {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::CallCFunctionUsingStub(
    ExternalReference function,
    int num_arguments) {
  UNIMPLEMENTED_MIPS();
}


void RegExpMacroAssemblerMIPS::LoadCurrentCharacterUnchecked(int cp_offset,
                                                             int characters) {
  UNIMPLEMENTED_MIPS();
}


void RegExpCEntryStub::Generate(MacroAssembler* masm_) {
  UNIMPLEMENTED_MIPS();
}


#undef __

#endif  // V8_INTERPRETED_REGEXP

}}  // namespace v8::internal

#endif  // V8_TARGET_ARCH_MIPS
