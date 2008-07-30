// Copyright 2006-2008 Google Inc. All Rights Reserved.
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
#include "debug.h"
#include "runtime.h"

namespace v8 { namespace internal {


#define __ masm->


void Builtins::Generate_Adaptor(MacroAssembler* masm,
                                int argc,
                                CFunctionId id) {
  __ JumpToBuiltin(ExternalReference(id));
}


void Builtins::Generate_JSConstructCall(MacroAssembler* masm) {
  // r0: number of arguments

  __ EnterJSFrame(0);

  // Allocate the new receiver object.
  __ push(r0);
  __ ldr(r0, MemOperand(pp, JavaScriptFrameConstants::kFunctionOffset));
  __ CallRuntime(Runtime::kNewObject, 1);
  __ push(r0);  // empty TOS cache

  // Push the function and the allocated receiver from the stack.
  __ ldr(r1, MemOperand(pp, JavaScriptFrameConstants::kFunctionOffset));
  __ push(r1);  // function
  __ push(r0);  // receiver

  // Restore the arguments length from the stack.
  __ ldr(r0, MemOperand(fp, JavaScriptFrameConstants::kArgsLengthOffset));

  // Setup pointer to last argument - receiver is not counted.
  __ sub(r2, pp, Operand(r0, LSL, kPointerSizeLog2));
  __ sub(r2, r2, Operand(kPointerSize));

  // Copy arguments and receiver to the expression stack.
  Label loop, entry;
  __ mov(r1, Operand(r0));
  __ b(&entry);
  __ bind(&loop);
  __ ldr(r3, MemOperand(r2, r1, LSL, kPointerSizeLog2));
  __ push(r3);
  __ bind(&entry);
  __ sub(r1, r1, Operand(1), SetCC);
  __ b(ge, &loop);

  // Get the function to call from the stack and get the code from it.
  __ ldr(r1, MemOperand(pp, JavaScriptFrameConstants::kFunctionOffset));
  __ ldr(cp, FieldMemOperand(r1, JSFunction::kContextOffset));
  __ ldr(r1, FieldMemOperand(r1, JSFunction::kSharedFunctionInfoOffset));
  __ ldr(r1, FieldMemOperand(r1, SharedFunctionInfo::kCodeOffset));
  __ add(r1, r1, Operand(Code::kHeaderSize - kHeapObjectTag));

  // Call the function.
  Label return_site;
  __ RecordPosition(position);
  __ Call(r1);
  __ bind(&return_site);

  // Restore context from the frame and discard the function.
  __ ldr(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  __ add(sp, sp, Operand(kPointerSize));

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.
  Label use_receiver, exit;

  // If the result is a smi, it is *not* an object in the ECMA sense.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &use_receiver);

  // If the type of the result (stored in its map) is less than
  // JS_OBJECT type, it is not an object in the ECMA sense.
  __ ldr(r2, FieldMemOperand(r0, HeapObject::kMapOffset));
  __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
  __ cmp(r2, Operand(JS_OBJECT_TYPE));
  __ b(ge, &exit);

  // Throw away the result of the constructor invocation and use the
  // on-stack receiver as the result.
  __ bind(&use_receiver);
  __ ldr(r0, MemOperand(sp));

  // Remove receiver from the stack, remove caller arguments, and
  // return.
  __ bind(&exit);
  __ ExitJSFrame(RETURN);

  // Compute the offset from the beginning of the JSConstructCall
  // builtin code object to the return address after the call.
  ASSERT(return_site.is_bound());
  construct_call_pc_offset_ = return_site.pos() + Code::kHeaderSize;
}


static void Generate_JSEntryTrampolineHelper(MacroAssembler* masm,
                                             bool is_construct) {
  // Called from Generate_JS_Entry
  // r0: code entry
  // r1: function
  // r2: receiver
  // r3: argc
  // r4: argv
  // r5-r7, cp may be clobbered

  // Enter the JS frame
  // compute parameter pointer before making changes
  __ mov(ip, Operand(sp));  // ip == caller_sp == new pp

  __ mov(r5, Operand(0));  // spare slot to store caller code object during GC
  __ mov(r6, Operand(0));  // no context
  __ mov(r7, Operand(0));  // no incoming parameters
  __ mov(r8, Operand(0));  // caller_pp == NULL for trampoline frames
  ASSERT(cp.bit() == r8.bit());  // adjust the code otherwise

  // push in reverse order:
  // code (r5==0), context (r6==0), args_len (r7==0), caller_pp (r8==0),
  // caller_fp, sp_on_exit (caller_sp), caller_pc
  __ stm(db_w, sp, r5.bit() | r6.bit() | r7.bit() | r8.bit() |
         fp.bit() | ip.bit() | lr.bit());
  // Setup new frame pointer.
  __ add(fp, sp, Operand(-StandardFrameConstants::kCodeOffset));
  __ mov(pp, Operand(ip));  // setup new parameter pointer

  // Setup the context from the function argument.
  __ ldr(cp, FieldMemOperand(r1, JSFunction::kContextOffset));

  // Push the function and the receiver onto the stack.
  __ mov(r5, Operand(r1));  // change save order: function above receiver
  __ stm(db_w, sp, r2.bit() | r5.bit());

  // Copy arguments to the stack in a loop.
  // r3: argc
  // r4: argv, i.e. points to first arg
  Label loop, entry;
  __ add(r2, r4, Operand(r3, LSL, kPointerSizeLog2));
  // r2 points past last arg.
  __ b(&entry);
  __ bind(&loop);
  __ ldr(r1, MemOperand(r4, kPointerSize, PostIndex));  // read next parameter
  __ ldr(r1, MemOperand(r1));  // dereference handle
  __ push(r1);  // push parameter
  __ bind(&entry);
  __ cmp(r4, Operand(r2));
  __ b(ne, &loop);

  // Initialize all JavaScript callee-saved registers, since they will be seen
  // by the garbage collector as part of handlers.
  __ mov(r4, Operand(Factory::undefined_value()));
  __ mov(r5, Operand(r4));
  __ mov(r6, Operand(r4));
  __ mov(r7, Operand(r4));
  if (kR9Available == 1)
    __ mov(r9, Operand(r4));

  // Invoke the code and pass argc as r0.
  if (is_construct) {
    __ mov(r0, Operand(r3));
    __ Call(Handle<Code>(Builtins::builtin(Builtins::JSConstructCall)),
            code_target);
  } else {
    __ mov(ip, Operand(r0));
    __ mov(r0, Operand(r3));
    __ Call(ip);
  }

  // Exit the JS frame and remove the parameters (except function), and return.
  // Respect ABI stack constraint.
  __ add(sp, fp, Operand(StandardFrameConstants::kCallerFPOffset));
  __ ldm(ia, sp, fp.bit() | sp.bit() | pc.bit());

  // r0: result
  // pp: not restored, should not be used anymore
}


void Builtins::Generate_JSEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, false);
}


void Builtins::Generate_JSConstructEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, true);
}


void Builtins::Generate_FunctionApply(MacroAssembler* masm) {
  // TODO(1233523): Implement. Unused for now.
  __ stop("Builtins::Generate_FunctionApply");
}


void Builtins::Generate_ArgumentsAdaptorTrampoline(MacroAssembler* masm) {
  // TODO(1233523): Implement. Unused for now.
  __ stop("Builtins::Generate_ArgumentsAdaptorTrampoline");

  Label return_site;
  __ bind(&return_site);

  // Compute the offset from the beginning of the ArgumentsAdaptorTrampoline
  // builtin code object to the return address after the call.
  ASSERT(return_site.is_bound());
  arguments_adaptor_call_pc_offset_ = return_site.pos() + Code::kHeaderSize;
}


static void Generate_DebugBreakCallHelper(MacroAssembler* masm,
                                          RegList pointer_regs) {
  // Save the content of all general purpose registers in memory. This copy in
  // memory is later pushed onto the JS expression stack for the fake JS frame
  // generated and also to the C frame generated on top of that. In the JS
  // frame ONLY the registers containing pointers will be pushed on the
  // expression stack. This causes the GC to update these  pointers so that
  // they will have the correct value when returning from the debugger.
  __ SaveRegistersToMemory(kJSCallerSaved);

  // This is a direct call from a debug breakpoint. To build a fake JS frame
  // with no parameters push a function and a receiver, keep the current
  // return address in lr, and set r0 to zero.
  __ mov(ip, Operand(ExternalReference::the_hole_value_location()));
  __ ldr(r3, MemOperand(ip));
  __ mov(r0, Operand(0));  // Null receiver and zero arguments.
  __ stm(db_w, sp, r0.bit() | r3.bit());  // push function and receiver

  // r0: number of arguments.
  // What follows is an inlined version of EnterJSFrame(0, 0).
  // It needs to be kept in sync if any calling conventions are changed.

  // Compute parameter pointer before making changes
  // ip = sp + kPointerSize*(args_len+1);  // +1 for receiver, args_len == 0
  __ add(ip, sp, Operand(kPointerSize));

  __ mov(r3, Operand(0));  // args_len to be saved
  __ mov(r2, Operand(cp));  // context to be saved

  // push in reverse order: context (r2), args_len (r3), caller_pp, caller_fp,
  // sp_on_exit (ip == pp), return address
  __ stm(db_w, sp, r2.bit() | r3.bit() | pp.bit() | fp.bit() |
         ip.bit() | lr.bit());
  // Setup new frame pointer.
  __ add(fp, sp, Operand(-StandardFrameConstants::kContextOffset));
  __ mov(pp, Operand(ip));  // setup new parameter pointer
  // r0 is already set to 0 as spare slot to store caller code object during GC

  // Inlined EnterJSFrame ends here.

  // Empty top-of-stack cache (code pointer).
  __ push(r0);

  // Store the registers containing object pointers on the expression stack to
  // make sure that these are correctly updated during GC.
  // Use sp as base to push.
  __ CopyRegistersFromMemoryToStack(sp, pointer_regs);

  // Empty top-of-stack cache (fake receiver).
  __ push(r0);

#ifdef DEBUG
  __ RecordComment("// Calling from debug break to runtime - come in - over");
#endif
  // r0 is already 0, no arguments
  __ mov(r1, Operand(ExternalReference::debug_break()));

  CEntryDebugBreakStub ceb;
  __ CallStub(&ceb);

  // Restore the register values containing object pointers from the expression
  // stack in the reverse order as they where pushed.
  // Use sp as base to pop.
  __ CopyRegistersFromStackToMemory(sp, r3, pointer_regs);

  // What follows is an inlined version of ExitJSFrame(0).
  // It needs to be kept in sync if any calling conventions are changed.
  // NOTE: loading the return address to lr and discarding the (fake) function
  //       is an addition to this inlined copy.

  __ mov(sp, Operand(fp));  // respect ABI stack constraint
  __ ldm(ia, sp, pp.bit() | fp.bit() | sp.bit() | lr.bit());
  __ add(sp, sp, Operand(kPointerSize));  // discard fake function

  // Inlined ExitJSFrame ends here.

  // Finally restore all registers.
  __ RestoreRegistersFromMemory(kJSCallerSaved);

  // Now that the break point has been handled, resume normal execution by
  // jumping to the target address intended by the caller and that was
  // overwritten by the address of DebugBreakXXX.
  __ mov(ip, Operand(ExternalReference(Debug_Address::AfterBreakTarget())));
  __ ldr(ip, MemOperand(ip));
  __ Jump(ip);
}


void Builtins::Generate_LoadIC_DebugBreak(MacroAssembler* masm) {
  // Calling convention for IC load (from ic-arm.cc).
  // ----------- S t a t e -------------
  //  -- r0    : receiver
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------
  // Registers r0 and r2 contain objects that needs to be pushed on the
  // expression stack of the fake JS frame.
  Generate_DebugBreakCallHelper(masm, r0.bit() | r2.bit());
}


void Builtins::Generate_StoreIC_DebugBreak(MacroAssembler* masm) {
  // Calling convention for IC store (from ic-arm.cc).
  // ----------- S t a t e -------------
  //  -- r0    : receiver
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------
  // Registers r0 and r2 contain objects that needs to be pushed on the
  // expression stack of the fake JS frame.
  Generate_DebugBreakCallHelper(masm, r0.bit() | r2.bit());
}


void Builtins::Generate_KeyedLoadIC_DebugBreak(MacroAssembler* masm) {
  // Keyed load IC not implemented on ARM.
}


void Builtins::Generate_KeyedStoreIC_DebugBreak(MacroAssembler* masm) {
  // Keyed store IC ont implemented on ARM.
}


void Builtins::Generate_CallIC_DebugBreak(MacroAssembler* masm) {
  // Calling convention for IC call (from ic-arm.cc)
  // ----------- S t a t e -------------
  //  -- r0: number of arguments
  //  -- r1: receiver
  //  -- lr: return address
  // -----------------------------------
  // Register r1 contains an object that needs to be pushed on the expression
  // stack of the fake JS frame. r0 is the actual number of arguments not
  // encoded as a smi, therefore it cannot be on the expression stack of the
  // fake JS frame as it can easily be an invalid pointer (e.g. 1). r0 will be
  // pushed on the stack of the C frame and restored from there.
  Generate_DebugBreakCallHelper(masm, r1.bit());
}


void Builtins::Generate_ConstructCall_DebugBreak(MacroAssembler* masm) {
  // In places other than IC call sites it is expected that r0 is TOS which
  // is an object - this is not generally the case so this should be used with
  // care.
  Generate_DebugBreakCallHelper(masm, r0.bit());
}


void Builtins::Generate_Return_DebugBreak(MacroAssembler* masm) {
  // In places other than IC call sites it is expected that r0 is TOS which
  // is an object - this is not generally the case so this should be used with
  // care.
  Generate_DebugBreakCallHelper(masm, r0.bit());
}


void Builtins::Generate_Return_DebugBreakEntry(MacroAssembler* masm) {
  // Generate nothing as this handling of debug break return is not done this
  // way on ARM  - yet.
}

void Builtins::Generate_StubNoRegisters_DebugBreak(MacroAssembler* masm) {
  // Generate nothing as CodeStub CallFunction is not used on ARM.
}


#undef __

} }  // namespace v8::internal
