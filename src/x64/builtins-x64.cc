// Copyright 2009 the V8 project authors. All rights reserved.
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
#include "codegen-inl.h"
#include "macro-assembler.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

void Builtins::Generate_Adaptor(MacroAssembler* masm,
                                Builtins::CFunctionId id) {
  masm->int3();  // UNIMPLEMENTED.
}

void Builtins::Generate_ArgumentsAdaptorTrampoline(MacroAssembler* masm) {
  masm->int3();  // UNIMPLEMENTED.
}

void Builtins::Generate_FunctionApply(MacroAssembler* masm) {
  masm->int3();  // UNIMPLEMENTED.
}

void Builtins::Generate_FunctionCall(MacroAssembler* masm) {
  masm->int3();  // UNIMPLEMENTED.
}

void Builtins::Generate_JSConstructCall(MacroAssembler* masm) {
  masm->int3();  // UNIMPLEMENTED.
}

static void Generate_JSEntryTrampolineHelper(MacroAssembler* masm,
                                             bool is_construct) {
  // Expects five C++ function parameters.
  // - Address entry (ignored)
  // - JSFunction* function (
  // - Object* receiver
  // - int argc
  // - Object*** argv
  // (see Handle::Invoke in execution.cc).

  // Platform specific argument handling. After this, the stack contains
  // an internal frame and the pushed function and receiver, and
  // register rax and rbx holds the argument count and argument array,
  // while rdi holds the function pointer and rsi the context.
#ifdef __MSVC__
  // MSVC parameters in:
  // rcx : entry (ignored)
  // rdx : function
  // r8 : receiver
  // r9 : argc
  // [rsp+0x20] : argv

  // Clear the context before we push it when entering the JS frame.
  __ xor_(rsi, rsi);
  // Enter an internal frame.
  __ EnterInternalFrame();


  // Load the function context into rsi.
  __ movq(rsi, FieldOperand(rdx, JSFunction::kContextOffset));

  // Push the function and the receiver onto the stack.
  __ push(rdx);
  __ push(r8);

  // Load the number of arguments and setup pointer to the arguments.
  __ movq(rax, r9);
  // Load the previous frame pointer to access C argument on stack
  __ movq(kScratchRegister, Operand(rbp, 0));
  __ movq(rbx, Operand(kScratchRegister, EntryFrameConstants::kArgvOffset));
  // Load the function pointer into rdi.
  __ movq(rdi, rdx);
#else  // !defined(__MSVC__)
  // GCC parameters in:
  // rdi : entry (ignored)
  // rsi : function
  // rdx : receiver
  // rcx : argc
  // r8  : argv

  __ movq(rdi, rsi);
  // rdi : function

  // Clear the context before we push it when entering the JS frame.
  __ xor_(rsi, rsi);
  // Enter an internal frame.
  __ EnterInternalFrame();

  // Push the function and receiver and setup the context.
  __ push(rdi);
  __ push(rdx);
  __ movq(rsi, FieldOperand(rdi, JSFunction::kContextOffset));

  // Load the number of arguments and setup pointer to the arguments.
  __ movq(rax, rcx);
  __ movq(rbx, r8);
#endif  // __MSVC__
  // Current stack contents:
  // [rsp + 2 * kPointerSize ... ]: Internal frame
  // [rsp + kPointerSize]         : function
  // [rsp]                        : receiver
  // Current register contents:
  // rax : argc
  // rbx : argv
  // rsi : context
  // rdi : function

  // Copy arguments to the stack in a loop.
  // Register rbx points to array of pointers to handle locations.
  // Push the values of these handles.
  Label loop, entry;
  __ xor_(rcx, rcx);  // Set loop variable to 0.
  __ jmp(&entry);
  __ bind(&loop);
  __ movq(kScratchRegister, Operand(rbx, rcx, kTimesPointerSize, 0));
  __ push(Operand(kScratchRegister, 0));  // dereference handle
  __ addq(rcx, Immediate(1));
  __ bind(&entry);
  __ cmpq(rcx, rax);
  __ j(not_equal, &loop);

  // Invoke the code.
  if (is_construct) {
    // Expects rdi to hold function pointer.
    __ movq(kScratchRegister,
            Handle<Code>(Builtins::builtin(Builtins::JSConstructCall)),
            RelocInfo::CODE_TARGET);
    __ call(kScratchRegister);
  } else {
    ParameterCount actual(rax);
    __ InvokeFunction(rdi, actual, CALL_FUNCTION);
  }

  // Exit the JS frame. Notice that this also removes the empty
  // context and the function left on the stack by the code
  // invocation.
  __ LeaveInternalFrame();
  // TODO(X64): Is argument correct? Is there a receiver to remove?
  __ ret(1 * kPointerSize);  // remove receiver
}


void Builtins::Generate_JSEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, false);
}


void Builtins::Generate_JSConstructEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, true);
}

} }  // namespace v8::internal
