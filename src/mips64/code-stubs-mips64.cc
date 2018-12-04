// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_MIPS64

#include "src/api-arguments.h"
#include "src/bootstrapper.h"
#include "src/code-stubs.h"
#include "src/frame-constants.h"
#include "src/frames.h"
#include "src/ic/ic.h"
#include "src/ic/stub-cache.h"
#include "src/isolate.h"
#include "src/macro-assembler.h"
#include "src/objects/api-callbacks.h"
#include "src/regexp/jsregexp.h"
#include "src/regexp/regexp-macro-assembler.h"
#include "src/runtime/runtime.h"

#include "src/mips64/code-stubs-mips64.h"  // Cannot be the first include.

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

void JSEntryStub::Generate(MacroAssembler* masm) {
  Label invoke, handler_entry, exit;
  Isolate* isolate = masm->isolate();

  {
    NoRootArrayScope no_root_array(masm);

    // TODO(plind): unify the ABI description here.
    // Registers:
    // a0: entry address
    // a1: function
    // a2: receiver
    // a3: argc
    // a4 (a4): on mips64

    // Stack:
    // 0 arg slots on mips64 (4 args slots on mips)
    // args -- in a4/a4 on mips64, on stack on mips

    // Save callee saved registers on the stack.
    __ MultiPush(kCalleeSaved | ra.bit());

    // Save callee-saved FPU registers.
    __ MultiPushFPU(kCalleeSavedFPU);
    // Set up the reserved register for 0.0.
    __ Move(kDoubleRegZero, 0.0);

    // Load argv in s0 register.
    __ mov(s0, a4);  // 5th parameter in mips64 a4 (a4) register.

    __ InitializeRootRegister();
  }

  // We build an EntryFrame.
  __ li(a7, Operand(-1));  // Push a bad frame pointer to fail if it is used.
  StackFrame::Type marker = type();
  __ li(a6, Operand(StackFrame::TypeToMarker(marker)));
  __ li(a5, Operand(StackFrame::TypeToMarker(marker)));
  ExternalReference c_entry_fp =
      ExternalReference::Create(IsolateAddressId::kCEntryFPAddress, isolate);
  __ li(a4, Operand(c_entry_fp));
  __ Ld(a4, MemOperand(a4));
  __ Push(a7, a6, a5, a4);
  // Set up frame pointer for the frame to be pushed.
  __ daddiu(fp, sp, -EntryFrameConstants::kCallerFPOffset);

  // Registers:
  // a0: entry_address
  // a1: function
  // a2: receiver_pointer
  // a3: argc
  // s0: argv
  //
  // Stack:
  // caller fp          |
  // function slot      | entry frame
  // context slot       |
  // bad fp (0xFF...F)  |
  // callee saved registers + ra
  // [ O32: 4 args slots]
  // args

  // If this is the outermost JS call, set js_entry_sp value.
  Label non_outermost_js;
  ExternalReference js_entry_sp =
      ExternalReference::Create(IsolateAddressId::kJSEntrySPAddress, isolate);
  __ li(a5, js_entry_sp);
  __ Ld(a6, MemOperand(a5));
  __ Branch(&non_outermost_js, ne, a6, Operand(zero_reg));
  __ Sd(fp, MemOperand(a5));
  __ li(a4, Operand(StackFrame::OUTERMOST_JSENTRY_FRAME));
  Label cont;
  __ b(&cont);
  __ nop();   // Branch delay slot nop.
  __ bind(&non_outermost_js);
  __ li(a4, Operand(StackFrame::INNER_JSENTRY_FRAME));
  __ bind(&cont);
  __ push(a4);

  // Jump to a faked try block that does the invoke, with a faked catch
  // block that sets the pending exception.
  __ jmp(&invoke);
  __ bind(&handler_entry);
  handler_offset_ = handler_entry.pos();
  // Caught exception: Store result (exception) in the pending exception
  // field in the JSEnv and return a failure sentinel.  Coming in here the
  // fp will be invalid because the PushStackHandler below sets it to 0 to
  // signal the existence of the JSEntry frame.
  __ li(a4, ExternalReference::Create(
                IsolateAddressId::kPendingExceptionAddress, isolate));
  __ Sd(v0, MemOperand(a4));  // We come back from 'invoke'. result is in v0.
  __ LoadRoot(v0, RootIndex::kException);
  __ b(&exit);  // b exposes branch delay slot.
  __ nop();   // Branch delay slot nop.

  // Invoke: Link this frame into the handler chain.
  __ bind(&invoke);
  __ PushStackHandler();
  // If an exception not caught by another handler occurs, this handler
  // returns control to the code after the bal(&invoke) above, which
  // restores all kCalleeSaved registers (including cp and fp) to their
  // saved values before returning a failure to C.

  // Invoke the function by calling through JS entry trampoline builtin.
  // Notice that we cannot store a reference to the trampoline code directly in
  // this stub, because runtime stubs are not traversed when doing GC.

  // Registers:
  // a0: entry_address
  // a1: function
  // a2: receiver_pointer
  // a3: argc
  // s0: argv
  //
  // Stack:
  // handler frame
  // entry frame
  // callee saved registers + ra
  // [ O32: 4 args slots]
  // args
  __ Call(EntryTrampoline(), RelocInfo::CODE_TARGET);

  // Unlink this frame from the handler chain.
  __ PopStackHandler();

  __ bind(&exit);  // v0 holds result
  // Check if the current stack frame is marked as the outermost JS frame.
  Label non_outermost_js_2;
  __ pop(a5);
  __ Branch(&non_outermost_js_2, ne, a5,
            Operand(StackFrame::OUTERMOST_JSENTRY_FRAME));
  __ li(a5, ExternalReference(js_entry_sp));
  __ Sd(zero_reg, MemOperand(a5));
  __ bind(&non_outermost_js_2);

  // Restore the top frame descriptors from the stack.
  __ pop(a5);
  __ li(a4,
        ExternalReference::Create(IsolateAddressId::kCEntryFPAddress, isolate));
  __ Sd(a5, MemOperand(a4));

  // Reset the stack to the callee saved registers.
  __ daddiu(sp, sp, -EntryFrameConstants::kCallerFPOffset);

  // Restore callee-saved fpu registers.
  __ MultiPopFPU(kCalleeSavedFPU);

  // Restore callee saved registers from the stack.
  __ MultiPop(kCalleeSaved | ra.bit());
  // Return.
  __ Jump(ra);
}

void DirectCEntryStub::Generate(MacroAssembler* masm) {
  // Make place for arguments to fit C calling convention. Most of the callers
  // of DirectCEntryStub::GenerateCall are using EnterExitFrame/LeaveExitFrame
  // so they handle stack restoring and we don't have to do that here.
  // Any caller of DirectCEntryStub::GenerateCall must take care of dropping
  // kCArgsSlotsSize stack space after the call.
  __ daddiu(sp, sp, -kCArgsSlotsSize);
  // Place the return address on the stack, making the call
  // GC safe. The RegExp backend also relies on this.
  __ Sd(ra, MemOperand(sp, kCArgsSlotsSize));
  __ Call(t9);  // Call the C++ function.
  __ Ld(t9, MemOperand(sp, kCArgsSlotsSize));

  if (FLAG_debug_code && FLAG_enable_slow_asserts) {
    // In case of an error the return address may point to a memory area
    // filled with kZapValue by the GC.
    // Dereference the address and check for this.
    __ Uld(a4, MemOperand(t9));
    __ Assert(ne, AbortReason::kReceivedInvalidReturnAddress, a4,
              Operand(reinterpret_cast<uint64_t>(kZapValue)));
  }
  __ Jump(t9);
}


void DirectCEntryStub::GenerateCall(MacroAssembler* masm,
                                    Register target) {
  if (FLAG_embedded_builtins) {
    if (masm->root_array_available() &&
        isolate()->ShouldLoadConstantsFromRootList()) {
      // This is basically an inlined version of Call(Handle<Code>) that loads
      // the code object into kScratchReg instead of t9.
      __ Move(t9, target);
      __ IndirectLoadConstant(kScratchReg, GetCode());
      __ Daddu(kScratchReg, kScratchReg,
               Operand(Code::kHeaderSize - kHeapObjectTag));
      __ Call(kScratchReg);
      return;
    }
  }
  intptr_t loc =
      reinterpret_cast<intptr_t>(GetCode().location());
  __ Move(t9, target);
  __ li(kScratchReg, Operand(loc, RelocInfo::CODE_TARGET), CONSTANT_SIZE);
  __ Call(kScratchReg);
}

static int AddressOffset(ExternalReference ref0, ExternalReference ref1) {
  int64_t offset = (ref0.address() - ref1.address());
  DCHECK(static_cast<int>(offset) == offset);
  return static_cast<int>(offset);
}


// Calls an API function.  Allocates HandleScope, extracts returned value
// from handle and propagates exceptions.  Restores context.  stack_space
// - space to be unwound on exit (includes the call JS arguments space and
// the additional space allocated for the fast call).
static void CallApiFunctionAndReturn(MacroAssembler* masm,
                                     Register function_address,
                                     ExternalReference thunk_ref,
                                     int stack_space,
                                     MemOperand* stack_space_operand,
                                     MemOperand return_value_operand) {
  Isolate* isolate = masm->isolate();
  ExternalReference next_address =
      ExternalReference::handle_scope_next_address(isolate);
  const int kNextOffset = 0;
  const int kLimitOffset = AddressOffset(
      ExternalReference::handle_scope_limit_address(isolate), next_address);
  const int kLevelOffset = AddressOffset(
      ExternalReference::handle_scope_level_address(isolate), next_address);

  DCHECK(function_address == a1 || function_address == a2);

  Label profiler_disabled;
  Label end_profiler_check;
  __ li(t9, ExternalReference::is_profiling_address(isolate));
  __ Lb(t9, MemOperand(t9, 0));
  __ Branch(&profiler_disabled, eq, t9, Operand(zero_reg));

  // Additional parameter is the address of the actual callback.
  __ li(t9, thunk_ref);
  __ jmp(&end_profiler_check);

  __ bind(&profiler_disabled);
  __ mov(t9, function_address);
  __ bind(&end_profiler_check);

  // Allocate HandleScope in callee-save registers.
  __ li(s5, next_address);
  __ Ld(s0, MemOperand(s5, kNextOffset));
  __ Ld(s1, MemOperand(s5, kLimitOffset));
  __ Lw(s2, MemOperand(s5, kLevelOffset));
  __ Addu(s2, s2, Operand(1));
  __ Sw(s2, MemOperand(s5, kLevelOffset));

  if (FLAG_log_timer_events) {
    FrameScope frame(masm, StackFrame::MANUAL);
    __ PushSafepointRegisters();
    __ PrepareCallCFunction(1, a0);
    __ li(a0, ExternalReference::isolate_address(isolate));
    __ CallCFunction(ExternalReference::log_enter_external_function(), 1);
    __ PopSafepointRegisters();
  }

  // Native call returns to the DirectCEntry stub which redirects to the
  // return address pushed on stack (could have moved after GC).
  // DirectCEntry stub itself is generated early and never moves.
  DirectCEntryStub stub(isolate);
  stub.GenerateCall(masm, t9);

  if (FLAG_log_timer_events) {
    FrameScope frame(masm, StackFrame::MANUAL);
    __ PushSafepointRegisters();
    __ PrepareCallCFunction(1, a0);
    __ li(a0, ExternalReference::isolate_address(isolate));
    __ CallCFunction(ExternalReference::log_leave_external_function(), 1);
    __ PopSafepointRegisters();
  }

  Label promote_scheduled_exception;
  Label delete_allocated_handles;
  Label leave_exit_frame;
  Label return_value_loaded;

  // Load value from ReturnValue.
  __ Ld(v0, return_value_operand);
  __ bind(&return_value_loaded);

  // No more valid handles (the result handle was the last one). Restore
  // previous handle scope.
  __ Sd(s0, MemOperand(s5, kNextOffset));
  if (__ emit_debug_code()) {
    __ Lw(a1, MemOperand(s5, kLevelOffset));
    __ Check(eq, AbortReason::kUnexpectedLevelAfterReturnFromApiCall, a1,
             Operand(s2));
  }
  __ Subu(s2, s2, Operand(1));
  __ Sw(s2, MemOperand(s5, kLevelOffset));
  __ Ld(kScratchReg, MemOperand(s5, kLimitOffset));
  __ Branch(&delete_allocated_handles, ne, s1, Operand(kScratchReg));

  // Leave the API exit frame.
  __ bind(&leave_exit_frame);

  if (stack_space_operand == nullptr) {
    CHECK_NE(stack_space, 0);
    __ li(s0, Operand(stack_space));
  } else {
    CHECK_EQ(stack_space, 0);
    STATIC_ASSERT(kCArgSlotCount == 0);
    __ Ld(s0, *stack_space_operand);
  }

  static constexpr bool kDontSaveDoubles = false;
  static constexpr bool kRegisterContainsSlotCount = false;
  __ LeaveExitFrame(kDontSaveDoubles, s0, NO_EMIT_RETURN,
                    kRegisterContainsSlotCount);

  // Check if the function scheduled an exception.
  __ LoadRoot(a4, RootIndex::kTheHoleValue);
  __ li(kScratchReg, ExternalReference::scheduled_exception_address(isolate));
  __ Ld(a5, MemOperand(kScratchReg));
  __ Branch(&promote_scheduled_exception, ne, a4, Operand(a5));

  __ Ret();

  // Re-throw by promoting a scheduled exception.
  __ bind(&promote_scheduled_exception);
  __ TailCallRuntime(Runtime::kPromoteScheduledException);

  // HandleScope limit has changed. Delete allocated extensions.
  __ bind(&delete_allocated_handles);
  __ Sd(s1, MemOperand(s5, kLimitOffset));
  __ mov(s0, v0);
  __ mov(a0, v0);
  __ PrepareCallCFunction(1, s1);
  __ li(a0, ExternalReference::isolate_address(isolate));
  __ CallCFunction(ExternalReference::delete_handle_scope_extensions(), 1);
  __ mov(v0, s0);
  __ jmp(&leave_exit_frame);
}

void CallApiCallbackStub::Generate(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- cp                  : kTargetContext
  //  -- a1                  : kApiFunctionAddress
  //  -- a2                  : kArgc
  //  --
  //  -- sp[0]               : last argument
  //  -- ...
  //  -- sp[(argc - 1) * 8]  : first argument
  //  -- sp[(argc + 0) * 8]  : receiver
  //  -- sp[(argc + 1) * 8]  : kHolder
  //  -- sp[(argc + 2) * 8]  : kCallData
  // -----------------------------------

  Register api_function_address = a1;
  Register argc = a2;
  Register scratch = t0;
  Register base = t1;  // For addressing MemOperands on the stack.

  DCHECK(!AreAliased(api_function_address, argc, scratch, base));

  // Stack offsets (without argc).
  static constexpr int kReceiverOffset = 0 * kPointerSize;
  static constexpr int kHolderOffset = kReceiverOffset + kPointerSize;
  static constexpr int kCallDataOffset = kHolderOffset + kPointerSize;

  // Extra stack arguments are: the receiver, kHolder, kCallData.
  static constexpr int kExtraStackArgumentCount = 3;

  typedef FunctionCallbackArguments FCA;

  STATIC_ASSERT(FCA::kArgsLength == 6);
  STATIC_ASSERT(FCA::kNewTargetIndex == 5);
  STATIC_ASSERT(FCA::kDataIndex == 4);
  STATIC_ASSERT(FCA::kReturnValueOffset == 3);
  STATIC_ASSERT(FCA::kReturnValueDefaultValueIndex == 2);
  STATIC_ASSERT(FCA::kIsolateIndex == 1);
  STATIC_ASSERT(FCA::kHolderIndex == 0);

  // Set up FunctionCallbackInfo's implicit_args on the stack as follows:
  //
  // Target state:
  //   sp[0 * kPointerSize]: kHolder
  //   sp[1 * kPointerSize]: kIsolate
  //   sp[2 * kPointerSize]: undefined (kReturnValueDefaultValue)
  //   sp[3 * kPointerSize]: undefined (kReturnValue)
  //   sp[4 * kPointerSize]: kData
  //   sp[5 * kPointerSize]: undefined (kNewTarget)

  // Set up the base register for addressing through MemOperands. It will point
  // at the receiver (located at sp + argc * kPointerSize).
  __ Dlsa(base, sp, argc, kPointerSizeLog2);

  // Reserve space on the stack.
  __ Dsubu(sp, sp, Operand(FCA::kArgsLength * kPointerSize));

  // kHolder.
  __ Ld(scratch, MemOperand(base, kHolderOffset));
  __ Sd(scratch, MemOperand(sp, 0 * kPointerSize));

  // kIsolate.
  __ li(scratch, ExternalReference::isolate_address(masm->isolate()));
  __ Sd(scratch, MemOperand(sp, 1 * kPointerSize));

  // kReturnValueDefaultValue, kReturnValue, and kNewTarget.
  __ LoadRoot(scratch, RootIndex::kUndefinedValue);
  __ Sd(scratch, MemOperand(sp, 2 * kPointerSize));
  __ Sd(scratch, MemOperand(sp, 3 * kPointerSize));
  __ Sd(scratch, MemOperand(sp, 5 * kPointerSize));

  // kData.
  __ Ld(scratch, MemOperand(base, kCallDataOffset));
  __ Sd(scratch, MemOperand(sp, 4 * kPointerSize));

  // Keep a pointer to kHolder (= implicit_args) in a scratch register.
  // We use it below to set up the FunctionCallbackInfo object.
  __ mov(scratch, sp);

  // Allocate the v8::Arguments structure in the arguments' space since
  // it's not controlled by GC.
  static constexpr int kApiStackSpace = 4;
  static constexpr bool kDontSaveDoubles = false;
  FrameScope frame_scope(masm, StackFrame::MANUAL);
  __ EnterExitFrame(kDontSaveDoubles, kApiStackSpace);

  // EnterExitFrame may align the sp.

  // FunctionCallbackInfo::implicit_args_ (points at kHolder as set up above).
  // Arguments are after the return address (pushed by EnterExitFrame()).
  __ Sd(scratch, MemOperand(sp, 1 * kPointerSize));

  // FunctionCallbackInfo::values_ (points at the first varargs argument passed
  // on the stack).
  __ Dsubu(scratch, base, Operand(1 * kPointerSize));
  __ Sd(scratch, MemOperand(sp, 2 * kPointerSize));

  // FunctionCallbackInfo::length_.
  // Stored as int field, 32-bit integers within struct on stack always left
  // justified by n64 ABI.
  __ Sw(argc, MemOperand(sp, 3 * kPointerSize));

  // We also store the number of bytes to drop from the stack after returning
  // from the API function here.
  // Note: Unlike on other architectures, this stores the number of slots to
  // drop, not the number of bytes.
  __ Daddu(scratch, argc, Operand(FCA::kArgsLength + kExtraStackArgumentCount));
  __ Sd(scratch, MemOperand(sp, 4 * kPointerSize));

  // v8::InvocationCallback's argument.
  DCHECK(!AreAliased(api_function_address, scratch, a0));
  __ Daddu(a0, sp, Operand(1 * kPointerSize));

  ExternalReference thunk_ref = ExternalReference::invoke_function_callback();

  // There are two stack slots above the arguments we constructed on the stack.
  // TODO(jgruber): Document what these arguments are.
  static constexpr int kStackSlotsAboveFCA = 2;
  MemOperand return_value_operand(
      fp, (kStackSlotsAboveFCA + FCA::kReturnValueOffset) * kPointerSize);

  static constexpr int kUseStackSpaceOperand = 0;
  MemOperand stack_space_operand(sp, 4 * kPointerSize);

  AllowExternalCallThatCantCauseGC scope(masm);
  CallApiFunctionAndReturn(masm, api_function_address, thunk_ref,
                           kUseStackSpaceOperand, &stack_space_operand,
                           return_value_operand);
}


void CallApiGetterStub::Generate(MacroAssembler* masm) {
  // Build v8::PropertyCallbackInfo::args_ array on the stack and push property
  // name below the exit frame to make GC aware of them.
  STATIC_ASSERT(PropertyCallbackArguments::kShouldThrowOnErrorIndex == 0);
  STATIC_ASSERT(PropertyCallbackArguments::kHolderIndex == 1);
  STATIC_ASSERT(PropertyCallbackArguments::kIsolateIndex == 2);
  STATIC_ASSERT(PropertyCallbackArguments::kReturnValueDefaultValueIndex == 3);
  STATIC_ASSERT(PropertyCallbackArguments::kReturnValueOffset == 4);
  STATIC_ASSERT(PropertyCallbackArguments::kDataIndex == 5);
  STATIC_ASSERT(PropertyCallbackArguments::kThisIndex == 6);
  STATIC_ASSERT(PropertyCallbackArguments::kArgsLength == 7);

  Register receiver = ApiGetterDescriptor::ReceiverRegister();
  Register holder = ApiGetterDescriptor::HolderRegister();
  Register callback = ApiGetterDescriptor::CallbackRegister();
  Register scratch = a4;
  DCHECK(!AreAliased(receiver, holder, callback, scratch));

  Register api_function_address = a2;

  // Here and below +1 is for name() pushed after the args_ array.
  typedef PropertyCallbackArguments PCA;
  __ Dsubu(sp, sp, (PCA::kArgsLength + 1) * kPointerSize);
  __ Sd(receiver, MemOperand(sp, (PCA::kThisIndex + 1) * kPointerSize));
  __ Ld(scratch, FieldMemOperand(callback, AccessorInfo::kDataOffset));
  __ Sd(scratch, MemOperand(sp, (PCA::kDataIndex + 1) * kPointerSize));
  __ LoadRoot(scratch, RootIndex::kUndefinedValue);
  __ Sd(scratch, MemOperand(sp, (PCA::kReturnValueOffset + 1) * kPointerSize));
  __ Sd(scratch, MemOperand(sp, (PCA::kReturnValueDefaultValueIndex + 1) *
                                    kPointerSize));
  __ li(scratch, ExternalReference::isolate_address(isolate()));
  __ Sd(scratch, MemOperand(sp, (PCA::kIsolateIndex + 1) * kPointerSize));
  __ Sd(holder, MemOperand(sp, (PCA::kHolderIndex + 1) * kPointerSize));
  // should_throw_on_error -> false
  DCHECK_NULL(Smi::kZero);
  __ Sd(zero_reg,
        MemOperand(sp, (PCA::kShouldThrowOnErrorIndex + 1) * kPointerSize));
  __ Ld(scratch, FieldMemOperand(callback, AccessorInfo::kNameOffset));
  __ Sd(scratch, MemOperand(sp, 0 * kPointerSize));

  // v8::PropertyCallbackInfo::args_ array and name handle.
  const int kStackUnwindSpace = PropertyCallbackArguments::kArgsLength + 1;

  // Load address of v8::PropertyAccessorInfo::args_ array and name handle.
  __ mov(a0, sp);                               // a0 = Handle<Name>
  __ Daddu(a1, a0, Operand(1 * kPointerSize));  // a1 = v8::PCI::args_

  const int kApiStackSpace = 1;
  FrameScope frame_scope(masm, StackFrame::MANUAL);
  __ EnterExitFrame(false, kApiStackSpace);

  // Create v8::PropertyCallbackInfo object on the stack and initialize
  // it's args_ field.
  __ Sd(a1, MemOperand(sp, 1 * kPointerSize));
  __ Daddu(a1, sp, Operand(1 * kPointerSize));
  // a1 = v8::PropertyCallbackInfo&

  ExternalReference thunk_ref =
      ExternalReference::invoke_accessor_getter_callback();

  __ Ld(scratch, FieldMemOperand(callback, AccessorInfo::kJsGetterOffset));
  __ Ld(api_function_address,
        FieldMemOperand(scratch, Foreign::kForeignAddressOffset));

  // +3 is to skip prolog, return address and name handle.
  MemOperand return_value_operand(
      fp, (PropertyCallbackArguments::kReturnValueOffset + 3) * kPointerSize);
  MemOperand* const kUseStackSpaceConstant = nullptr;
  CallApiFunctionAndReturn(masm, api_function_address, thunk_ref,
                           kStackUnwindSpace, kUseStackSpaceConstant,
                           return_value_operand);
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_MIPS64
