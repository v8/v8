// Copyright 2010 the V8 project authors. All rights reserved.
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

#include "codegen-inl.h"
#include "register-allocator-inl.h"
#include "scopes.h"
#include "virtual-frame-inl.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm())

void VirtualFrame::PopToA1A0() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::PopToA1() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::PopToA0() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::MergeTo(const VirtualFrame* expected,
                           Condition cond,
                           Register r1,
                           const Operand& r2) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::MergeTo(VirtualFrame* expected,
                           Condition cond,
                           Register r1,
                           const Operand& r2) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::MergeTOSTo(
    VirtualFrame::TopOfStack expected_top_of_stack_state,
    Condition cond,
    Register r1,
    const Operand& r2) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::Enter() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::Exit() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::AllocateStackSlots() {
  UNIMPLEMENTED_MIPS();
}



void VirtualFrame::PushReceiverSlotAddress() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::PushTryHandler(HandlerType type) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::CallJSFunction(int arg_count) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::CallRuntime(const Runtime::Function* f, int arg_count) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::CallRuntime(Runtime::FunctionId id, int arg_count) {
  UNIMPLEMENTED_MIPS();
}


#ifdef ENABLE_DEBUGGER_SUPPORT
void VirtualFrame::DebugBreak() {
  UNIMPLEMENTED_MIPS();
}
#endif


void VirtualFrame::InvokeBuiltin(Builtins::JavaScript id,
                                 InvokeJSFlags flags,
                                 int arg_count) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::CallLoadIC(Handle<String> name, RelocInfo::Mode mode) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::CallStoreIC(Handle<String> name, bool is_contextual) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::CallKeyedLoadIC() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::CallKeyedStoreIC() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::CallCodeObject(Handle<Code> code,
                                  RelocInfo::Mode rmode,
                                  int dropped_args) {
  UNIMPLEMENTED_MIPS();
}


//    NO_TOS_REGISTERS, A0_TOS, A1_TOS, A1_A0_TOS, A0_A1_TOS.
const bool VirtualFrame::kA0InUse[TOS_STATES] =
    { false,            true,   false,  true,      true };
const bool VirtualFrame::kA1InUse[TOS_STATES] =
    { false,            false,  true,   true,      true };
const int VirtualFrame::kVirtualElements[TOS_STATES] =
    { 0,                1,      1,      2,         2 };
const Register VirtualFrame::kTopRegister[TOS_STATES] =
    { a0,               a0,     a1,     a1,        a0 };
const Register VirtualFrame::kBottomRegister[TOS_STATES] =
    { a0,               a0,     a1,     a0,        a1 };
const Register VirtualFrame::kAllocatedRegisters[
    VirtualFrame::kNumberOfAllocatedRegisters] = { a2, a3, t0, t1, t2 };
// Popping is done by the transition implied by kStateAfterPop.  Of course if
// there were no stack slots allocated to registers then the physical SP must
// be adjusted.
const VirtualFrame::TopOfStack VirtualFrame::kStateAfterPop[TOS_STATES] =
    { NO_TOS_REGISTERS, NO_TOS_REGISTERS, NO_TOS_REGISTERS, A0_TOS, A1_TOS };
// Pushing is done by the transition implied by kStateAfterPush.  Of course if
// the maximum number of registers was already allocated to the top of stack
// slots then one register must be physically pushed onto the stack.
const VirtualFrame::TopOfStack VirtualFrame::kStateAfterPush[TOS_STATES] =
    { A0_TOS, A1_A0_TOS, A0_A1_TOS, A0_A1_TOS, A1_A0_TOS };


void VirtualFrame::Drop(int count) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::Pop() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::EmitPop(Register reg) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::SpillAllButCopyTOSToA0() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::SpillAllButCopyTOSToA1() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::SpillAllButCopyTOSToA1A0() {
  UNIMPLEMENTED_MIPS();
}


Register VirtualFrame::Peek() {
  UNIMPLEMENTED_MIPS();
  return no_reg;
}


Register VirtualFrame::Peek2() {
  UNIMPLEMENTED_MIPS();
  return no_reg;
}


void VirtualFrame::Dup() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::Dup2() {
  UNIMPLEMENTED_MIPS();
}


Register VirtualFrame::PopToRegister(Register but_not_to_this_one) {
  UNIMPLEMENTED_MIPS();
  return no_reg;
}


void VirtualFrame::EnsureOneFreeTOSRegister() {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::EmitMultiPop(RegList regs) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::EmitPush(Register reg, TypeInfo info) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::SetElementAt(Register reg, int this_far_down) {
  UNIMPLEMENTED_MIPS();
}


Register VirtualFrame::GetTOSRegister() {
  UNIMPLEMENTED_MIPS();
  return no_reg;
}


void VirtualFrame::EmitPush(Operand operand, TypeInfo info) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::EmitPush(MemOperand operand, TypeInfo info) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::EmitPushRoot(Heap::RootListIndex index) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::EmitMultiPush(RegList regs) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::EmitMultiPushReversed(RegList regs) {
  UNIMPLEMENTED_MIPS();
}


void VirtualFrame::SpillAll() {
  UNIMPLEMENTED_MIPS();
}


#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_MIPS
