// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_IA32

#include "src/api-arguments-inl.h"
#include "src/assembler-inl.h"
#include "src/base/bits.h"
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

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

void JSEntryStub::Generate(MacroAssembler* masm) {
  Label invoke, handler_entry, exit;
  Label not_outermost_js, not_outermost_js_2;

  {  // NOLINT. Scope block confuses linter.
    NoRootArrayScope uninitialized_root_register(masm);

    // Set up frame.
    __ push(ebp);
    __ mov(ebp, esp);

    // Push marker in two places.
    StackFrame::Type marker = type();
    __ push(Immediate(StackFrame::TypeToMarker(marker)));  // marker
    ExternalReference context_address =
        ExternalReference::Create(IsolateAddressId::kContextAddress, isolate());
    __ push(Operand(context_address.address(),
                    RelocInfo::EXTERNAL_REFERENCE));  // context
    // Save callee-saved registers (C calling conventions).
    __ push(edi);
    __ push(esi);
    __ push(ebx);

    __ InitializeRootRegister();
  }

  // Save copies of the top frame descriptor on the stack.
  ExternalReference c_entry_fp =
      ExternalReference::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  __ push(__ ExternalReferenceAsOperand(c_entry_fp, edi));

  // If this is the outermost JS call, set js_entry_sp value.
  ExternalReference js_entry_sp =
      ExternalReference::Create(IsolateAddressId::kJSEntrySPAddress, isolate());
  __ cmp(__ ExternalReferenceAsOperand(js_entry_sp, edi), Immediate(0));
  __ j(not_equal, &not_outermost_js, Label::kNear);
  __ mov(__ ExternalReferenceAsOperand(js_entry_sp, edi), ebp);
  __ push(Immediate(StackFrame::OUTERMOST_JSENTRY_FRAME));
  __ jmp(&invoke, Label::kNear);
  __ bind(&not_outermost_js);
  __ push(Immediate(StackFrame::INNER_JSENTRY_FRAME));

  // Jump to a faked try block that does the invoke, with a faked catch
  // block that sets the pending exception.
  __ jmp(&invoke);
  __ bind(&handler_entry);
  handler_offset_ = handler_entry.pos();
  // Caught exception: Store result (exception) in the pending exception
  // field in the JSEnv and return a failure sentinel.
  ExternalReference pending_exception = ExternalReference::Create(
      IsolateAddressId::kPendingExceptionAddress, isolate());
  __ mov(__ ExternalReferenceAsOperand(pending_exception, edi), eax);
  __ mov(eax, Immediate(isolate()->factory()->exception()));
  __ jmp(&exit);

  // Invoke: Link this frame into the handler chain.
  __ bind(&invoke);
  __ PushStackHandler(edi);

  // Invoke the function by calling through JS entry trampoline builtin and
  // pop the faked function when we return. Notice that we cannot store a
  // reference to the trampoline code directly in this stub, because the
  // builtin stubs may not have been generated yet.
  __ Call(EntryTrampoline(), RelocInfo::CODE_TARGET);

  // Unlink this frame from the handler chain.
  __ PopStackHandler(edi);

  __ bind(&exit);

  // Check if the current stack frame is marked as the outermost JS frame.
  __ pop(edi);
  __ cmp(edi, Immediate(StackFrame::OUTERMOST_JSENTRY_FRAME));
  __ j(not_equal, &not_outermost_js_2);
  __ mov(__ ExternalReferenceAsOperand(js_entry_sp, edi), Immediate(0));
  __ bind(&not_outermost_js_2);

  // Restore the top frame descriptor from the stack.
  __ pop(__ ExternalReferenceAsOperand(c_entry_fp, edi));

  // Restore callee-saved registers (C calling conventions).
  __ pop(ebx);
  __ pop(esi);
  __ pop(edi);
  __ add(esp, Immediate(2 * kPointerSize));  // remove markers

  // Restore frame pointer and return.
  __ pop(ebp);
  __ ret(0);
}

// Generates an Operand for saving parameters after PrepareCallApiFunction.
static Operand ApiParameterOperand(int index) {
  return Operand(esp, index * kPointerSize);
}


// Prepares stack to put arguments (aligns and so on). Reserves
// space for return value if needed (assumes the return value is a handle).
// Arguments must be stored in ApiParameterOperand(0), ApiParameterOperand(1)
// etc. Saves context (esi). If space was reserved for return value then
// stores the pointer to the reserved slot into esi.
static void PrepareCallApiFunction(MacroAssembler* masm, int argc,
                                   Register scratch) {
  __ EnterApiExitFrame(argc, scratch);
  if (__ emit_debug_code()) {
    __ mov(esi, Immediate(bit_cast<int32_t>(kZapValue)));
  }
}

// Calls an API function.  Allocates HandleScope, extracts returned value
// from handle and propagates exceptions.  Clobbers esi, edi and
// caller-save registers.  Restores context.  On return removes
// stack_space * kPointerSize (GCed).
static void CallApiFunctionAndReturn(MacroAssembler* masm,
                                     Register function_address,
                                     ExternalReference thunk_ref,
                                     Operand thunk_last_arg, int stack_space,
                                     Operand* stack_space_operand,
                                     Operand return_value_operand) {
  Isolate* isolate = masm->isolate();

  ExternalReference next_address =
      ExternalReference::handle_scope_next_address(isolate);
  ExternalReference limit_address =
      ExternalReference::handle_scope_limit_address(isolate);
  ExternalReference level_address =
      ExternalReference::handle_scope_level_address(isolate);

  DCHECK(edx == function_address);
  // Allocate HandleScope in callee-save registers.
  __ add(__ ExternalReferenceAsOperand(level_address, esi), Immediate(1));
  __ mov(esi, __ ExternalReferenceAsOperand(next_address, esi));
  __ mov(edi, __ ExternalReferenceAsOperand(limit_address, edi));

  if (FLAG_log_timer_events) {
    FrameScope frame(masm, StackFrame::MANUAL);
    __ PushSafepointRegisters();
    __ PrepareCallCFunction(1, eax);
    __ Move(Operand(esp, 0),
            Immediate(ExternalReference::isolate_address(isolate)));
    __ CallCFunction(ExternalReference::log_enter_external_function(), 1);
    __ PopSafepointRegisters();
  }


  Label profiler_disabled;
  Label end_profiler_check;
  __ Move(eax, Immediate(ExternalReference::is_profiling_address(isolate)));
  __ cmpb(Operand(eax, 0), Immediate(0));
  __ j(zero, &profiler_disabled);

  // Additional parameter is the address of the actual getter function.
  __ mov(thunk_last_arg, function_address);
  // Call the api function.
  __ Move(eax, Immediate(thunk_ref));
  __ call(eax);
  __ jmp(&end_profiler_check);

  __ bind(&profiler_disabled);
  // Call the api function.
  __ call(function_address);
  __ bind(&end_profiler_check);

  if (FLAG_log_timer_events) {
    FrameScope frame(masm, StackFrame::MANUAL);
    __ PushSafepointRegisters();
    __ PrepareCallCFunction(1, eax);
    __ mov(eax, Immediate(ExternalReference::isolate_address(isolate)));
    __ mov(Operand(esp, 0), eax);
    __ CallCFunction(ExternalReference::log_leave_external_function(), 1);
    __ PopSafepointRegisters();
  }

  Label prologue;
  // Load the value from ReturnValue
  __ mov(eax, return_value_operand);

  Label promote_scheduled_exception;
  Label delete_allocated_handles;
  Label leave_exit_frame;

  __ bind(&prologue);
  // No more valid handles (the result handle was the last one). Restore
  // previous handle scope.
  __ mov(__ ExternalReferenceAsOperand(next_address, ecx), esi);
  __ sub(__ ExternalReferenceAsOperand(level_address, ecx), Immediate(1));
  __ Assert(above_equal, AbortReason::kInvalidHandleScopeLevel);
  __ cmp(edi, __ ExternalReferenceAsOperand(limit_address, ecx));
  __ j(not_equal, &delete_allocated_handles);

  // Leave the API exit frame.
  __ bind(&leave_exit_frame);
  if (stack_space_operand != nullptr) {
    __ mov(edx, *stack_space_operand);
  }
  __ LeaveApiExitFrame();

  // Check if the function scheduled an exception.
  ExternalReference scheduled_exception_address =
      ExternalReference::scheduled_exception_address(isolate);
  __ mov(ecx, __ ExternalReferenceAsOperand(scheduled_exception_address, ecx));
  __ CompareRoot(ecx, RootIndex::kTheHoleValue);
  __ j(not_equal, &promote_scheduled_exception);

#if DEBUG
  // Check if the function returned a valid JavaScript value.
  Label ok;
  Register return_value = eax;
  Register map = ecx;

  __ JumpIfSmi(return_value, &ok, Label::kNear);
  __ mov(map, FieldOperand(return_value, HeapObject::kMapOffset));

  __ CmpInstanceType(map, LAST_NAME_TYPE);
  __ j(below_equal, &ok, Label::kNear);

  __ CmpInstanceType(map, FIRST_JS_RECEIVER_TYPE);
  __ j(above_equal, &ok, Label::kNear);

  __ CompareRoot(map, RootIndex::kHeapNumberMap);
  __ j(equal, &ok, Label::kNear);

  __ CompareRoot(return_value, RootIndex::kUndefinedValue);
  __ j(equal, &ok, Label::kNear);

  __ CompareRoot(return_value, RootIndex::kTrueValue);
  __ j(equal, &ok, Label::kNear);

  __ CompareRoot(return_value, RootIndex::kFalseValue);
  __ j(equal, &ok, Label::kNear);

  __ CompareRoot(return_value, RootIndex::kNullValue);
  __ j(equal, &ok, Label::kNear);

  __ Abort(AbortReason::kAPICallReturnedInvalidObject);

  __ bind(&ok);
#endif

  if (stack_space_operand != nullptr) {
    DCHECK_EQ(0, stack_space);
    __ pop(ecx);
    __ add(esp, edx);
    __ jmp(ecx);
  } else {
    __ ret(stack_space * kPointerSize);
  }

  // Re-throw by promoting a scheduled exception.
  __ bind(&promote_scheduled_exception);
  __ TailCallRuntime(Runtime::kPromoteScheduledException);

  // HandleScope limit has changed. Delete allocated extensions.
  ExternalReference delete_extensions =
      ExternalReference::delete_handle_scope_extensions();
  __ bind(&delete_allocated_handles);
  __ mov(__ ExternalReferenceAsOperand(limit_address, ecx), edi);
  __ mov(edi, eax);
  __ Move(eax, Immediate(ExternalReference::isolate_address(isolate)));
  __ mov(Operand(esp, 0), eax);
  __ Move(eax, Immediate(delete_extensions));
  __ call(eax);
  __ mov(eax, edi);
  __ jmp(&leave_exit_frame);
}

void CallApiCallbackStub::Generate(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- esi                 : kTargetContext
  //  -- edx                 : kApiFunctionAddress
  //  -- ecx                 : kArgc
  //  --
  //  -- esp[0]              : return address
  //  -- esp[4]              : last argument
  //  -- ...
  //  -- esp[argc * 4]       : first argument
  //  -- esp[(argc + 1) * 4] : receiver
  //  -- esp[(argc + 2) * 4] : kHolder
  //  -- esp[(argc + 3) * 4] : kCallData
  // -----------------------------------

  Register api_function_address = edx;
  Register argc = ecx;
  Register scratch = eax;

  DCHECK(!AreAliased(api_function_address, argc, scratch));

  // Stack offsets (without argc).
  static constexpr int kReceiverOffset = kPointerSize;
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
  // Current state:
  //   esp[0]: return address
  //
  // Target state:
  //   esp[0 * kPointerSize]: return address
  //   esp[1 * kPointerSize]: kHolder
  //   esp[2 * kPointerSize]: kIsolate
  //   esp[3 * kPointerSize]: undefined (kReturnValueDefaultValue)
  //   esp[4 * kPointerSize]: undefined (kReturnValue)
  //   esp[5 * kPointerSize]: kData
  //   esp[6 * kPointerSize]: undefined (kNewTarget)

  // Reserve space on the stack.
  __ sub(esp, Immediate(FCA::kArgsLength * kPointerSize));

  // Return address (the old stack location is overwritten later on).
  __ mov(scratch, Operand(esp, FCA::kArgsLength * kPointerSize));
  __ mov(Operand(esp, 0 * kPointerSize), scratch);

  // kHolder.
  __ mov(scratch, Operand(esp, argc, times_pointer_size,
                          FCA::kArgsLength * kPointerSize + kHolderOffset));
  __ mov(Operand(esp, 1 * kPointerSize), scratch);

  // kIsolate.
  __ Move(scratch,
          Immediate(ExternalReference::isolate_address(masm->isolate())));
  __ mov(Operand(esp, 2 * kPointerSize), scratch);

  // kReturnValueDefaultValue, kReturnValue, and kNewTarget.
  __ LoadRoot(scratch, RootIndex::kUndefinedValue);
  __ mov(Operand(esp, 3 * kPointerSize), scratch);
  __ mov(Operand(esp, 4 * kPointerSize), scratch);
  __ mov(Operand(esp, 6 * kPointerSize), scratch);

  // kData.
  __ mov(scratch, Operand(esp, argc, times_pointer_size,
                          FCA::kArgsLength * kPointerSize + kCallDataOffset));
  __ mov(Operand(esp, 5 * kPointerSize), scratch);

  // Keep a pointer to kHolder (= implicit_args) in a scratch register.
  // We use it below to set up the FunctionCallbackInfo object.
  __ lea(scratch, Operand(esp, 1 * kPointerSize));

  // The API function takes a reference to v8::Arguments. If the CPU profiler
  // is enabled, a wrapper function will be called and we need to pass
  // the address of the callback as an additional parameter. Always allocate
  // space for it.
  static constexpr int kApiArgc = 1 + 1;

  // Allocate the v8::Arguments structure in the arguments' space since
  // it's not controlled by GC.
  static constexpr int kApiStackSpace = 4;

  PrepareCallApiFunction(masm, kApiArgc + kApiStackSpace, edi);

  // FunctionCallbackInfo::implicit_args_ (points at kHolder as set up above).
  __ mov(ApiParameterOperand(kApiArgc + 0), scratch);

  // FunctionCallbackInfo::values_ (points at the first varargs argument passed
  // on the stack).
  __ lea(scratch, Operand(scratch, argc, times_pointer_size,
                          (FCA::kArgsLength - 1) * kPointerSize));
  __ mov(ApiParameterOperand(kApiArgc + 1), scratch);

  // FunctionCallbackInfo::length_.
  __ mov(ApiParameterOperand(kApiArgc + 2), argc);

  // We also store the number of bytes to drop from the stack after returning
  // from the API function here.
  __ lea(scratch,
         Operand(argc, times_pointer_size,
                 (FCA::kArgsLength + kExtraStackArgumentCount) * kPointerSize));
  __ mov(ApiParameterOperand(kApiArgc + 3), scratch);

  // v8::InvocationCallback's argument.
  __ lea(scratch, ApiParameterOperand(kApiArgc + 0));
  __ mov(ApiParameterOperand(0), scratch);

  ExternalReference thunk_ref = ExternalReference::invoke_function_callback();

  // There are two stack slots above the arguments we constructed on the stack:
  // the stored ebp (pushed by EnterApiExitFrame), and the return address.
  static constexpr int kStackSlotsAboveFCA = 2;
  Operand return_value_operand(
      ebp, (kStackSlotsAboveFCA + FCA::kReturnValueOffset) * kPointerSize);

  static constexpr int kUseStackSpaceOperand = 0;
  Operand stack_space_operand = ApiParameterOperand(kApiArgc + 3);
  CallApiFunctionAndReturn(masm, api_function_address, thunk_ref,
                           ApiParameterOperand(1), kUseStackSpaceOperand,
                           &stack_space_operand, return_value_operand);
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
  Register scratch = edi;
  DCHECK(!AreAliased(receiver, holder, callback, scratch));

  __ pop(scratch);  // Pop return address to extend the frame.
  __ push(receiver);
  __ push(FieldOperand(callback, AccessorInfo::kDataOffset));
  __ PushRoot(RootIndex::kUndefinedValue);  // ReturnValue
  // ReturnValue default value
  __ PushRoot(RootIndex::kUndefinedValue);
  __ Push(Immediate(ExternalReference::isolate_address(isolate())));
  __ push(holder);
  __ push(Immediate(Smi::zero()));  // should_throw_on_error -> false
  __ push(FieldOperand(callback, AccessorInfo::kNameOffset));
  __ push(scratch);  // Restore return address.

  // v8::PropertyCallbackInfo::args_ array and name handle.
  const int kStackUnwindSpace = PropertyCallbackArguments::kArgsLength + 1;

  // Allocate v8::PropertyCallbackInfo object, arguments for callback and
  // space for optional callback address parameter (in case CPU profiler is
  // active) in non-GCed stack space.
  const int kApiArgc = 3 + 1;

  PrepareCallApiFunction(masm, kApiArgc, scratch);

  // Load address of v8::PropertyAccessorInfo::args_ array. The value in ebp
  // here corresponds to esp + kPointersize before PrepareCallApiFunction.
  __ lea(scratch, Operand(ebp, kPointerSize + 2 * kPointerSize));
  // Create v8::PropertyCallbackInfo object on the stack and initialize
  // it's args_ field.
  Operand info_object = ApiParameterOperand(3);
  __ mov(info_object, scratch);

  // Name as handle.
  __ sub(scratch, Immediate(kPointerSize));
  __ mov(ApiParameterOperand(0), scratch);
  // Arguments pointer.
  __ lea(scratch, info_object);
  __ mov(ApiParameterOperand(1), scratch);
  // Reserve space for optional callback address parameter.
  Operand thunk_last_arg = ApiParameterOperand(2);

  ExternalReference thunk_ref =
      ExternalReference::invoke_accessor_getter_callback();

  __ mov(scratch, FieldOperand(callback, AccessorInfo::kJsGetterOffset));
  Register function_address = edx;
  __ mov(function_address,
         FieldOperand(scratch, Foreign::kForeignAddressOffset));
  // +3 is to skip prolog, return address and name handle.
  Operand return_value_operand(
      ebp, (PropertyCallbackArguments::kReturnValueOffset + 3) * kPointerSize);
  Operand* const kUseStackSpaceConstant = nullptr;
  CallApiFunctionAndReturn(masm, function_address, thunk_ref, thunk_last_arg,
                           kStackUnwindSpace, kUseStackSpaceConstant,
                           return_value_operand);
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_IA32
