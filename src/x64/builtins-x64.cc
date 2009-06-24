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

static void EnterArgumentsAdaptorFrame(MacroAssembler* masm) {
  __ push(rbp);
  __ movq(rbp, rsp);

  // Store the arguments adaptor context sentinel.
  __ push(Immediate(ArgumentsAdaptorFrame::SENTINEL));

  // Push the function on the stack.
  __ push(rdi);

  // Preserve the number of arguments on the stack. Must preserve both
  // rax and rbx because these registers are used when copying the
  // arguments and the receiver.
  ASSERT(kSmiTagSize == 1);
  __ lea(rcx, Operand(rax, rax, times_1, kSmiTag));
  __ push(rcx);
}


static void LeaveArgumentsAdaptorFrame(MacroAssembler* masm) {
  // Retrieve the number of arguments from the stack. Number is a Smi.
  __ movq(rbx, Operand(rbp, ArgumentsAdaptorFrameConstants::kLengthOffset));

  // Leave the frame.
  __ movq(rsp, rbp);
  __ pop(rbp);

  // Remove caller arguments from the stack.
  // rbx holds a Smi, so we convery to dword offset by multiplying by 4.
  ASSERT_EQ(kSmiTagSize, 1 && kSmiTag == 0);
  ASSERT_EQ(kPointerSize, (1 << kSmiTagSize) * 4);
  __ pop(rcx);
  __ lea(rsp, Operand(rsp, rbx, times_4, 1 * kPointerSize));  // 1 ~ receiver
  __ push(rcx);
}


void Builtins::Generate_ArgumentsAdaptorTrampoline(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax : actual number of arguments
  //  -- rbx : expected number of arguments
  //  -- rdx : code entry to call
  // -----------------------------------

  Label invoke, dont_adapt_arguments;
  __ IncrementCounter(&Counters::arguments_adaptors, 1);

  Label enough, too_few;
  __ cmpq(rax, rbx);
  __ j(less, &too_few);
  __ cmpq(rbx, Immediate(SharedFunctionInfo::kDontAdaptArgumentsSentinel));
  __ j(equal, &dont_adapt_arguments);

  {  // Enough parameters: Actual >= expected.
    __ bind(&enough);
    EnterArgumentsAdaptorFrame(masm);

    // Copy receiver and all expected arguments.
    const int offset = StandardFrameConstants::kCallerSPOffset;
    __ lea(rax, Operand(rbp, rax, times_pointer_size, offset));
    __ movq(rcx, Immediate(-1));  // account for receiver

    Label copy;
    __ bind(&copy);
    __ incq(rcx);
    __ push(Operand(rax, 0));
    __ subq(rax, Immediate(kPointerSize));
    __ cmpq(rcx, rbx);
    __ j(less, &copy);
    __ jmp(&invoke);
  }

  {  // Too few parameters: Actual < expected.
    __ bind(&too_few);
    EnterArgumentsAdaptorFrame(masm);

    // Copy receiver and all actual arguments.
    const int offset = StandardFrameConstants::kCallerSPOffset;
    __ lea(rdi, Operand(rbp, rax, times_pointer_size, offset));
    __ movq(rcx, Immediate(-1));  // account for receiver

    Label copy;
    __ bind(&copy);
    __ incq(rcx);
    __ push(Operand(rdi, 0));
    __ subq(rdi, Immediate(kPointerSize));
    __ cmpq(rcx, rax);
    __ j(less, &copy);

    // Fill remaining expected arguments with undefined values.
    Label fill;
    __ movq(kScratchRegister,
            Factory::undefined_value(),
            RelocInfo::EMBEDDED_OBJECT);
    __ bind(&fill);
    __ incq(rcx);
    __ push(kScratchRegister);
    __ cmpq(rcx, rbx);
    __ j(less, &fill);

    // Restore function pointer.
    __ movq(rdi, Operand(rbp, JavaScriptFrameConstants::kFunctionOffset));
  }

  // Call the entry point.
  __ bind(&invoke);
  __ call(rdx);

  // Leave frame and return.
  LeaveArgumentsAdaptorFrame(masm);
  __ ret(0);

  // -------------------------------------------
  // Dont adapt arguments.
  // -------------------------------------------
  __ bind(&dont_adapt_arguments);
  __ jmp(rdx);
}


void Builtins::Generate_FunctionApply(MacroAssembler* masm) {
  masm->int3();  // UNIMPLEMENTED.
}

void Builtins::Generate_FunctionCall(MacroAssembler* masm) {
  masm->int3();  // UNIMPLEMENTED.
}


void Builtins::Generate_JSConstructCall(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax: number of arguments
  //  -- rdi: constructor function
  // -----------------------------------

  Label non_function_call;
  // Check that function is not a smi.
  __ testl(rdi, Immediate(kSmiTagMask));
  __ j(zero, &non_function_call);
  // Check that function is a JSFunction.
  __ CmpObjectType(rdi, JS_FUNCTION_TYPE, rcx);
  __ j(not_equal, &non_function_call);

  // Jump to the function-specific construct stub.
  __ movq(rbx, FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
  __ movq(rbx, FieldOperand(rbx, SharedFunctionInfo::kConstructStubOffset));
  __ lea(rbx, FieldOperand(rbx, Code::kHeaderSize));
  __ jmp(rbx);

  // edi: called object
  // eax: number of arguments
  __ bind(&non_function_call);

  // Set expected number of arguments to zero (not changing eax).
  __ movq(rbx, Immediate(0));
  __ GetBuiltinEntry(rdx, Builtins::CALL_NON_FUNCTION_AS_CONSTRUCTOR);
  __ Jump(Handle<Code>(builtin(ArgumentsAdaptorTrampoline)),
          RelocInfo::CODE_TARGET);
}


void Builtins::Generate_JSConstructStubGeneric(MacroAssembler* masm) {
    // Enter a construct frame.
  __ EnterConstructFrame();

  // Store a smi-tagged arguments count on the stack.
  __ shl(rax, Immediate(kSmiTagSize));
  __ push(rax);

  // Push the function to invoke on the stack.
  __ push(rdi);

  // Try to allocate the object without transitioning into C code. If any of the
  // preconditions is not met, the code bails out to the runtime call.
  Label rt_call, allocated;

  // TODO(x64): Implement inlined allocation.

  // Allocate the new receiver object using the runtime call.
  // rdi: function (constructor)
  __ bind(&rt_call);
  // Must restore edi (constructor) before calling runtime.
  __ movq(rdi, Operand(rsp, 0));
  __ push(rdi);
  __ CallRuntime(Runtime::kNewObject, 1);
  __ movq(rbx, rax);  // store result in rbx

  // New object allocated.
  // rbx: newly allocated object
  __ bind(&allocated);
  // Retrieve the function from the stack.
  __ pop(rdi);

  // Retrieve smi-tagged arguments count from the stack.
  __ movq(rax, Operand(rsp, 0));
  __ shr(rax, Immediate(kSmiTagSize));

  // Push the allocated receiver to the stack. We need two copies
  // because we may have to return the original one and the calling
  // conventions dictate that the called function pops the receiver.
  __ push(rbx);
  __ push(rbx);

  // Setup pointer to last argument.
  __ lea(rbx, Operand(rbp, StandardFrameConstants::kCallerSPOffset));

  // Copy arguments and receiver to the expression stack.
  Label loop, entry;
  __ movq(rcx, rax);
  __ jmp(&entry);
  __ bind(&loop);
  __ push(Operand(rbx, rcx, times_pointer_size, 0));
  __ bind(&entry);
  __ decq(rcx);
  __ j(greater_equal, &loop);

  // Call the function.
  ParameterCount actual(rax);
  __ InvokeFunction(rdi, actual, CALL_FUNCTION);

  // Restore context from the frame.
  __ movq(rsi, Operand(rbp, StandardFrameConstants::kContextOffset));

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.
  Label use_receiver, exit;
  // If the result is a smi, it is *not* an object in the ECMA sense.
  __ testl(rax, Immediate(kSmiTagMask));
  __ j(zero, &use_receiver);

  // If the type of the result (stored in its map) is less than
  // FIRST_JS_OBJECT_TYPE, it is not an object in the ECMA sense.
  __ CmpObjectType(rax, FIRST_JS_OBJECT_TYPE, rcx);
  __ j(greater_equal, &exit);

  // Throw away the result of the constructor invocation and use the
  // on-stack receiver as the result.
  __ bind(&use_receiver);
  __ movq(rax, Operand(rsp, 0));

  // Restore the arguments count and leave the construct frame.
  __ bind(&exit);
  __ movq(rbx, Operand(rsp, kPointerSize));  // get arguments count
  __ LeaveConstructFrame();

  // Remove caller arguments from the stack and return.
  ASSERT(kSmiTagSize == 1 && kSmiTag == 0);
  __ pop(rcx);
  __ lea(rsp, Operand(rsp, rbx, times_4, 1 * kPointerSize));  // 1 ~ receiver
  __ push(rcx);
  __ ret(0);
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
  __ movq(kScratchRegister, Operand(rbx, rcx, times_pointer_size, 0));
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
    // Function must be in rdi.
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
