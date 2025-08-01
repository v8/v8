// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_X64

#include "src/api/api-arguments.h"
#include "src/base/bits-iterator.h"
#include "src/base/iterator.h"
#include "src/builtins/builtins-descriptors.h"
#include "src/builtins/builtins-inl.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/interface-descriptors-inl.h"
// For interpreter_entry_return_pc_offset. TODO(jkummerow): Drop.
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/register-configuration.h"
#include "src/codegen/x64/assembler-x64.h"
#include "src/common/globals.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/frame-constants.h"
#include "src/execution/frames.h"
#include "src/heap/heap-inl.h"
#include "src/logging/counters.h"
#include "src/objects/cell.h"
#include "src/objects/code.h"
#include "src/objects/debug-objects.h"
#include "src/objects/foreign.h"
#include "src/objects/heap-number.h"
#include "src/objects/js-generator.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "src/sandbox/indirect-pointer-tag.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/baseline/liftoff-assembler-defs.h"
#include "src/wasm/object-access.h"
#include "src/wasm/stacks.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

void Builtins::Generate_Adaptor(MacroAssembler* masm,
                                int formal_parameter_count, Address address) {
  __ CodeEntry();

  __ LoadAddress(kJavaScriptCallExtraArg1Register,
                 ExternalReference::Create(address));
  __ TailCallBuiltin(
      Builtins::AdaptorWithBuiltinExitFrame(formal_parameter_count));
}

namespace {

constexpr int kReceiverOnStackSize = kSystemPointerSize;

enum class ArgumentsElementType {
  kRaw,    // Push arguments as they are.
  kHandle  // Dereference arguments before pushing.
};

void Generate_PushArguments(MacroAssembler* masm, Register array, Register argc,
                            Register scratch,
                            ArgumentsElementType element_type) {
  DCHECK(!AreAliased(array, argc, scratch, kScratchRegister));
  Register counter = scratch;
  Label loop, entry;
  __ leaq(counter, Operand(argc, -kJSArgcReceiverSlots));
  __ jmp(&entry);
  __ bind(&loop);
  Operand value(array, counter, times_system_pointer_size, 0);
  if (element_type == ArgumentsElementType::kHandle) {
    __ movq(kScratchRegister, value);
    value = Operand(kScratchRegister, 0);
  }
  __ Push(value);
  __ bind(&entry);
  __ decq(counter);
  __ j(greater_equal, &loop, Label::kNear);
}

void Generate_JSBuiltinsConstructStubHelper(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax: number of arguments
  //  -- rdi: constructor function
  //  -- rdx: new target
  //  -- rsi: context
  // -----------------------------------

  Label stack_overflow;
  __ StackOverflowCheck(rax, &stack_overflow, Label::kFar);

  // Enter a construct frame.
  {
    FrameScope scope(masm, StackFrame::CONSTRUCT);

    // Preserve the incoming parameters on the stack.
    __ Push(rsi);
    __ Push(rax);

    // TODO(victorgomes): When the arguments adaptor is completely removed, we
    // should get the formal parameter count and copy the arguments in its
    // correct position (including any undefined), instead of delaying this to
    // InvokeFunction.

    // Set up pointer to first argument (skip receiver).
    __ leaq(rbx, Operand(rbp, StandardFrameConstants::kFixedFrameSizeAboveFp +
                                  kSystemPointerSize));
    // Copy arguments to the expression stack.
    // rbx: Pointer to start of arguments.
    // rax: Number of arguments.
    Generate_PushArguments(masm, rbx, rax, rcx, ArgumentsElementType::kRaw);
    // The receiver for the builtin/api call.
    __ PushRoot(RootIndex::kTheHoleValue);

    // Call the function.
    // rax: number of arguments (untagged)
    // rdi: constructor function
    // rdx: new target
    __ InvokeFunction(rdi, rdx, rax, InvokeType::kCall);

    // Restore arguments count from the frame.
    __ movq(rbx, Operand(rbp, ConstructFrameConstants::kLengthOffset));

    // Leave construct frame.
  }

  // Remove caller arguments from the stack and return.
  __ DropArguments(rbx, rcx);

  __ ret(0);

  __ bind(&stack_overflow);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    __ int3();  // This should be unreachable.
  }
}

}  // namespace

// This code needs to be present in all continuations pushed onto the
// stack during the deoptimization process. It is part of a scheme to ensure
// that the return address immediately after the call to
// Builtin::kAdaptShadowStackForDeopt is present on the hardware shadow stack.
// Below, you'll see that this call is unconditionally jumped over. However,
// during deoptimization, the address of the call is jumped to directly
// and executed. The end result being that later, returning to that address
// after the call will be successful because the user stack and the
// shadow stack will be found to match perfectly.
void Generate_CallToAdaptShadowStackForDeopt(MacroAssembler* masm,
                                             bool add_jump) {
#ifdef V8_ENABLE_CET_SHADOW_STACK
  ASM_CODE_COMMENT(masm);
  Label post_adapt_shadow_stack;
  if (add_jump) __ jmp(&post_adapt_shadow_stack, Label::kNear);
  const auto saved_pc_offset = masm->pc_offset();
  __ Call(Operand(kRootRegister, IsolateData::BuiltinEntrySlotOffset(
                                     Builtin::kAdaptShadowStackForDeopt)));
  CHECK_EQ(Deoptimizer::kAdaptShadowStackOffsetToSubtract,
           masm->pc_offset() - saved_pc_offset);
  if (add_jump) __ bind(&post_adapt_shadow_stack);
#endif  // V8_ENABLE_CET_SHADOW_STACK
}

// The construct stub for ES5 constructor functions and ES6 class constructors.
void Builtins::Generate_JSConstructStubGeneric(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax: number of arguments (untagged)
  //  -- rdi: constructor function
  //  -- rdx: new target
  //  -- rsi: context
  //  -- sp[...]: constructor arguments
  // -----------------------------------

  FrameScope scope(masm, StackFrame::MANUAL);
  // Enter a construct frame.
  __ EnterFrame(StackFrame::CONSTRUCT);
  Label post_instantiation_deopt_entry, not_create_implicit_receiver;

  // Preserve the incoming parameters on the stack.
  __ Push(rsi);
  __ Push(rax);
  __ Push(rdi);
  __ PushRoot(RootIndex::kTheHoleValue);
  __ Push(rdx);

  // ----------- S t a t e -------------
  //  --         sp[0*kSystemPointerSize]: new target
  //  --         sp[1*kSystemPointerSize]: padding
  //  -- rdi and sp[2*kSystemPointerSize]: constructor function
  //  --         sp[3*kSystemPointerSize]: argument count
  //  --         sp[4*kSystemPointerSize]: context
  // -----------------------------------

  const TaggedRegister shared_function_info(rbx);
  __ LoadTaggedField(shared_function_info,
                     FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
  __ movl(rbx,
          FieldOperand(shared_function_info, SharedFunctionInfo::kFlagsOffset));
  __ DecodeField<SharedFunctionInfo::FunctionKindBits>(rbx);
  __ JumpIfIsInRange(
      rbx, static_cast<uint32_t>(FunctionKind::kDefaultDerivedConstructor),
      static_cast<uint32_t>(FunctionKind::kDerivedConstructor),
      &not_create_implicit_receiver, Label::kNear);

  // If not derived class constructor: Allocate the new receiver object.
  __ CallBuiltin(Builtin::kFastNewObject);
  __ jmp(&post_instantiation_deopt_entry, Label::kNear);

  // Else: use TheHoleValue as receiver for constructor call
  __ bind(&not_create_implicit_receiver);
  __ LoadRoot(rax, RootIndex::kTheHoleValue);

  // ----------- S t a t e -------------
  //  -- rax                          implicit receiver
  //  -- Slot 4 / sp[0*kSystemPointerSize]  new target
  //  -- Slot 3 / sp[1*kSystemPointerSize]  padding
  //  -- Slot 2 / sp[2*kSystemPointerSize]  constructor function
  //  -- Slot 1 / sp[3*kSystemPointerSize]  number of arguments
  //  -- Slot 0 / sp[4*kSystemPointerSize]  context
  // -----------------------------------
  __ bind(&post_instantiation_deopt_entry);

  // Restore new target.
  __ Pop(rdx);

  // Push the allocated receiver to the stack.
  __ Push(rax);

  // We need two copies because we may have to return the original one
  // and the calling conventions dictate that the called function pops the
  // receiver. The second copy is pushed after the arguments, we saved in r8
  // since rax needs to store the number of arguments before
  // InvokingFunction.
  __ movq(r8, rax);

  // Set up pointer to first argument (skip receiver).
  __ leaq(rbx, Operand(rbp, StandardFrameConstants::kFixedFrameSizeAboveFp +
                                kSystemPointerSize));

  // Restore constructor function and argument count.
  __ movq(rdi, Operand(rbp, ConstructFrameConstants::kConstructorOffset));
  __ movq(rax, Operand(rbp, ConstructFrameConstants::kLengthOffset));

  // Check if we have enough stack space to push all arguments.
  // Argument count in rax.
  Label stack_overflow;
  __ StackOverflowCheck(rax, &stack_overflow);

  // TODO(victorgomes): When the arguments adaptor is completely removed, we
  // should get the formal parameter count and copy the arguments in its
  // correct position (including any undefined), instead of delaying this to
  // InvokeFunction.

  // Copy arguments to the expression stack.
  // rbx: Pointer to start of arguments.
  // rax: Number of arguments.
  Generate_PushArguments(masm, rbx, rax, rcx, ArgumentsElementType::kRaw);

  // Push implicit receiver.
  __ Push(r8);

  // Call the function.
  __ InvokeFunction(rdi, rdx, rax, InvokeType::kCall);

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.
  Label use_receiver, do_throw, leave_and_return, check_result;

  // If the result is undefined, we'll use the implicit receiver. Otherwise we
  // do a smi check and fall through to check if the return value is a valid
  // receiver.
  __ JumpIfNotRoot(rax, RootIndex::kUndefinedValue, &check_result,
                   Label::kNear);

  // Throw away the result of the constructor invocation and use the
  // on-stack receiver as the result.
  __ bind(&use_receiver);
  __ movq(rax, Operand(rsp, 0 * kSystemPointerSize));
  __ JumpIfRoot(rax, RootIndex::kTheHoleValue, &do_throw, Label::kNear);

  __ bind(&leave_and_return);
  // Restore the arguments count.
  __ movq(rbx, Operand(rbp, ConstructFrameConstants::kLengthOffset));
  __ LeaveFrame(StackFrame::CONSTRUCT);
  // Remove caller arguments from the stack and return.
  __ DropArguments(rbx, rcx);
  __ ret(0);

  // If the result is a smi, it is *not* an object in the ECMA sense.
  __ bind(&check_result);
  __ JumpIfSmi(rax, &use_receiver, Label::kNear);

  // Check if the type of the result is not an object in the ECMA sense.
  __ JumpIfJSAnyIsNotPrimitive(rax, rcx, &leave_and_return, Label::kNear);
  __ jmp(&use_receiver);

  __ bind(&do_throw);
  // Restore context from the frame.
  __ movq(rsi, Operand(rbp, ConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowConstructorReturnedNonObject);
  // We don't return here.
  __ int3();

  __ bind(&stack_overflow);
  // Restore the context from the frame.
  __ movq(rsi, Operand(rbp, ConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowStackOverflow);
  // This should be unreachable.
  __ int3();

  // Since the address below is returned into instead of being called directly,
  // special code to get that address on the shadow stack is necessary to avoid
  // a security exception.
  Generate_CallToAdaptShadowStackForDeopt(masm, false);
  // Deoptimizer enters here.
  masm->isolate()->heap()->SetConstructStubCreateDeoptPCOffset(
      masm->pc_offset());
  __ jmp(&post_instantiation_deopt_entry, Label::kNear);
}

void Builtins::Generate_JSBuiltinsConstructStub(MacroAssembler* masm) {
  Generate_JSBuiltinsConstructStubHelper(masm);
}

void Builtins::Generate_ConstructedNonConstructable(MacroAssembler* masm) {
  FrameScope scope(masm, StackFrame::INTERNAL);
  __ Push(rdi);
  __ CallRuntime(Runtime::kThrowConstructedNonConstructable);
}

namespace {

// Called with the native C calling convention. The corresponding function
// signature is either:
//   using JSEntryFunction = GeneratedCode<Address(
//       Address root_register_value, Address new_target, Address target,
//       Address receiver, intptr_t argc, Address** argv)>;
// or
//   using JSEntryFunction = GeneratedCode<Address(
//       Address root_register_value, MicrotaskQueue* microtask_queue)>;
void Generate_JSEntryVariant(MacroAssembler* masm, StackFrame::Type type,
                             Builtin entry_trampoline) {
  Label invoke, handler_entry, exit;
  Label not_outermost_js, not_outermost_js_2;

  {
    NoRootArrayScope uninitialized_root_register(masm);

    // Set up the frame.
    //
    // Note: at this point we are entering V8-generated code from C++ and thus
    // rbp can be an arbitrary value (-fomit-frame-pointer). Since V8 still
    // needs to know where the next interesting frame is for the purpose of
    // stack walks, we instead push the stored EXIT frame fp
    // (IsolateAddressId::kCEntryFPAddress) below to a dedicated slot.
    __ pushq(rbp);
    __ movq(rbp, rsp);

    // Push the stack frame type.
    __ Push(Immediate(StackFrame::TypeToMarker(type)));
    // Reserve a slot for the context. It is filled after the root register has
    // been set up.
    __ AllocateStackSpace(kSystemPointerSize);
    // Save callee-saved registers (X64/X32/Win64 calling conventions).
    __ pushq(r12);
    __ pushq(r13);
    __ pushq(r14);
    __ pushq(r15);
#ifdef V8_TARGET_OS_WIN
    __ pushq(rdi);  // Only callee save in Win64 ABI, argument in AMD64 ABI.
    __ pushq(rsi);  // Only callee save in Win64 ABI, argument in AMD64 ABI.
#endif
    __ pushq(rbx);

#ifdef V8_TARGET_OS_WIN
    // On Win64 XMM6-XMM15 are callee-save.
    __ AllocateStackSpace(EntryFrameConstants::kXMMRegistersBlockSize);
    __ movdqu(Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 0), xmm6);
    __ movdqu(Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 1), xmm7);
    __ movdqu(Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 2), xmm8);
    __ movdqu(Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 3), xmm9);
    __ movdqu(Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 4), xmm10);
    __ movdqu(Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 5), xmm11);
    __ movdqu(Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 6), xmm12);
    __ movdqu(Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 7), xmm13);
    __ movdqu(Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 8), xmm14);
    __ movdqu(Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 9), xmm15);
    static_assert(EntryFrameConstants::kCalleeSaveXMMRegisters == 10);
    static_assert(EntryFrameConstants::kXMMRegistersBlockSize ==
                  EntryFrameConstants::kXMMRegisterSize *
                      EntryFrameConstants::kCalleeSaveXMMRegisters);
#endif

    // Initialize the root register.
    // C calling convention. The first argument is passed in kCArgRegs[0].
    __ movq(kRootRegister, kCArgRegs[0]);

#ifdef V8_COMPRESS_POINTERS
    // Initialize the pointer cage base register.
    __ LoadRootRelative(kPtrComprCageBaseRegister,
                        IsolateData::cage_base_offset());
#endif
  }

  // Save copies of the top frame descriptor on the stack.
  ExternalReference c_entry_fp = ExternalReference::Create(
      IsolateAddressId::kCEntryFPAddress, masm->isolate());

  {
    // Keep this static_assert to preserve a link between the offset constant
    // and the code location it refers to.
#ifdef V8_TARGET_OS_WIN
    static_assert(EntryFrameConstants::kNextExitFrameFPOffset ==
                  -3 * kSystemPointerSize + -7 * kSystemPointerSize -
                      EntryFrameConstants::kXMMRegistersBlockSize);
#else
    static_assert(EntryFrameConstants::kNextExitFrameFPOffset ==
                  -3 * kSystemPointerSize + -5 * kSystemPointerSize);
#endif  // V8_TARGET_OS_WIN
    Operand c_entry_fp_operand = masm->ExternalReferenceAsOperand(c_entry_fp);
    __ Push(c_entry_fp_operand);

    // Clear c_entry_fp, now we've pushed its previous value to the stack.
    // If the c_entry_fp is not already zero and we don't clear it, the
    // StackFrameIteratorForProfiler will assume we are executing C++ and miss
    // the JS frames on top.
    // Do the same for the fast C call fp and pc.
    __ Move(c_entry_fp_operand, 0);

    Operand fast_c_call_fp_operand =
        masm->ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP);
    Operand fast_c_call_pc_operand =
        masm->ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerPC);
    __ Push(fast_c_call_fp_operand);
    __ Move(fast_c_call_fp_operand, 0);

    __ Push(fast_c_call_pc_operand);
    __ Move(fast_c_call_pc_operand, 0);
  }

  // Store the context address in the previously-reserved slot.
  ExternalReference context_address = ExternalReference::Create(
      IsolateAddressId::kContextAddress, masm->isolate());
  __ Load(kScratchRegister, context_address);

  static constexpr int kOffsetToContextSlot = -2 * kSystemPointerSize;
  __ movq(Operand(rbp, kOffsetToContextSlot), kScratchRegister);

  // If this is the outermost JS call, set js_entry_sp value.
  ExternalReference js_entry_sp = ExternalReference::Create(
      IsolateAddressId::kJSEntrySPAddress, masm->isolate());
  __ Load(rax, js_entry_sp);
  __ testq(rax, rax);
  __ j(not_zero, &not_outermost_js);
  __ Push(Immediate(StackFrame::OUTERMOST_JSENTRY_FRAME));
  __ movq(rax, rbp);
  __ Store(js_entry_sp, rax);
  Label cont;
  __ jmp(&cont);
  __ bind(&not_outermost_js);
  __ Push(Immediate(StackFrame::INNER_JSENTRY_FRAME));
  __ bind(&cont);

  // Jump to a faked try block that does the invoke, with a faked catch
  // block that sets the exception.
  __ jmp(&invoke);
  __ BindExceptionHandler(&handler_entry);

  // Exception handlers are always invoked in sandboxed execution mode, but
  // this code runs unsandboxed, so here we need to manually ExitSandbox().
  __ ExitSandbox();

  // Store the current pc as the handler offset. It's used later to create the
  // handler table.
  masm->isolate()->builtins()->SetJSEntryHandlerOffset(handler_entry.pos());

  // Caught exception: Store result (exception) in the exception
  // field in the JSEnv and return a failure sentinel.
  ExternalReference exception = ExternalReference::Create(
      IsolateAddressId::kExceptionAddress, masm->isolate());
  __ Store(exception, rax);
  __ LoadRoot(rax, RootIndex::kException);
  __ jmp(&exit);

  // Invoke: Link this frame into the handler chain.
  __ bind(&invoke);
  __ PushStackHandler();

  // Invoke the function by calling through JS entry trampoline builtin and
  // pop the faked function when we return.
  __ CallBuiltin(entry_trampoline);

  // Unlink this frame from the handler chain.
  __ PopStackHandler();

  __ bind(&exit);

  // Check if the current stack frame is marked as the outermost JS frame.
  __ Pop(rbx);
  __ cmpq(rbx, Immediate(StackFrame::OUTERMOST_JSENTRY_FRAME));
  __ j(not_equal, &not_outermost_js_2);
  __ Move(kScratchRegister, js_entry_sp);
  __ movq(Operand(kScratchRegister, 0), Immediate(0));
  __ bind(&not_outermost_js_2);

  // Restore the top frame descriptor from the stack.
  {
    Operand fast_c_call_pc_operand =
        masm->ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerPC);
    __ Pop(fast_c_call_pc_operand);

    Operand fast_c_call_fp_operand =
        masm->ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP);
    __ Pop(fast_c_call_fp_operand);

    Operand c_entry_fp_operand = masm->ExternalReferenceAsOperand(c_entry_fp);
    __ Pop(c_entry_fp_operand);
  }

  // Restore callee-saved registers (X64 conventions).
#ifdef V8_TARGET_OS_WIN
  // On Win64 XMM6-XMM15 are callee-save
  __ movdqu(xmm6, Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 0));
  __ movdqu(xmm7, Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 1));
  __ movdqu(xmm8, Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 2));
  __ movdqu(xmm9, Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 3));
  __ movdqu(xmm10, Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 4));
  __ movdqu(xmm11, Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 5));
  __ movdqu(xmm12, Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 6));
  __ movdqu(xmm13, Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 7));
  __ movdqu(xmm14, Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 8));
  __ movdqu(xmm15, Operand(rsp, EntryFrameConstants::kXMMRegisterSize * 9));
  __ addq(rsp, Immediate(EntryFrameConstants::kXMMRegistersBlockSize));
#endif

  __ popq(rbx);
#ifdef V8_TARGET_OS_WIN
  // Callee save on in Win64 ABI, arguments/volatile in AMD64 ABI.
  __ popq(rsi);
  __ popq(rdi);
#endif
  __ popq(r15);
  __ popq(r14);
  __ popq(r13);
  __ popq(r12);
  __ addq(rsp, Immediate(2 * kSystemPointerSize));  // remove markers

  // Restore frame pointer and return.
  __ popq(rbp);
  __ ret(0);
}

}  // namespace

void Builtins::Generate_JSEntry(MacroAssembler* masm) {
  Generate_JSEntryVariant(masm, StackFrame::ENTRY, Builtin::kJSEntryTrampoline);
}

void Builtins::Generate_JSConstructEntry(MacroAssembler* masm) {
  Generate_JSEntryVariant(masm, StackFrame::CONSTRUCT_ENTRY,
                          Builtin::kJSConstructEntryTrampoline);
}

void Builtins::Generate_JSRunMicrotasksEntry(MacroAssembler* masm) {
  Generate_JSEntryVariant(masm, StackFrame::ENTRY,
                          Builtin::kRunMicrotasksTrampoline);
}

static void Generate_JSEntryTrampolineHelper(MacroAssembler* masm,
                                             bool is_construct) {
  // Expects six C++ function parameters.
  // - Address root_register_value
  // - Address new_target (tagged Object pointer)
  // - Address function (tagged JSFunction pointer)
  // - Address receiver (tagged Object pointer)
  // - intptr_t argc
  // - Address** argv (pointer to array of tagged Object pointers)
  // (see Handle::Invoke in execution.cc).

  // This builtins transitions into sandboxed execution mode.
  // TODO(350324877): here EnterSandbox doesn't need to preserve all registers.
  __ EnterSandbox();
  __ SetSandboxingModeForCurrentBuiltin(CodeSandboxingMode::kSandboxed);

  // Open a C++ scope for the FrameScope.
  {
    // Platform specific argument handling. After this, the stack contains
    // an internal frame and the pushed function and receiver, and
    // register rax and rbx holds the argument count and argument array,
    // while rdi holds the function pointer, rsi the context, and rdx the
    // new.target.

    // MSVC parameters in:
    // rcx        : root_register_value
    // rdx        : new_target
    // r8         : function
    // r9         : receiver
    // [rsp+0x20] : argc
    // [rsp+0x28] : argv
    //
    // GCC parameters in:
    // rdi : root_register_value
    // rsi : new_target
    // rdx : function
    // rcx : receiver
    // r8  : argc
    // r9  : argv

    __ movq(rdi, kCArgRegs[2]);
    __ Move(rdx, kCArgRegs[1]);
    // rdi : function
    // rdx : new_target

    // Clear the context before we push it when entering the internal frame.
    __ Move(rsi, 0);

    // Enter an internal frame.
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Setup the context (we need to use the caller context from the isolate).
    ExternalReference context_address = ExternalReference::Create(
        IsolateAddressId::kContextAddress, masm->isolate());
    __ movq(rsi, masm->ExternalReferenceAsOperand(context_address));
#ifdef DEBUG
    __ Move(__ ExternalReferenceAsOperand(context_address),
            Context::kNoContext);
#endif  // DEBUG

    // Push the function onto the stack.
    __ Push(rdi);

#ifdef V8_TARGET_OS_WIN
    // Load the previous frame pointer to access C arguments on stack
    __ movq(kScratchRegister, Operand(rbp, 0));
    // Load the number of arguments and setup pointer to the arguments.
    __ movq(rax, Operand(kScratchRegister, EntryFrameConstants::kArgcOffset));
    __ movq(rbx, Operand(kScratchRegister, EntryFrameConstants::kArgvOffset));
#else   // V8_TARGET_OS_WIN
    // Load the number of arguments and setup pointer to the arguments.
    __ movq(rax, r8);
    __ movq(rbx, r9);
    __ movq(r9, kCArgRegs[3]);  // Temporarily saving the receiver.
#endif  // V8_TARGET_OS_WIN

    // Current stack contents:
    // [rsp + kSystemPointerSize]     : Internal frame
    // [rsp]                          : function
    // Current register contents:
    // rax : argc
    // rbx : argv
    // rsi : context
    // rdi : function
    // rdx : new.target
    // r9  : receiver

    // Check if we have enough stack space to push all arguments.
    // Argument count in rax.
    Label enough_stack_space, stack_overflow;
    __ StackOverflowCheck(rax, &stack_overflow, Label::kNear);
    __ jmp(&enough_stack_space, Label::kNear);

    __ bind(&stack_overflow);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    // This should be unreachable.
    __ int3();

    __ bind(&enough_stack_space);

    // Copy arguments to the stack.
    // Register rbx points to array of pointers to handle locations.
    // Push the values of these handles.
    // rbx: Pointer to start of arguments.
    // rax: Number of arguments.
    Generate_PushArguments(masm, rbx, rax, rcx, ArgumentsElementType::kHandle);

    // Push the receiver.
    __ Push(r9);

    // Invoke the builtin code.
    Builtin builtin = is_construct ? Builtin::kConstruct : Builtins::Call();
    __ CallBuiltin(builtin);

    // Exit the internal frame. Notice that this also removes the empty
    // context and the function left on the stack by the code
    // invocation.
  }

  // TODO(350324877): here ExitSandbox doesn't need to preserve all registers.
  __ ExitSandbox();

  __ ret(0);
}

void Builtins::Generate_JSEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, false);
}

void Builtins::Generate_JSConstructEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, true);
}

void Builtins::Generate_RunMicrotasksTrampoline(MacroAssembler* masm) {
  // kCArgRegs[1]: microtask_queue
  __ movq(RunMicrotasksDescriptor::MicrotaskQueueRegister(), kCArgRegs[1]);
  __ TailCallBuiltin(Builtin::kRunMicrotasks);
}

static void AssertCodeIsBaselineAllowClobber(MacroAssembler* masm,
                                             Register code, Register scratch) {
  // Verify that the code kind is baseline code via the CodeKind.
  __ movl(scratch, FieldOperand(code, Code::kFlagsOffset));
  __ DecodeField<Code::KindField>(scratch);
  __ cmpl(scratch, Immediate(static_cast<int>(CodeKind::BASELINE)));
  __ Assert(equal, AbortReason::kExpectedBaselineData);
}

static void AssertCodeIsBaseline(MacroAssembler* masm, Register code,
                                 Register scratch) {
  DCHECK(!AreAliased(code, scratch));
  return AssertCodeIsBaselineAllowClobber(masm, code, scratch);
}

static void GetSharedFunctionInfoBytecodeOrBaseline(
    MacroAssembler* masm, Register sfi, Register bytecode, Register scratch1,
    Label* is_baseline, Label* is_unavailable) {
  ASM_CODE_COMMENT(masm);
  Label done;

  Register data = bytecode;
  __ LoadTrustedPointerField(
      data, FieldOperand(sfi, SharedFunctionInfo::kTrustedFunctionDataOffset),
      kUnknownIndirectPointerTag, scratch1);

  if (V8_JITLESS_BOOL) {
    __ IsObjectType(data, INTERPRETER_DATA_TYPE, scratch1);
    __ j(not_equal, &done, Label::kNear);
  } else {
#if V8_STATIC_ROOTS_BOOL
    __ IsObjectTypeFast(data, CODE_TYPE, scratch1);
#else
    __ CmpObjectType(data, CODE_TYPE, scratch1);
#endif  // V8_STATIC_ROOTS_BOOL
  if (v8_flags.debug_code) {
    Label not_baseline;
    __ j(not_equal, &not_baseline);
    AssertCodeIsBaseline(masm, data, scratch1);
    __ j(equal, is_baseline);
    __ bind(&not_baseline);
  } else {
    __ j(equal, is_baseline);
  }

#if V8_STATIC_ROOTS_BOOL
  // Scratch1 already contains the compressed map.
  __ CompareInstanceTypeWithUniqueCompressedMap(scratch1,
                                                INTERPRETER_DATA_TYPE);
#else
  // Scratch1 already contains the instance type.
  __ CmpInstanceType(scratch1, INTERPRETER_DATA_TYPE);
#endif  // V8_STATIC_ROOTS_BOOL
  __ j(not_equal, &done, Label::kNear);
  }

  __ LoadInterpreterDataBytecodeArray(bytecode, data);

  __ bind(&done);
  __ IsObjectType(bytecode, BYTECODE_ARRAY_TYPE, scratch1);
  __ j(not_equal, is_unavailable);
}

// static
void Builtins::Generate_ResumeGeneratorTrampoline(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax    : the value to pass to the generator
  //  -- rdx    : the JSGeneratorObject to resume
  //  -- rsp[0] : return address
  // -----------------------------------

  // Store input value into generator object.
  __ StoreTaggedField(
      FieldOperand(rdx, JSGeneratorObject::kInputOrDebugPosOffset), rax);
  Register object = WriteBarrierDescriptor::ObjectRegister();
  __ Move(object, rdx);
  __ RecordWriteField(object, JSGeneratorObject::kInputOrDebugPosOffset, rax,
                      WriteBarrierDescriptor::SlotAddressRegister(),
                      SaveFPRegsMode::kIgnore);
  // Check that rdx is still valid, RecordWrite might have clobbered it.
  __ AssertGeneratorObject(rdx);

  // Load suspended function and context.
  __ LoadTaggedField(rdi,
                     FieldOperand(rdx, JSGeneratorObject::kFunctionOffset));
  __ LoadTaggedField(rsi, FieldOperand(rdi, JSFunction::kContextOffset));

  // Flood function if we are stepping.
  Label prepare_step_in_if_stepping, prepare_step_in_suspended_generator;
  Label stepping_prepared;
  ExternalReference debug_hook =
      ExternalReference::debug_hook_on_function_call_address(masm->isolate());
  Operand debug_hook_operand = masm->ExternalReferenceAsOperand(debug_hook);
  __ cmpb(debug_hook_operand, Immediate(0));
  __ j(not_equal, &prepare_step_in_if_stepping);

  // Flood function if we need to continue stepping in the suspended generator.
  ExternalReference debug_suspended_generator =
      ExternalReference::debug_suspended_generator_address(masm->isolate());
  Operand debug_suspended_generator_operand =
      masm->ExternalReferenceAsOperand(debug_suspended_generator);
  __ cmpq(rdx, debug_suspended_generator_operand);
  __ j(equal, &prepare_step_in_suspended_generator);
  __ bind(&stepping_prepared);

  // Check the stack for overflow. We are not trying to catch interruptions
  // (i.e. debug break and preemption) here, so check the "real stack limit".
  Label stack_overflow;
  __ cmpq(rsp, __ StackLimitAsOperand(StackLimitKind::kRealStackLimit));
  __ j(below, &stack_overflow);

  // ----------- S t a t e -------------
  //  -- rdx    : the JSGeneratorObject to resume
  //  -- rdi    : generator function
  //  -- rsi    : generator context
  // -----------------------------------

  Register decompr_scratch1 = COMPRESS_POINTERS_BOOL ? r8 : no_reg;
  Register argc = kJavaScriptCallArgCountRegister;
  Register index = r9;
  Register return_address = r11;
  Register params_array = rbx;

  __ PopReturnAddressTo(return_address);

  // Compute actual arguments count value as a formal parameter count without
  // receiver, loaded from the dispatch table entry or shared function info.
#if V8_ENABLE_LEAPTIERING
  static_assert(kJavaScriptCallCodeStartRegister == rcx, "ABI mismatch");
  static_assert(kJavaScriptCallDispatchHandleRegister == r15, "ABI mismatch");
  __ movl(r15, FieldOperand(rdi, JSFunction::kDispatchHandleOffset));
  __ LoadEntrypointAndParameterCountFromJSDispatchTable(rcx, argc, r15);
#else
  __ LoadTaggedField(argc,
                     FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
  __ movzxwq(argc, FieldOperand(
                       argc, SharedFunctionInfo::kFormalParameterCountOffset));
#endif  // V8_ENABLE_LEAPTIERING

  // Сopy the function arguments from the generator object's register file.
  {
    Label push_arguments, done_loop, loop;

#if V8_ENABLE_LEAPTIERING
    // In case the formal parameter count is kDontAdaptArgumentsSentinel the
    // actual arguments count should be set accordingly.
    static_assert(kDontAdaptArgumentsSentinel < JSParameterCount(0));
    __ cmpl(argc, Immediate(JSParameterCount(0)));
    __ j(kGreaterThan, &push_arguments, Label::kNear);
    __ movl(argc, Immediate(JSParameterCount(0)));
    __ jmp(&done_loop, Label::kNear);
#else
    // Generator functions are always created from user code and thus the
    // formal parameter count is never equal to kDontAdaptArgumentsSentinel,
    // which is used only for certain non-generator builtin functions.
#endif  // V8_ENABLE_LEAPTIERING

    __ bind(&push_arguments);
    __ LoadTaggedField(
        params_array,
        FieldOperand(rdx, JSGeneratorObject::kParametersAndRegistersOffset));

    // Exclude receiver.
    __ leal(index, Operand(argc, -1));

    __ bind(&loop);
    __ decl(index);
    __ j(kLessThan, &done_loop, Label::kNear);
    __ PushTaggedField(FieldOperand(params_array, index, times_tagged_size,
                                    OFFSET_OF_DATA_START(FixedArray)),
                       decompr_scratch1);
    __ jmp(&loop);
    __ bind(&done_loop);

    // Push the receiver.
    __ PushTaggedField(FieldOperand(rdx, JSGeneratorObject::kReceiverOffset),
                       decompr_scratch1);
  }

  // Underlying function needs to have bytecode available.
  if (v8_flags.debug_code) {
    Label is_baseline, is_unavailable, ok;
    Register scratch = ReassignRegister(params_array);
    __ LoadTaggedField(
        scratch, FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
    GetSharedFunctionInfoBytecodeOrBaseline(masm, scratch, scratch,
                                            kScratchRegister, &is_baseline,
                                            &is_unavailable);
    __ jmp(&ok);

    __ bind(&is_unavailable);
    __ Abort(AbortReason::kMissingBytecodeArray);

    __ bind(&is_baseline);
    __ IsObjectType(scratch, CODE_TYPE, scratch);
    __ Assert(equal, AbortReason::kMissingBytecodeArray);

    __ bind(&ok);
  }

  // Resume (Ignition/TurboFan) generator object.
  {
    __ PushReturnAddressFrom(return_address);
    // We abuse new.target both to indicate that this is a resume call and to
    // pass in the generator object.  In ordinary calls, new.target is always
    // undefined because generator functions are non-constructable.
    static_assert(kJavaScriptCallCodeStartRegister == rcx, "ABI mismatch");
#if V8_ENABLE_LEAPTIERING
    // Actual arguments count and code start are already initialized above.
    __ jmp(rcx);
#else
    // Actual arguments count is already initialized above.
    __ JumpJSFunction(rdi);
#endif  // V8_ENABLE_LEAPTIERING
  }

  __ bind(&prepare_step_in_if_stepping);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(rdx);
    __ Push(rdi);
    // Push hole as receiver since we do not use it for stepping.
    __ PushRoot(RootIndex::kTheHoleValue);
    __ CallRuntime(Runtime::kDebugOnFunctionCall);
    __ Pop(rdx);
    __ LoadTaggedField(rdi,
                       FieldOperand(rdx, JSGeneratorObject::kFunctionOffset));
  }
  __ jmp(&stepping_prepared);

  __ bind(&prepare_step_in_suspended_generator);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(rdx);
    __ CallRuntime(Runtime::kDebugPrepareStepInSuspendedGenerator);
    __ Pop(rdx);
    __ LoadTaggedField(rdi,
                       FieldOperand(rdx, JSGeneratorObject::kFunctionOffset));
  }
  __ jmp(&stepping_prepared);

  __ bind(&stack_overflow);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    __ int3();  // This should be unreachable.
  }
}

static void LeaveInterpreterFrame(MacroAssembler* masm, Register scratch1,
                                  Register scratch2) {
  ASM_CODE_COMMENT(masm);
  Register params_size = scratch1;
  // Get the size of the formal parameters (in bytes).
  __ movq(params_size,
          Operand(rbp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ movzxwl(params_size,
             FieldOperand(params_size, BytecodeArray::kParameterSizeOffset));

  Register actual_params_size = scratch2;
  // Compute the size of the actual parameters (in bytes).
  __ movq(actual_params_size,
          Operand(rbp, StandardFrameConstants::kArgCOffset));

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  __ cmpq(params_size, actual_params_size);
  __ cmovq(kLessThan, params_size, actual_params_size);

  // Leave the frame (also dropping the register file).
  __ leave();

  // Drop receiver + arguments.
  __ DropArguments(params_size, scratch2);
}

// Tail-call |function_id| if |actual_state| == |expected_state|
// Advance the current bytecode offset. This simulates what all bytecode
// handlers do upon completion of the underlying operation. Will bail out to a
// label if the bytecode (without prefix) is a return bytecode. Will not advance
// the bytecode offset if the current bytecode is a JumpLoop, instead just
// re-executing the JumpLoop to jump to the correct bytecode.
static void AdvanceBytecodeOffsetOrReturn(MacroAssembler* masm,
                                          Register bytecode_array,
                                          Register bytecode_offset,
                                          Register bytecode, Register scratch1,
                                          Register scratch2, Label* if_return) {
  ASM_CODE_COMMENT(masm);
  Register bytecode_size_table = scratch1;

  // The bytecode offset value will be increased by one in wide and extra wide
  // cases. In the case of having a wide or extra wide JumpLoop bytecode, we
  // will restore the original bytecode. In order to simplify the code, we have
  // a backup of it.
  Register original_bytecode_offset = scratch2;
  DCHECK(!AreAliased(bytecode_array, bytecode_offset, bytecode,
                     bytecode_size_table, original_bytecode_offset));

  __ movq(original_bytecode_offset, bytecode_offset);

  __ Move(bytecode_size_table,
          ExternalReference::bytecode_size_table_address());

  // Check if the bytecode is a Wide or ExtraWide prefix bytecode.
  Label process_bytecode, extra_wide;
  static_assert(0 == static_cast<int>(interpreter::Bytecode::kWide));
  static_assert(1 == static_cast<int>(interpreter::Bytecode::kExtraWide));
  static_assert(2 == static_cast<int>(interpreter::Bytecode::kDebugBreakWide));
  static_assert(3 ==
                static_cast<int>(interpreter::Bytecode::kDebugBreakExtraWide));
  __ cmpb(bytecode, Immediate(0x3));
  __ j(above, &process_bytecode, Label::kNear);
  // The code to load the next bytecode is common to both wide and extra wide.
  // We can hoist them up here. incl has to happen before testb since it
  // modifies the ZF flag.
  __ incl(bytecode_offset);
  __ testb(bytecode, Immediate(0x1));
  __ movzxbq(bytecode, Operand(bytecode_array, bytecode_offset, times_1, 0));
  __ j(not_equal, &extra_wide, Label::kNear);

  // Update table to the wide scaled table.
  __ addq(bytecode_size_table,
          Immediate(kByteSize * interpreter::Bytecodes::kBytecodeCount));
  __ jmp(&process_bytecode, Label::kNear);

  __ bind(&extra_wide);
  // Update table to the extra wide scaled table.
  __ addq(bytecode_size_table,
          Immediate(2 * kByteSize * interpreter::Bytecodes::kBytecodeCount));

  __ bind(&process_bytecode);

// Bailout to the return label if this is a return bytecode.
#define JUMP_IF_EQUAL(NAME)                                             \
  __ cmpb(bytecode,                                                     \
          Immediate(static_cast<int>(interpreter::Bytecode::k##NAME))); \
  __ j(equal, if_return, Label::kFar);
  RETURN_BYTECODE_LIST(JUMP_IF_EQUAL)
#undef JUMP_IF_EQUAL

  // If this is a JumpLoop, re-execute it to perform the jump to the beginning
  // of the loop.
  Label end, not_jump_loop;
  __ cmpb(bytecode,
          Immediate(static_cast<int>(interpreter::Bytecode::kJumpLoop)));
  __ j(not_equal, &not_jump_loop, Label::kNear);
  // We need to restore the original bytecode_offset since we might have
  // increased it to skip the wide / extra-wide prefix bytecode.
  __ movq(bytecode_offset, original_bytecode_offset);
  __ jmp(&end, Label::kNear);

  __ bind(&not_jump_loop);
  // Otherwise, load the size of the current bytecode and advance the offset.
  __ movzxbl(kScratchRegister,
             Operand(bytecode_size_table, bytecode, times_1, 0));
  __ addl(bytecode_offset, kScratchRegister);

  __ bind(&end);
}

namespace {

void ResetSharedFunctionInfoAge(MacroAssembler* masm, Register sfi) {
  __ movw(FieldOperand(sfi, SharedFunctionInfo::kAgeOffset), Immediate(0));
}

void ResetJSFunctionAge(MacroAssembler* masm, Register js_function) {
  const Register shared_function_info(kScratchRegister);
  __ LoadTaggedField(
      shared_function_info,
      FieldOperand(js_function, JSFunction::kSharedFunctionInfoOffset));
  ResetSharedFunctionInfoAge(masm, shared_function_info);
}

void ResetFeedbackVectorOsrUrgency(MacroAssembler* masm,
                                   Register feedback_vector, Register scratch) {
  __ movb(scratch,
          FieldOperand(feedback_vector, FeedbackVector::kOsrStateOffset));
  __ andb(scratch, Immediate(~FeedbackVector::OsrUrgencyBits::kMask));
  __ movb(FieldOperand(feedback_vector, FeedbackVector::kOsrStateOffset),
          scratch);
}

}  // namespace

// Generate code for entering a JS function with the interpreter.
// On entry to the function the receiver and arguments have been pushed on the
// stack left to right.
//
// The live registers are:
//   o rax: actual argument count
//   o rdi: the JS function object being called
//   o rdx: the incoming new target or generator object
//   o rsi: our context
//   o rbp: the caller's frame pointer
//   o rsp: stack pointer (pointing to return address)
//
// The function builds an interpreter frame. See InterpreterFrameConstants in
// frame-constants.h for its layout.
void Builtins::Generate_InterpreterEntryTrampoline(
    MacroAssembler* masm, InterpreterEntryTrampolineMode mode) {
  Register closure = rdi;

  __ AssertInSandboxedExecutionMode();

  // Get the bytecode array from the function object and load it into
  // kInterpreterBytecodeArrayRegister.
  const Register shared_function_info(r11);
  __ LoadTaggedField(
      shared_function_info,
      FieldOperand(closure, JSFunction::kSharedFunctionInfoOffset));
  ResetSharedFunctionInfoAge(masm, shared_function_info);

  // The bytecode array could have been flushed from the shared function info,
  // if so, call into CompileLazy.
  Label is_baseline, compile_lazy;
  GetSharedFunctionInfoBytecodeOrBaseline(
      masm, shared_function_info, kInterpreterBytecodeArrayRegister,
      kScratchRegister, &is_baseline, &compile_lazy);

#ifdef V8_ENABLE_SANDBOX
  // Validate the parameter count. This protects against an attacker swapping
  // the bytecode (or the dispatch handle) such that the parameter count of the
  // dispatch entry doesn't match the one of the BytecodeArray.
  // TODO(saelo): instead of this validation step, it would probably be nicer
  // if we could store the BytecodeArray directly in the dispatch entry and
  // load it from there. Then we can easily guarantee that the parameter count
  // of the entry matches the parameter count of the bytecode.
  static_assert(V8_JS_LINKAGE_INCLUDES_DISPATCH_HANDLE_BOOL);
  Register dispatch_handle = kJavaScriptCallDispatchHandleRegister;
  __ LoadParameterCountFromJSDispatchTable(r8, dispatch_handle);
  __ cmpw(r8, FieldOperand(kInterpreterBytecodeArrayRegister,
                           BytecodeArray::kParameterSizeOffset));
  __ SbxCheck(equal, AbortReason::kJSSignatureMismatch);
#endif  // V8_ENABLE_SANDBOX

  Label push_stack_frame;
  Register feedback_vector = rbx;
  __ LoadFeedbackVector(feedback_vector, closure, &push_stack_frame,
                        Label::kNear);

#ifndef V8_JITLESS
#ifndef V8_ENABLE_LEAPTIERING
  // If feedback vector is valid, check for optimized code and update invocation
  // count.
  Label flags_need_processing;
  __ CheckFeedbackVectorFlagsAndJumpIfNeedsProcessing(
      feedback_vector, CodeKind::INTERPRETED_FUNCTION, &flags_need_processing);
#endif  // !V8_ENABLE_LEAPTIERING

  ResetFeedbackVectorOsrUrgency(masm, feedback_vector, kScratchRegister);

  // Increment invocation count for the function.
  __ incl(
      FieldOperand(feedback_vector, FeedbackVector::kInvocationCountOffset));

  // Open a frame scope to indicate that there is a frame on the stack.  The
  // MANUAL indicates that the scope shouldn't actually generate code to set up
  // the frame (that is done below).
#else
  // Note: By omitting the above code in jitless mode we also disable:
  // - kFlagsLogNextExecution: only used for logging/profiling; and
  // - kInvocationCountOffset: only used for tiering heuristics and code
  //   coverage.
#endif  // !V8_JITLESS

  __ bind(&push_stack_frame);
  FrameScope frame_scope(masm, StackFrame::MANUAL);
  __ pushq(rbp);  // Caller's frame pointer.
  __ movq(rbp, rsp);
  __ Push(kContextRegister);                 // Callee's context.
  __ Push(kJavaScriptCallTargetRegister);    // Callee's JS function.
  __ Push(kJavaScriptCallArgCountRegister);  // Actual argument count.

  // Load initial bytecode offset.
  __ Move(kInterpreterBytecodeOffsetRegister,
          BytecodeArray::kHeaderSize - kHeapObjectTag);

  // Push bytecode array and Smi tagged bytecode offset.
  __ Push(kInterpreterBytecodeArrayRegister);
  __ SmiTag(rcx, kInterpreterBytecodeOffsetRegister);
  __ Push(rcx);

  // Push feedback vector.
  __ Push(feedback_vector);

  // Allocate the local and temporary register file on the stack.
  Label stack_overflow;
  {
    // Load frame size from the BytecodeArray object.
    __ movl(rcx, FieldOperand(kInterpreterBytecodeArrayRegister,
                              BytecodeArray::kFrameSizeOffset));

    // Do a stack check to ensure we don't go over the limit.
    __ movq(rax, rsp);
    __ subq(rax, rcx);
    __ cmpq(rax, __ StackLimitAsOperand(StackLimitKind::kRealStackLimit));
    __ j(below, &stack_overflow);

    // If ok, push undefined as the initial value for all register file entries.
    Label loop_header;
    Label loop_check;
    __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
    __ jmp(&loop_check, Label::kNear);
    __ bind(&loop_header);
    // TODO(rmcilroy): Consider doing more than one push per loop iteration.
    __ Push(kInterpreterAccumulatorRegister);
    // Continue loop if not done.
    __ bind(&loop_check);
    __ subq(rcx, Immediate(kSystemPointerSize));
    __ j(greater_equal, &loop_header, Label::kNear);
  }

  // If the bytecode array has a valid incoming new target or generator object
  // register, initialize it with incoming value which was passed in rdx.
  Label no_incoming_new_target_or_generator_register;
  __ movsxlq(
      rcx,
      FieldOperand(kInterpreterBytecodeArrayRegister,
                   BytecodeArray::kIncomingNewTargetOrGeneratorRegisterOffset));
  __ testl(rcx, rcx);
  __ j(zero, &no_incoming_new_target_or_generator_register, Label::kNear);
  __ movq(Operand(rbp, rcx, times_system_pointer_size, 0), rdx);
  __ bind(&no_incoming_new_target_or_generator_register);

  // Perform interrupt stack check.
  // TODO(solanes): Merge with the real stack limit check above.
  Label stack_check_interrupt, after_stack_check_interrupt;
  __ cmpq(rsp, __ StackLimitAsOperand(StackLimitKind::kInterruptStackLimit));
  __ j(below, &stack_check_interrupt);
  __ bind(&after_stack_check_interrupt);

  // The accumulator is already loaded with undefined.

  // Load the dispatch table into a register and dispatch to the bytecode
  // handler at the current bytecode offset.
  Label do_dispatch;
  __ bind(&do_dispatch);
  __ Move(
      kInterpreterDispatchTableRegister,
      ExternalReference::interpreter_dispatch_table_address(masm->isolate()));
  __ movzxbq(kScratchRegister,
             Operand(kInterpreterBytecodeArrayRegister,
                     kInterpreterBytecodeOffsetRegister, times_1, 0));
  __ movq(kJavaScriptCallCodeStartRegister,
          Operand(kInterpreterDispatchTableRegister, kScratchRegister,
                  times_system_pointer_size, 0));

  // X64 has this location as the interpreter_entry_return_offset for CET
  // shadow stack rather than after `call`. InterpreterEnterBytecode will
  // jump to this location and call kJavaScriptCallCodeStartRegister, which
  // will form the valid shadow stack.
  __ RecordComment("--- InterpreterEntryPC point ---");
  if (mode == InterpreterEntryTrampolineMode::kDefault) {
    masm->isolate()->heap()->SetInterpreterEntryReturnPCOffset(
        masm->pc_offset());
  } else {
    DCHECK_EQ(mode, InterpreterEntryTrampolineMode::kForProfiling);
    // Both versions must be the same up to this point otherwise the builtins
    // will not be interchangable.
    CHECK_EQ(
        masm->isolate()->heap()->interpreter_entry_return_pc_offset().value(),
        masm->pc_offset());
  }
  __ call(kJavaScriptCallCodeStartRegister);

  // Any returns to the entry trampoline are either due to the return bytecode
  // or the interpreter tail calling a builtin and then a dispatch.

  // Get bytecode array and bytecode offset from the stack frame.
  __ movq(kInterpreterBytecodeArrayRegister,
          Operand(rbp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ SmiUntagUnsigned(
      kInterpreterBytecodeOffsetRegister,
      Operand(rbp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  // Either return, or advance to the next bytecode and dispatch.
  Label do_return;
  __ movzxbq(rbx, Operand(kInterpreterBytecodeArrayRegister,
                          kInterpreterBytecodeOffsetRegister, times_1, 0));
  AdvanceBytecodeOffsetOrReturn(masm, kInterpreterBytecodeArrayRegister,
                                kInterpreterBytecodeOffsetRegister, rbx, rcx,
                                r8, &do_return);
  __ jmp(&do_dispatch);

  __ bind(&do_return);
  // The return value is in rax.
  LeaveInterpreterFrame(masm, rbx, rcx);
  __ ret(0);

  __ bind(&stack_check_interrupt);
  // Modify the bytecode offset in the stack to be kFunctionEntryBytecodeOffset
  // for the call to the StackGuard.
  __ Move(Operand(rbp, InterpreterFrameConstants::kBytecodeOffsetFromFp),
          Smi::FromInt(BytecodeArray::kHeaderSize - kHeapObjectTag +
                       kFunctionEntryBytecodeOffset));
  __ CallRuntime(Runtime::kStackGuard);

  // After the call, restore the bytecode array, bytecode offset and accumulator
  // registers again. Also, restore the bytecode offset in the stack to its
  // previous value.
  __ movq(kInterpreterBytecodeArrayRegister,
          Operand(rbp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ Move(kInterpreterBytecodeOffsetRegister,
          BytecodeArray::kHeaderSize - kHeapObjectTag);
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);

  __ SmiTag(rcx, kInterpreterBytecodeArrayRegister);
  __ movq(Operand(rbp, InterpreterFrameConstants::kBytecodeOffsetFromFp), rcx);

  __ jmp(&after_stack_check_interrupt);

  __ bind(&compile_lazy);
  __ GenerateTailCallToReturnedCode(Runtime::kCompileLazy);
  __ int3();  // Should not return.

#ifndef V8_JITLESS
#ifndef V8_ENABLE_LEAPTIERING
  __ bind(&flags_need_processing);
  __ OptimizeCodeOrTailCallOptimizedCodeSlot(feedback_vector, closure,
                                             JumpMode::kJump);
#endif  // !V8_ENABLE_LEAPTIERING

  __ bind(&is_baseline);
  {
#ifndef V8_ENABLE_LEAPTIERING
    // Load the feedback vector from the closure.
    TaggedRegister feedback_cell(feedback_vector);
    __ LoadTaggedField(feedback_cell,
                       FieldOperand(closure, JSFunction::kFeedbackCellOffset));
    __ LoadTaggedField(feedback_vector,
                       FieldOperand(feedback_cell, FeedbackCell::kValueOffset));

    Label install_baseline_code;
    // Check if feedback vector is valid. If not, call prepare for baseline to
    // allocate it.
    __ IsObjectType(feedback_vector, FEEDBACK_VECTOR_TYPE, rcx);
    __ j(not_equal, &install_baseline_code);

    // Check the tiering state.
    __ CheckFeedbackVectorFlagsAndJumpIfNeedsProcessing(
        feedback_vector, CodeKind::BASELINE, &flags_need_processing);

    // TODO(olivf, 42204201): This fastcase is difficult to support with the
    // sandbox as it requires getting write access to the dispatch table. See
    // `JSFunction::UpdateCode`. We might want to remove it for all
    // configurations as it does not seem to be performance sensitive.

    // Load the baseline code into the closure.
    __ Move(rcx, kInterpreterBytecodeArrayRegister);
    static_assert(kJavaScriptCallCodeStartRegister == rcx, "ABI mismatch");
    __ ReplaceClosureCodeWithOptimizedCode(
        rcx, closure, kInterpreterBytecodeArrayRegister,
        WriteBarrierDescriptor::SlotAddressRegister());
    __ JumpCodeObject(rcx, kJSEntrypointTag);

    __ bind(&install_baseline_code);
#endif  // !V8_ENABLE_LEAPTIERING

    __ GenerateTailCallToReturnedCode(Runtime::kInstallBaselineCode);
  }
#endif  // !V8_JITLESS

  __ bind(&stack_overflow);
  __ CallRuntime(Runtime::kThrowStackOverflow);
  __ int3();  // Should not return.
}

static void GenerateInterpreterPushArgs(MacroAssembler* masm, Register num_args,
                                        Register start_address,
                                        Register scratch) {
  ASM_CODE_COMMENT(masm);
  // Find the argument with lowest address.
  __ movq(scratch, num_args);
  __ negq(scratch);
  __ leaq(start_address,
          Operand(start_address, scratch, times_system_pointer_size,
                  kSystemPointerSize));
  // Push the arguments.
  __ PushArray(start_address, num_args, scratch,
               MacroAssembler::PushArrayOrder::kReverse);
}

// static
void Builtins::Generate_InterpreterPushArgsThenCallImpl(
    MacroAssembler* masm, ConvertReceiverMode receiver_mode,
    InterpreterPushArgsMode mode) {
  DCHECK(mode != InterpreterPushArgsMode::kArrayFunction);
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rbx : the address of the first argument to be pushed. Subsequent
  //           arguments should be consecutive above this, in the same order as
  //           they are to be pushed onto the stack.
  //  -- rdi : the target to call (can be any Object).
  // -----------------------------------
  Label stack_overflow;

  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // The spread argument should not be pushed.
    __ decl(rax);
  }

  __ movl(rcx, rax);
  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    __ decl(rcx);  // Exclude receiver.
  }

  // Add a stack check before pushing arguments.
  __ StackOverflowCheck(rcx, &stack_overflow);

  // Pop return address to allow tail-call after pushing arguments.
  __ PopReturnAddressTo(kScratchRegister);

  // rbx and rdx will be modified.
  GenerateInterpreterPushArgs(masm, rcx, rbx, rdx);

  // Push "undefined" as the receiver arg if we need to.
  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    __ PushRoot(RootIndex::kUndefinedValue);
  }

  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // Pass the spread in the register rbx.
    // rbx already points to the penultime argument, the spread
    // is below that.
    __ movq(rbx, Operand(rbx, -kSystemPointerSize));
  }

  // Call the target.
  __ PushReturnAddressFrom(kScratchRegister);  // Re-push return address.

  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    __ TailCallBuiltin(Builtin::kCallWithSpread);
  } else {
    __ TailCallBuiltin(Builtins::Call(receiver_mode));
  }

  // Throw stack overflow exception.
  __ bind(&stack_overflow);
  {
    __ TailCallRuntime(Runtime::kThrowStackOverflow);
    // This should be unreachable.
    __ int3();
  }
}

// static
void Builtins::Generate_InterpreterPushArgsThenConstructImpl(
    MacroAssembler* masm, InterpreterPushArgsMode mode) {
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdx : the new target (either the same as the constructor or
  //           the JSFunction on which new was invoked initially)
  //  -- rdi : the constructor to call (can be any Object)
  //  -- rbx : the allocation site feedback if available, undefined otherwise
  //  -- rcx : the address of the first argument to be pushed. Subsequent
  //           arguments should be consecutive above this, in the same order as
  //           they are to be pushed onto the stack.
  // -----------------------------------
  Label stack_overflow;

  // Add a stack check before pushing arguments.
  __ StackOverflowCheck(rax, &stack_overflow);

  // Pop return address to allow tail-call after pushing arguments.
  __ PopReturnAddressTo(kScratchRegister);

  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // The spread argument should not be pushed.
    __ decl(rax);
  }

  // rcx and r8 will be modified.
  Register argc_without_receiver = r11;
  __ leaq(argc_without_receiver, Operand(rax, -kJSArgcReceiverSlots));
  GenerateInterpreterPushArgs(masm, argc_without_receiver, rcx, r8);

  // Push slot for the receiver to be constructed.
  __ Push(Immediate(0));

  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // Pass the spread in the register rbx.
    __ movq(rbx, Operand(rcx, -kSystemPointerSize));
    // Push return address in preparation for the tail-call.
    __ PushReturnAddressFrom(kScratchRegister);
  } else {
    __ PushReturnAddressFrom(kScratchRegister);
    __ AssertUndefinedOrAllocationSite(rbx);
  }

  if (mode == InterpreterPushArgsMode::kArrayFunction) {
    // Tail call to the array construct stub (still in the caller
    // context at this point).
    __ AssertFunction(rdi);
    // Jump to the constructor function (rax, rbx, rdx passed on).
    __ TailCallBuiltin(Builtin::kArrayConstructorImpl);
  } else if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // Call the constructor (rax, rdx, rdi passed on).
    __ TailCallBuiltin(Builtin::kConstructWithSpread);
  } else {
    DCHECK_EQ(InterpreterPushArgsMode::kOther, mode);
    // Call the constructor (rax, rdx, rdi passed on).
    __ TailCallBuiltin(Builtin::kConstruct);
  }

  // Throw stack overflow exception.
  __ bind(&stack_overflow);
  {
    __ TailCallRuntime(Runtime::kThrowStackOverflow);
    // This should be unreachable.
    __ int3();
  }
}

// static
void Builtins::Generate_ConstructForwardAllArgsImpl(
    MacroAssembler* masm, ForwardWhichFrame which_frame) {
  // ----------- S t a t e -------------
  //  -- rdx : the new target (either the same as the constructor or
  //           the JSFunction on which new was invoked initially)
  //  -- rdi : the constructor to call (can be any Object)
  // -----------------------------------
  Label stack_overflow;

  // Load the frame pointer into rcx.
  switch (which_frame) {
    case ForwardWhichFrame::kCurrentFrame:
      __ movq(rcx, rbp);
      break;
    case ForwardWhichFrame::kParentFrame:
      __ movq(rcx, Operand(rbp, StandardFrameConstants::kCallerFPOffset));
      break;
  }

  // Load the argument count into rax.
  __ movq(rax, Operand(rcx, StandardFrameConstants::kArgCOffset));

  // Add a stack check before copying arguments.
  __ StackOverflowCheck(rax, &stack_overflow);

  // Pop return address to allow tail-call after forwarding arguments.
  __ PopReturnAddressTo(kScratchRegister);

  // Point rcx to the base of the argument list to forward, excluding the
  // receiver.
  __ addq(rcx, Immediate((StandardFrameConstants::kFixedSlotCountAboveFp + 1) *
                         kSystemPointerSize));

  // Copy the arguments on the stack. r8 is a scratch register.
  Register argc_without_receiver = r11;
  __ leaq(argc_without_receiver, Operand(rax, -kJSArgcReceiverSlots));
  __ PushArray(rcx, argc_without_receiver, r8);

  // Push slot for the receiver to be constructed.
  __ Push(Immediate(0));

  __ PushReturnAddressFrom(kScratchRegister);

  // Call the constructor (rax, rdx, rdi passed on).
  __ TailCallBuiltin(Builtin::kConstruct);

  // Throw stack overflow exception.
  __ bind(&stack_overflow);
  {
    __ TailCallRuntime(Runtime::kThrowStackOverflow);
    // This should be unreachable.
    __ int3();
  }
}

namespace {

void NewImplicitReceiver(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdx : the new target
  //  -- rdi : the constructor to call (checked to be a JSFunction)
  //
  //  Stack:
  //  -- Implicit Receiver
  //  -- [arguments without receiver]
  //  -- Implicit Receiver
  //  -- Context
  //  -- FastConstructMarker
  //  -- FramePointer
  // -----------------------------------
  Register implicit_receiver = rcx;

  // Save live registers.
  __ SmiTag(rax);
  __ Push(rax);  // Number of arguments
  __ Push(rdx);  // NewTarget
  __ Push(rdi);  // Target
  __ CallBuiltin(Builtin::kFastNewObject);
  // Save result.
  __ movq(implicit_receiver, rax);
  // Restore live registers.
  __ Pop(rdi);
  __ Pop(rdx);
  __ Pop(rax);
  __ SmiUntagUnsigned(rax);

  // Patch implicit receiver (in arguments)
  __ movq(Operand(rsp, 0 /* first argument */), implicit_receiver);
  // Patch second implicit (in construct frame)
  __ movq(Operand(rbp, FastConstructFrameConstants::kImplicitReceiverOffset),
          implicit_receiver);

  // Restore context.
  __ movq(rsi, Operand(rbp, FastConstructFrameConstants::kContextOffset));
}

}  // namespace

// static
void Builtins::Generate_InterpreterPushArgsThenFastConstructFunction(
    MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdx : the new target
  //  -- rdi : the constructor to call (checked to be a JSFunction)
  //  -- rcx : the address of the first argument to be pushed. Subsequent
  //           arguments should be consecutive above this, in the same order as
  //           they are to be pushed onto the stack.
  // -----------------------------------
  __ AssertFunction(rdi);

  // Check if target has a [[Construct]] internal method.
  Label non_constructor;
  __ LoadMap(kScratchRegister, rdi);
  __ testb(FieldOperand(kScratchRegister, Map::kBitFieldOffset),
           Immediate(Map::Bits1::IsConstructorBit::kMask));
  __ j(zero, &non_constructor);

  // Add a stack check before pushing arguments.
  Label stack_overflow;
  __ StackOverflowCheck(rax, &stack_overflow);

  // Enter a construct frame.
  FrameScope scope(masm, StackFrame::MANUAL);
  __ EnterFrame(StackFrame::FAST_CONSTRUCT);
  __ Push(rsi);
  // Implicit receiver stored in the construct frame.
  __ PushRoot(RootIndex::kTheHoleValue);

  // Push arguments + implicit receiver.
  Register argc_without_receiver = r11;
  __ leaq(argc_without_receiver, Operand(rax, -kJSArgcReceiverSlots));
  GenerateInterpreterPushArgs(masm, argc_without_receiver, rcx, r12);
  // Implicit receiver as part of the arguments (patched later if needed).
  __ PushRoot(RootIndex::kTheHoleValue);

  // Check if it is a builtin call.
  Label builtin_call;
  const TaggedRegister shared_function_info(kScratchRegister);
  __ LoadTaggedField(shared_function_info,
                     FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
  __ testl(FieldOperand(shared_function_info, SharedFunctionInfo::kFlagsOffset),
           Immediate(SharedFunctionInfo::ConstructAsBuiltinBit::kMask));
  __ j(not_zero, &builtin_call);

  // Check if we need to create an implicit receiver.
  Label not_create_implicit_receiver;
  __ movl(kScratchRegister,
          FieldOperand(shared_function_info, SharedFunctionInfo::kFlagsOffset));
  __ DecodeField<SharedFunctionInfo::FunctionKindBits>(kScratchRegister);
  __ JumpIfIsInRange(
      kScratchRegister,
      static_cast<uint32_t>(FunctionKind::kDefaultDerivedConstructor),
      static_cast<uint32_t>(FunctionKind::kDerivedConstructor),
      &not_create_implicit_receiver, Label::kNear);
  NewImplicitReceiver(masm);
  __ bind(&not_create_implicit_receiver);

  // Call the function.
  __ InvokeFunction(rdi, rdx, rax, InvokeType::kCall);

  // ----------- S t a t e -------------
  //  -- rax     constructor result
  //
  //  Stack:
  //  -- Implicit Receiver
  //  -- Context
  //  -- FastConstructMarker
  //  -- FramePointer
  // -----------------------------------

  Label deopt_entry;
  __ bind(&deopt_entry);

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.
  Label use_receiver, do_throw, leave_and_return, check_result;

  // If the result is undefined, we'll use the implicit receiver. Otherwise we
  // do a smi check and fall through to check if the return value is a valid
  // receiver.
  __ JumpIfNotRoot(rax, RootIndex::kUndefinedValue, &check_result,
                   Label::kNear);

  // Throw away the result of the constructor invocation and use the
  // on-stack receiver as the result.
  __ bind(&use_receiver);
  __ movq(rax,
          Operand(rbp, FastConstructFrameConstants::kImplicitReceiverOffset));
  __ JumpIfRoot(rax, RootIndex::kTheHoleValue, &do_throw, Label::kNear);

  __ bind(&leave_and_return);
  __ LeaveFrame(StackFrame::FAST_CONSTRUCT);
  __ ret(0);

  // If the result is a smi, it is *not* an object in the ECMA sense.
  __ bind(&check_result);
  __ JumpIfSmi(rax, &use_receiver, Label::kNear);

  // Check if the type of the result is not an object in the ECMA sense.
  __ JumpIfJSAnyIsNotPrimitive(rax, rcx, &leave_and_return, Label::kNear);
  __ jmp(&use_receiver);

  __ bind(&do_throw);
  __ movq(rsi, Operand(rbp, ConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowConstructorReturnedNonObject);
  // We don't return here.
  __ int3();

  __ bind(&builtin_call);
  // TODO(victorgomes): Check the possibility to turn this into a tailcall.
  __ InvokeFunction(rdi, rdx, rax, InvokeType::kCall);
  __ LeaveFrame(StackFrame::FAST_CONSTRUCT);
  __ ret(0);

  // Called Construct on an Object that doesn't have a [[Construct]] internal
  // method.
  __ bind(&non_constructor);
  __ TailCallBuiltin(Builtin::kConstructedNonConstructable);

  // Throw stack overflow exception.
  __ bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
  // This should be unreachable.
  __ int3();

  Generate_CallToAdaptShadowStackForDeopt(masm, false);
  // Store offset of return address for deoptimizer.
  masm->isolate()->heap()->SetConstructStubInvokeDeoptPCOffset(
      masm->pc_offset());
  __ jmp(&deopt_entry);
}

static void Generate_InterpreterEnterBytecode(MacroAssembler* masm) {
  __ AssertInSandboxedExecutionMode();

  // Set the return address to the correct point in the interpreter entry
  // trampoline.
  Label builtin_trampoline, trampoline_loaded;
  Tagged<Smi> interpreter_entry_return_pc_offset(
      masm->isolate()->heap()->interpreter_entry_return_pc_offset());
  DCHECK_NE(interpreter_entry_return_pc_offset, Smi::zero());

  // If the SFI function_data is an InterpreterData, the function will have a
  // custom copy of the interpreter entry trampoline for profiling. If so,
  // get the custom trampoline, otherwise grab the entry address of the global
  // trampoline.
  __ movq(rbx, Operand(rbp, StandardFrameConstants::kFunctionOffset));
  const Register shared_function_info(rbx);
  __ LoadTaggedField(shared_function_info,
                     FieldOperand(rbx, JSFunction::kSharedFunctionInfoOffset));

  __ LoadTrustedPointerField(
      rbx,
      FieldOperand(shared_function_info,
                   SharedFunctionInfo::kTrustedFunctionDataOffset),
      kUnknownIndirectPointerTag, kScratchRegister);
  __ IsObjectType(rbx, INTERPRETER_DATA_TYPE, kScratchRegister);
  __ j(not_equal, &builtin_trampoline, Label::kNear);
  __ LoadInterpreterDataInterpreterTrampoline(rbx, rbx);
  __ LoadCodeInstructionStart(rbx, rbx, kJSEntrypointTag);
  __ jmp(&trampoline_loaded, Label::kNear);

  __ bind(&builtin_trampoline);
  // TODO(jgruber): Replace this by a lookup in the builtin entry table.
  __ movq(rbx,
          __ ExternalReferenceAsOperand(
              ExternalReference::
                  address_of_interpreter_entry_trampoline_instruction_start(
                      masm->isolate()),
              kScratchRegister));

  __ bind(&trampoline_loaded);
  __ addq(rbx, Immediate(interpreter_entry_return_pc_offset.value()));
  __ movq(kScratchRegister, rbx);

  // Initialize dispatch table register.
  __ Move(
      kInterpreterDispatchTableRegister,
      ExternalReference::interpreter_dispatch_table_address(masm->isolate()));

  // Get the bytecode array pointer from the frame.
  __ movq(kInterpreterBytecodeArrayRegister,
          Operand(rbp, InterpreterFrameConstants::kBytecodeArrayFromFp));

  if (v8_flags.debug_code) {
    // Check function data field is actually a BytecodeArray object.
    __ AssertNotSmi(kInterpreterBytecodeArrayRegister);
    __ IsObjectType(kInterpreterBytecodeArrayRegister, BYTECODE_ARRAY_TYPE,
                    kScratchRegister);
    __ Assert(
        equal,
        AbortReason::kFunctionDataShouldBeBytecodeArrayOnInterpreterEntry);
  }

  // Get the target bytecode offset from the frame.
  __ SmiUntagUnsigned(
      kInterpreterBytecodeOffsetRegister,
      Operand(rbp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  if (v8_flags.debug_code) {
    Label okay;
    __ cmpq(kInterpreterBytecodeOffsetRegister,
            Immediate(BytecodeArray::kHeaderSize - kHeapObjectTag));
    __ j(greater_equal, &okay, Label::kNear);
    __ int3();
    __ bind(&okay);
  }

  // Dispatch to the target bytecode.
  __ movzxbq(kScratchRegister,
             Operand(kInterpreterBytecodeArrayRegister,
                     kInterpreterBytecodeOffsetRegister, times_1, 0));
  __ movq(kJavaScriptCallCodeStartRegister,
          Operand(kInterpreterDispatchTableRegister, kScratchRegister,
                  times_system_pointer_size, 0));

  // Jump to the interpreter entry, and call kJavaScriptCallCodeStartRegister.
  __ jmp(rbx, /*notrack=*/true);
}

void Builtins::Generate_InterpreterEnterAtNextBytecode(MacroAssembler* masm) {
  Generate_CallToAdaptShadowStackForDeopt(masm, true);
  masm->isolate()->heap()->SetDeoptPCOffsetAfterAdaptShadowStack(
      masm->pc_offset());

  // Get bytecode array and bytecode offset from the stack frame.
  __ movq(kInterpreterBytecodeArrayRegister,
          Operand(rbp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ SmiUntagUnsigned(
      kInterpreterBytecodeOffsetRegister,
      Operand(rbp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  Label enter_bytecode, function_entry_bytecode;
  __ cmpq(kInterpreterBytecodeOffsetRegister,
          Immediate(BytecodeArray::kHeaderSize - kHeapObjectTag +
                    kFunctionEntryBytecodeOffset));
  __ j(equal, &function_entry_bytecode);

  // Load the current bytecode.
  __ movzxbq(rbx, Operand(kInterpreterBytecodeArrayRegister,
                          kInterpreterBytecodeOffsetRegister, times_1, 0));

  // Advance to the next bytecode.
  Label if_return;
  AdvanceBytecodeOffsetOrReturn(masm, kInterpreterBytecodeArrayRegister,
                                kInterpreterBytecodeOffsetRegister, rbx, rcx,
                                r8, &if_return);

  __ bind(&enter_bytecode);
  // Convert new bytecode offset to a Smi and save in the stackframe.
  __ SmiTag(kInterpreterBytecodeOffsetRegister);
  __ movq(Operand(rbp, InterpreterFrameConstants::kBytecodeOffsetFromFp),
          kInterpreterBytecodeOffsetRegister);

  Generate_InterpreterEnterBytecode(masm);

  __ bind(&function_entry_bytecode);
  // If the code deoptimizes during the implicit function entry stack interrupt
  // check, it will have a bailout ID of kFunctionEntryBytecodeOffset, which is
  // not a valid bytecode offset. Detect this case and advance to the first
  // actual bytecode.
  __ Move(kInterpreterBytecodeOffsetRegister,
          BytecodeArray::kHeaderSize - kHeapObjectTag);
  __ jmp(&enter_bytecode);

  // We should never take the if_return path.
  __ bind(&if_return);
  __ Abort(AbortReason::kInvalidBytecodeAdvance);
}

void Builtins::Generate_InterpreterEnterAtBytecode(MacroAssembler* masm) {
  Generate_CallToAdaptShadowStackForDeopt(masm, true);
  masm->isolate()->heap()->SetDeoptPCOffsetAfterAdaptShadowStack(
      masm->pc_offset());

  Generate_InterpreterEnterBytecode(masm);
}

// static
void Builtins::Generate_BaselineOutOfLinePrologue(MacroAssembler* masm) {
  Register feedback_cell = r8;
  Register feedback_vector = r9;
  Register return_address = r11;

#ifdef DEBUG
  for (auto reg : BaselineOutOfLinePrologueDescriptor::registers()) {
    DCHECK(!AreAliased(feedback_vector, return_address, reg));
  }
#endif

  auto descriptor =
      Builtins::CallInterfaceDescriptorFor(Builtin::kBaselineOutOfLinePrologue);
  Register closure = descriptor.GetRegisterParameter(
      BaselineOutOfLinePrologueDescriptor::kClosure);
  // Load the feedback cell and vector from the closure.
  __ LoadTaggedField(feedback_cell,
                     FieldOperand(closure, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedField(feedback_vector,
                     FieldOperand(feedback_cell, FeedbackCell::kValueOffset));
  __ AssertFeedbackVector(feedback_vector, kScratchRegister);

#ifndef V8_ENABLE_LEAPTIERING
  // Check the tiering state.
  Label flags_need_processing;
  __ CheckFeedbackVectorFlagsAndJumpIfNeedsProcessing(
      feedback_vector, CodeKind::BASELINE, &flags_need_processing);
#endif  // !V8_ENABLE_LEAPTIERING

  ResetFeedbackVectorOsrUrgency(masm, feedback_vector, kScratchRegister);

  // Increment invocation count for the function.
  __ incl(
      FieldOperand(feedback_vector, FeedbackVector::kInvocationCountOffset));

    // Save the return address, so that we can push it to the end of the newly
    // set-up frame once we're done setting it up.
    __ PopReturnAddressTo(return_address);
    FrameScope frame_scope(masm, StackFrame::MANUAL);
    {
      ASM_CODE_COMMENT_STRING(masm, "Frame Setup");
      __ EnterFrame(StackFrame::BASELINE);

      __ Push(descriptor.GetRegisterParameter(
          BaselineOutOfLinePrologueDescriptor::kCalleeContext));  // Callee's
                                                                  // context.
      Register callee_js_function = descriptor.GetRegisterParameter(
          BaselineOutOfLinePrologueDescriptor::kClosure);
      DCHECK_EQ(callee_js_function, kJavaScriptCallTargetRegister);
      DCHECK_EQ(callee_js_function, kJSFunctionRegister);
      ResetJSFunctionAge(masm, callee_js_function);
      __ Push(callee_js_function);  // Callee's JS function.
      __ Push(descriptor.GetRegisterParameter(
          BaselineOutOfLinePrologueDescriptor::
              kJavaScriptCallArgCount));  // Actual argument count.

      // We'll use the bytecode for both code age/OSR resetting, and pushing
      // onto the frame, so load it into a register.
      Register bytecode_array = descriptor.GetRegisterParameter(
          BaselineOutOfLinePrologueDescriptor::kInterpreterBytecodeArray);
      __ Push(bytecode_array);
      __ Push(feedback_cell);
      __ Push(feedback_vector);
    }

  Register new_target = descriptor.GetRegisterParameter(
      BaselineOutOfLinePrologueDescriptor::kJavaScriptCallNewTarget);

  Label call_stack_guard;
  Register frame_size = descriptor.GetRegisterParameter(
      BaselineOutOfLinePrologueDescriptor::kStackFrameSize);
  {
    ASM_CODE_COMMENT_STRING(masm, " Stack/interrupt check");
    // Stack check. This folds the checks for both the interrupt stack limit
    // check and the real stack limit into one by just checking for the
    // interrupt limit. The interrupt limit is either equal to the real stack
    // limit or tighter. By ensuring we have space until that limit after
    // building the frame we can quickly precheck both at once.
    //
    // TODO(v8:11429): Backport this folded check to the
    // InterpreterEntryTrampoline.
    __ Move(kScratchRegister, rsp);
    DCHECK_NE(frame_size, new_target);
    __ subq(kScratchRegister, frame_size);
    __ cmpq(kScratchRegister,
            __ StackLimitAsOperand(StackLimitKind::kInterruptStackLimit));
    __ j(below, &call_stack_guard);
  }

  // Push the return address back onto the stack for return.
  __ PushReturnAddressFrom(return_address);
  // Return to caller pushed pc, without any frame teardown.
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  __ Ret();

#ifndef V8_ENABLE_LEAPTIERING
  __ bind(&flags_need_processing);
  {
    ASM_CODE_COMMENT_STRING(masm, "Optimized marker check");
    // Drop the return address, rebalancing the return stack buffer by using
    // JumpMode::kPushAndReturn. We can't leave the slot and overwrite it on
    // return since we may do a runtime call along the way that requires the
    // stack to only contain valid frames.
    __ Drop(1);
    __ OptimizeCodeOrTailCallOptimizedCodeSlot(feedback_vector, closure,
                                               JumpMode::kPushAndReturn);
    __ Trap();
  }
#endif  //! V8_ENABLE_LEAPTIERING

  __ bind(&call_stack_guard);
  {
    ASM_CODE_COMMENT_STRING(masm, "Stack/interrupt call");
    {
      // Push the baseline code return address now, as if it had been pushed by
      // the call to this builtin.
      __ PushReturnAddressFrom(return_address);
      FrameScope inner_frame_scope(masm, StackFrame::INTERNAL);
      // Save incoming new target or generator
      __ Push(new_target);
#ifdef V8_JS_LINKAGE_INCLUDES_DISPATCH_HANDLE
      // No need to SmiTag as dispatch handles always look like Smis.
      static_assert(kJSDispatchHandleShift > 0);
      __ AssertSmi(kJavaScriptCallDispatchHandleRegister);
      __ Push(kJavaScriptCallDispatchHandleRegister);
#endif
      __ SmiTag(frame_size);
      __ Push(frame_size);
      __ CallRuntime(Runtime::kStackGuardWithGap, 1);
#ifdef V8_JS_LINKAGE_INCLUDES_DISPATCH_HANDLE
      __ Pop(kJavaScriptCallDispatchHandleRegister);
#endif
      __ Pop(new_target);
    }

    // Return to caller pushed pc, without any frame teardown.
    __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
    __ Ret();
  }
}

// static
void Builtins::Generate_BaselineOutOfLinePrologueDeopt(MacroAssembler* masm) {
  // We're here because we got deopted during BaselineOutOfLinePrologue's stack
  // check. Undo all its frame creation and call into the interpreter instead.

  // Drop feedback vector.
  __ Pop(kScratchRegister);
  // Drop bytecode offset (was the feedback vector but got replaced during
  // deopt).
  __ Pop(kScratchRegister);
  // Drop bytecode array
  __ Pop(kScratchRegister);

  // argc.
  __ Pop(kJavaScriptCallArgCountRegister);
  // Closure.
  __ Pop(kJavaScriptCallTargetRegister);
  // Context.
  __ Pop(kContextRegister);

  // Drop frame pointer
  __ LeaveFrame(StackFrame::BASELINE);

  // Enter the interpreter.
  __ TailCallBuiltin(Builtin::kInterpreterEntryTrampoline);
}

namespace {
void Generate_ContinueToBuiltinHelper(MacroAssembler* masm,
                                      bool javascript_builtin,
                                      bool with_result) {
  Generate_CallToAdaptShadowStackForDeopt(masm, true);
  masm->isolate()->heap()->SetDeoptPCOffsetAfterAdaptShadowStack(
      masm->pc_offset());

  ASM_CODE_COMMENT(masm);
  const RegisterConfiguration* config(RegisterConfiguration::Default());
  int allocatable_register_count = config->num_allocatable_general_registers();
  if (with_result) {
    if (javascript_builtin) {
      // kScratchRegister is not included in the allocateable registers.
      __ movq(kScratchRegister, rax);
    } else {
      // Overwrite the hole inserted by the deoptimizer with the return value
      // from the LAZY deopt point.
      __ movq(
          Operand(rsp, config->num_allocatable_general_registers() *
                               kSystemPointerSize +
                           BuiltinContinuationFrameConstants::kFixedFrameSize),
          rax);
    }
  }
  for (int i = allocatable_register_count - 1; i >= 0; --i) {
    int code = config->GetAllocatableGeneralCode(i);
    __ popq(Register::from_code(code));
    if (javascript_builtin && code == kJavaScriptCallArgCountRegister.code()) {
      __ SmiUntagUnsigned(Register::from_code(code));
    }
  }
  if (with_result && javascript_builtin) {
    // Overwrite the hole inserted by the deoptimizer with the return value from
    // the LAZY deopt point. rax contains the arguments count, the return value
    // from LAZY is always the last argument.
    __ movq(Operand(rsp, rax, times_system_pointer_size,
                    BuiltinContinuationFrameConstants::kFixedFrameSize -
                        kJSArgcReceiverSlots * kSystemPointerSize),
            kScratchRegister);
  }
  __ movq(
      rbp,
      Operand(rsp, BuiltinContinuationFrameConstants::kFixedFrameSizeFromFp));
  const int offsetToPC =
      BuiltinContinuationFrameConstants::kFixedFrameSizeFromFp -
      kSystemPointerSize;
  __ popq(Operand(rsp, offsetToPC));
  __ Drop(offsetToPC / kSystemPointerSize);

  // Replace the builtin index Smi on the stack with the instruction start
  // address of the builtin from the builtins table, and then jump to this
  // address
  __ popq(kScratchRegister);
  __ movq(kScratchRegister,
          __ EntryFromBuiltinIndexAsOperand(kScratchRegister));
  __ jmp(kScratchRegister);
}
}  // namespace

void Builtins::Generate_ContinueToCodeStubBuiltin(MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, false, false);
}

void Builtins::Generate_ContinueToCodeStubBuiltinWithResult(
    MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, false, true);
}

void Builtins::Generate_ContinueToJavaScriptBuiltin(MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, true, false);
}

void Builtins::Generate_ContinueToJavaScriptBuiltinWithResult(
    MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, true, true);
}

void Builtins::Generate_NotifyDeoptimized(MacroAssembler* masm) {
  // Enter an internal frame.
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kNotifyDeoptimized);
    // Tear down internal frame.
  }

  DCHECK_EQ(kInterpreterAccumulatorRegister.code(), rax.code());
  __ movq(rax, Operand(rsp, kPCOnStackSize));
  __ ret(1 * kSystemPointerSize);  // Remove rax.
}

// static
void Builtins::Generate_FunctionPrototypeApply(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax     : argc
  //  -- rsp[0]  : return address
  //  -- rsp[1]  : receiver
  //  -- rsp[2]  : thisArg
  //  -- rsp[3]  : argArray
  // -----------------------------------

  // 1. Load receiver into rdi, argArray into rbx (if present), remove all
  // arguments from the stack (including the receiver), and push thisArg (if
  // present) instead.
  {
    Label no_arg_array, no_this_arg;
    StackArgumentsAccessor args(rax);
    __ LoadRoot(rdx, RootIndex::kUndefinedValue);
    __ movq(rbx, rdx);
    __ movq(rdi, args[0]);
    __ cmpq(rax, Immediate(JSParameterCount(0)));
    __ j(equal, &no_this_arg, Label::kNear);
    {
      __ movq(rdx, args[1]);
      __ cmpq(rax, Immediate(JSParameterCount(1)));
      __ j(equal, &no_arg_array, Label::kNear);
      __ movq(rbx, args[2]);
      __ bind(&no_arg_array);
    }
    __ bind(&no_this_arg);
    __ DropArgumentsAndPushNewReceiver(rax, rdx, rcx);
  }

  // ----------- S t a t e -------------
  //  -- rbx     : argArray
  //  -- rdi     : receiver
  //  -- rsp[0]  : return address
  //  -- rsp[8]  : thisArg
  // -----------------------------------

  // 2. We don't need to check explicitly for callable receiver here,
  // since that's the first thing the Call/CallWithArrayLike builtins
  // will do.

  // 3. Tail call with no arguments if argArray is null or undefined.
  Label no_arguments;
  __ JumpIfRoot(rbx, RootIndex::kNullValue, &no_arguments, Label::kNear);
  __ JumpIfRoot(rbx, RootIndex::kUndefinedValue, &no_arguments, Label::kNear);

  // 4a. Apply the receiver to the given argArray.
  __ TailCallBuiltin(Builtin::kCallWithArrayLike);

  // 4b. The argArray is either null or undefined, so we tail call without any
  // arguments to the receiver. Since we did not create a frame for
  // Function.prototype.apply() yet, we use a normal Call builtin here.
  __ bind(&no_arguments);
  {
    __ Move(rax, JSParameterCount(0));
    __ TailCallBuiltin(Builtins::Call());
  }
}

// static
void Builtins::Generate_FunctionPrototypeCall(MacroAssembler* masm) {
  // Stack Layout:
  // rsp[0]           : Return address
  // rsp[8]           : Argument 0 (receiver: callable to call)
  // rsp[16]          : Argument 1
  //  ...
  // rsp[8 * n]       : Argument n-1
  // rsp[8 * (n + 1)] : Argument n
  // rax contains the number of arguments, n.

  // 1. Get the callable to call (passed as receiver) from the stack.
  {
    StackArgumentsAccessor args(rax);
    __ movq(rdi, args.GetReceiverOperand());
  }

  // 2. Save the return address and drop the callable.
  __ PopReturnAddressTo(rbx);
  __ Pop(kScratchRegister);

  // 3. Make sure we have at least one argument.
  {
    Label done;
    __ cmpq(rax, Immediate(JSParameterCount(0)));
    __ j(greater, &done, Label::kNear);
    __ PushRoot(RootIndex::kUndefinedValue);
    __ incq(rax);
    __ bind(&done);
  }

  // 4. Push back the return address one slot down on the stack (overwriting the
  // original callable), making the original first argument the new receiver.
  __ PushReturnAddressFrom(rbx);
  __ decq(rax);  // One fewer argument (first argument is new receiver).

  // 5. Call the callable.
  // Since we did not create a frame for Function.prototype.call() yet,
  // we use a normal Call builtin here.
  __ TailCallBuiltin(Builtins::Call());
}

void Builtins::Generate_ReflectApply(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax     : argc
  //  -- rsp[0]  : return address
  //  -- rsp[8]  : receiver
  //  -- rsp[16] : target         (if argc >= 1)
  //  -- rsp[24] : thisArgument   (if argc >= 2)
  //  -- rsp[32] : argumentsList  (if argc == 3)
  // -----------------------------------

  // 1. Load target into rdi (if present), argumentsList into rbx (if present),
  // remove all arguments from the stack (including the receiver), and push
  // thisArgument (if present) instead.
  {
    Label done;
    StackArgumentsAccessor args(rax);
    __ LoadRoot(rdi, RootIndex::kUndefinedValue);
    __ movq(rdx, rdi);
    __ movq(rbx, rdi);
    __ cmpq(rax, Immediate(JSParameterCount(1)));
    __ j(below, &done, Label::kNear);
    __ movq(rdi, args[1]);  // target
    __ j(equal, &done, Label::kNear);
    __ movq(rdx, args[2]);  // thisArgument
    __ cmpq(rax, Immediate(JSParameterCount(3)));
    __ j(below, &done, Label::kNear);
    __ movq(rbx, args[3]);  // argumentsList
    __ bind(&done);
    __ DropArgumentsAndPushNewReceiver(rax, rdx, rcx);
  }

  // ----------- S t a t e -------------
  //  -- rbx     : argumentsList
  //  -- rdi     : target
  //  -- rsp[0]  : return address
  //  -- rsp[8]  : thisArgument
  // -----------------------------------

  // 2. We don't need to check explicitly for callable target here,
  // since that's the first thing the Call/CallWithArrayLike builtins
  // will do.

  // 3. Apply the target to the given argumentsList.
  __ TailCallBuiltin(Builtin::kCallWithArrayLike);
}

void Builtins::Generate_ReflectConstruct(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax     : argc
  //  -- rsp[0]  : return address
  //  -- rsp[8]  : receiver
  //  -- rsp[16] : target
  //  -- rsp[24] : argumentsList
  //  -- rsp[32] : new.target (optional)
  // -----------------------------------

  // 1. Load target into rdi (if present), argumentsList into rbx (if present),
  // new.target into rdx (if present, otherwise use target), remove all
  // arguments from the stack (including the receiver), and push thisArgument
  // (if present) instead.
  {
    Label done;
    StackArgumentsAccessor args(rax);
    __ LoadRoot(rdi, RootIndex::kUndefinedValue);
    __ movq(rdx, rdi);
    __ movq(rbx, rdi);
    __ cmpq(rax, Immediate(JSParameterCount(1)));
    __ j(below, &done, Label::kNear);
    __ movq(rdi, args[1]);                     // target
    __ movq(rdx, rdi);                         // new.target defaults to target
    __ j(equal, &done, Label::kNear);
    __ movq(rbx, args[2]);  // argumentsList
    __ cmpq(rax, Immediate(JSParameterCount(3)));
    __ j(below, &done, Label::kNear);
    __ movq(rdx, args[3]);  // new.target
    __ bind(&done);
    __ DropArgumentsAndPushNewReceiver(
        rax, masm->RootAsOperand(RootIndex::kUndefinedValue), rcx);
  }

  // ----------- S t a t e -------------
  //  -- rbx     : argumentsList
  //  -- rdx     : new.target
  //  -- rdi     : target
  //  -- rsp[0]  : return address
  //  -- rsp[8]  : receiver (undefined)
  // -----------------------------------

  // 2. We don't need to check explicitly for constructor target here,
  // since that's the first thing the Construct/ConstructWithArrayLike
  // builtins will do.

  // 3. We don't need to check explicitly for constructor new.target here,
  // since that's the second thing the Construct/ConstructWithArrayLike
  // builtins will do.

  // 4. Construct the target with the given new.target and argumentsList.
  __ TailCallBuiltin(Builtin::kConstructWithArrayLike);
}

namespace {

// Allocate new stack space for |count| arguments and shift all existing
// arguments already on the stack. |pointer_to_new_space_out| points to the
// first free slot on the stack to copy additional arguments to and
// |argc_in_out| is updated to include |count|.
void Generate_AllocateSpaceAndShiftExistingArguments(
    MacroAssembler* masm, Register count, Register argc_in_out,
    Register pointer_to_new_space_out, Register scratch1, Register scratch2) {
  DCHECK(!AreAliased(count, argc_in_out, pointer_to_new_space_out, scratch1,
                     scratch2, kScratchRegister));
  // Use pointer_to_new_space_out as scratch until we set it to the correct
  // value at the end.
  Register old_rsp = pointer_to_new_space_out;
  Register new_space = kScratchRegister;
  __ movq(old_rsp, rsp);

  __ leaq(new_space, Operand(count, times_system_pointer_size, 0));
  __ AllocateStackSpace(new_space);

  Register copy_count = argc_in_out;
  Register current = scratch2;
  Register value = kScratchRegister;

  Label loop, entry;
  __ Move(current, 0);
  __ jmp(&entry);
  __ bind(&loop);
  __ movq(value, Operand(old_rsp, current, times_system_pointer_size, 0));
  __ movq(Operand(rsp, current, times_system_pointer_size, 0), value);
  __ incq(current);
  __ bind(&entry);
  __ cmpq(current, copy_count);
  __ j(less_equal, &loop, Label::kNear);

  // Point to the next free slot above the shifted arguments (copy_count + 1
  // slot for the return address).
  __ leaq(
      pointer_to_new_space_out,
      Operand(rsp, copy_count, times_system_pointer_size, kSystemPointerSize));
  // We use addl instead of addq here because we can omit REX.W, saving 1 byte.
  // We are especially constrained here because we are close to reaching the
  // limit for a near jump to the stackoverflow label, so every byte counts.
  __ addl(argc_in_out, count);  // Update total number of arguments.
}

}  // namespace

// static
// TODO(v8:11615): Observe Code::kMaxArguments in CallOrConstructVarargs
void Builtins::Generate_CallOrConstructVarargs(MacroAssembler* masm,
                                               Builtin target_builtin) {
  // ----------- S t a t e -------------
  //  -- rdi    : target
  //  -- rax    : number of parameters on the stack
  //  -- rbx    : arguments list (a FixedArray)
  //  -- rcx    : len (number of elements to push from args)
  //  -- rdx    : new.target (for [[Construct]])
  //  -- rsp[0] : return address
  // -----------------------------------

  if (v8_flags.debug_code) {
    // Allow rbx to be a FixedArray, or a FixedDoubleArray if rcx == 0.
    Label ok, fail;
    __ AssertNotSmi(rbx);
    Register map = r9;
    __ LoadMap(map, rbx);
    __ CmpInstanceType(map, FIXED_ARRAY_TYPE);
    __ j(equal, &ok);
    __ CmpInstanceType(map, FIXED_DOUBLE_ARRAY_TYPE);
    __ j(not_equal, &fail);
    __ Cmp(rcx, 0);
    __ j(equal, &ok);
    // Fall through.
    __ bind(&fail);
    __ Abort(AbortReason::kOperandIsNotAFixedArray);

    __ bind(&ok);
  }

  Label stack_overflow;
  __ StackOverflowCheck(rcx, &stack_overflow,
                        DEBUG_BOOL ? Label::kFar : Label::kNear);

  // Push additional arguments onto the stack.
  // Move the arguments already in the stack,
  // including the receiver and the return address.
  // rcx: Number of arguments to make room for.
  // rax: Number of arguments already on the stack.
  // r8: Points to first free slot on the stack after arguments were shifted.
  Generate_AllocateSpaceAndShiftExistingArguments(masm, rcx, rax, r8, r9, r12);
  // Copy the additional arguments onto the stack.
  {
    Register value = r12;
    Register src = rbx, dest = r8, num = rcx, current = r9;
    __ Move(current, 0);
    Label done, push, loop;
    __ bind(&loop);
    __ cmpl(current, num);
    __ j(equal, &done, Label::kNear);
    // Turn the hole into undefined as we go.
    __ LoadTaggedField(value, FieldOperand(src, current, times_tagged_size,
                                           OFFSET_OF_DATA_START(FixedArray)));
    __ CompareRoot(value, RootIndex::kTheHoleValue);
    __ j(not_equal, &push, Label::kNear);
    __ LoadRoot(value, RootIndex::kUndefinedValue);
    __ bind(&push);
    __ movq(Operand(dest, current, times_system_pointer_size, 0), value);
    __ incl(current);
    __ jmp(&loop);
    __ bind(&done);
  }

  // Tail-call to the actual Call or Construct builtin.
  __ TailCallBuiltin(target_builtin);

  __ bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
}

// static
void Builtins::Generate_CallOrConstructForwardVarargs(MacroAssembler* masm,
                                                      CallOrConstructMode mode,
                                                      Builtin target_builtin) {
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdx : the new target (for [[Construct]] calls)
  //  -- rdi : the target to call (can be any Object)
  //  -- rcx : start index (to support rest parameters)
  // -----------------------------------

  // Check if new.target has a [[Construct]] internal method.
  if (mode == CallOrConstructMode::kConstruct) {
    Label new_target_constructor, new_target_not_constructor;
    __ JumpIfSmi(rdx, &new_target_not_constructor, Label::kNear);
    __ LoadMap(rbx, rdx);
    __ testb(FieldOperand(rbx, Map::kBitFieldOffset),
             Immediate(Map::Bits1::IsConstructorBit::kMask));
    __ j(not_zero, &new_target_constructor, Label::kNear);
    __ bind(&new_target_not_constructor);
    {
      FrameScope scope(masm, StackFrame::MANUAL);
      __ EnterFrame(StackFrame::INTERNAL);
      __ Push(rdx);
      __ CallRuntime(Runtime::kThrowNotConstructor);
    }
    __ bind(&new_target_constructor);
  }

  Label stack_done, stack_overflow;
  __ movq(r8, Operand(rbp, StandardFrameConstants::kArgCOffset));
  __ decq(r8);  // Exclude receiver.
  __ subl(r8, rcx);
  __ j(less_equal, &stack_done);
  {
    // ----------- S t a t e -------------
    //  -- rax : the number of arguments already in the stack
    //  -- rbp : point to the caller stack frame
    //  -- rcx : start index (to support rest parameters)
    //  -- rdx : the new target (for [[Construct]] calls)
    //  -- rdi : the target to call (can be any Object)
    //  -- r8  : number of arguments to copy, i.e. arguments count - start index
    // -----------------------------------

    // Check for stack overflow.
    __ StackOverflowCheck(r8, &stack_overflow, Label::kNear);

    // Forward the arguments from the caller frame.
    // Move the arguments already in the stack,
    // including the receiver and the return address.
    // r8: Number of arguments to make room for.
    // rax: Number of arguments already on the stack.
    // r9: Points to first free slot on the stack after arguments were shifted.
    Generate_AllocateSpaceAndShiftExistingArguments(masm, r8, rax, r9, r12,
                                                    r15);

    // Point to the first argument to copy (skipping receiver).
    __ leaq(rcx, Operand(rcx, times_system_pointer_size,
                         CommonFrameConstants::kFixedFrameSizeAboveFp +
                             kSystemPointerSize));
    __ addq(rcx, rbp);

    // Copy the additional caller arguments onto the stack.
    // TODO(victorgomes): Consider using forward order as potentially more cache
    // friendly.
    {
      Register src = rcx, dest = r9, num = r8;
      Label loop;
      __ bind(&loop);
      __ decq(num);
      __ movq(kScratchRegister,
              Operand(src, num, times_system_pointer_size, 0));
      __ movq(Operand(dest, num, times_system_pointer_size, 0),
              kScratchRegister);
      __ j(not_zero, &loop);
    }
  }
  __ bind(&stack_done);
  // Tail-call to the actual Call or Construct builtin.
  __ TailCallBuiltin(target_builtin);

  __ bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
}

// static
void Builtins::Generate_CallFunction(MacroAssembler* masm,
                                     ConvertReceiverMode mode) {
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdi : the function to call (checked to be a JSFunction)
  // -----------------------------------

  StackArgumentsAccessor args(rax);
  __ AssertCallableFunction(rdi);

  __ LoadTaggedField(rdx,
                     FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdx : the shared function info.
  //  -- rdi : the function to call (checked to be a JSFunction)
  // -----------------------------------

  // Enter the context of the function; ToObject has to run in the function
  // context, and we also need to take the global proxy from the function
  // context in case of conversion.
  __ LoadTaggedField(rsi, FieldOperand(rdi, JSFunction::kContextOffset));
  // We need to convert the receiver for non-native sloppy mode functions.
  Label done_convert;
  __ testl(FieldOperand(rdx, SharedFunctionInfo::kFlagsOffset),
           Immediate(SharedFunctionInfo::IsNativeBit::kMask |
                     SharedFunctionInfo::IsStrictBit::kMask));
  __ j(not_zero, &done_convert);
  {
    // ----------- S t a t e -------------
    //  -- rax : the number of arguments
    //  -- rdx : the shared function info.
    //  -- rdi : the function to call (checked to be a JSFunction)
    //  -- rsi : the function context.
    // -----------------------------------

    if (mode == ConvertReceiverMode::kNullOrUndefined) {
      // Patch receiver to global proxy.
      __ LoadGlobalProxy(rcx);
    } else {
      Label convert_to_object, convert_receiver;
      __ movq(rcx, args.GetReceiverOperand());
      __ JumpIfSmi(rcx, &convert_to_object,
                   DEBUG_BOOL ? Label::kFar : Label::kNear);
      __ JumpIfJSAnyIsNotPrimitive(rcx, rbx, &done_convert,
                                   DEBUG_BOOL ? Label::kFar : Label::kNear);
      if (mode != ConvertReceiverMode::kNotNullOrUndefined) {
        Label convert_global_proxy;
        __ JumpIfRoot(rcx, RootIndex::kUndefinedValue, &convert_global_proxy,
                      DEBUG_BOOL ? Label::kFar : Label::kNear);
        __ JumpIfNotRoot(rcx, RootIndex::kNullValue, &convert_to_object,
                         DEBUG_BOOL ? Label::kFar : Label::kNear);
        __ bind(&convert_global_proxy);
        {
          // Patch receiver to global proxy.
          __ LoadGlobalProxy(rcx);
        }
        __ jmp(&convert_receiver);
      }
      __ bind(&convert_to_object);
      {
        // Convert receiver using ToObject.
        // TODO(bmeurer): Inline the allocation here to avoid building the frame
        // in the fast case? (fall back to AllocateInNewSpace?)
        FrameScope scope(masm, StackFrame::INTERNAL);
        __ SmiTag(rax);
        __ Push(rax);
        __ Push(rdi);
        __ movq(rax, rcx);
        __ Push(rsi);
        __ CallBuiltin(Builtin::kToObject);
        __ Pop(rsi);
        __ movq(rcx, rax);
        __ Pop(rdi);
        __ Pop(rax);
        __ SmiUntagUnsigned(rax);
      }
      __ LoadTaggedField(
          rdx, FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
      __ bind(&convert_receiver);
    }
    __ movq(args.GetReceiverOperand(), rcx);
  }
  __ bind(&done_convert);

  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdx : the shared function info.
  //  -- rdi : the function to call (checked to be a JSFunction)
  //  -- rsi : the function context.
  // -----------------------------------

#ifdef V8_ENABLE_LEAPTIERING
  __ InvokeFunctionCode(rdi, no_reg, rax, InvokeType::kJump);
#else
  __ movzxwq(
      rbx, FieldOperand(rdx, SharedFunctionInfo::kFormalParameterCountOffset));
  __ InvokeFunctionCode(rdi, no_reg, rbx, rax, InvokeType::kJump);
#endif  // V8_ENABLE_LEAPTIERING
}

namespace {

void Generate_PushBoundArguments(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdx : new.target (only in case of [[Construct]])
  //  -- rdi : target (checked to be a JSBoundFunction)
  // -----------------------------------

  // Load [[BoundArguments]] into rcx and length of that into rbx.
  Label no_bound_arguments;
  __ LoadTaggedField(rcx,
                     FieldOperand(rdi, JSBoundFunction::kBoundArgumentsOffset));
  __ SmiUntagFieldUnsigned(rbx,
                           FieldOperand(rcx, offsetof(FixedArray, length_)));
  __ testl(rbx, rbx);
  __ j(zero, &no_bound_arguments);
  {
    // ----------- S t a t e -------------
    //  -- rax : the number of arguments
    //  -- rdx : new.target (only in case of [[Construct]])
    //  -- rdi : target (checked to be a JSBoundFunction)
    //  -- rcx : the [[BoundArguments]] (implemented as FixedArray)
    //  -- rbx : the number of [[BoundArguments]] (checked to be non-zero)
    // -----------------------------------

    // TODO(victor): Use Generate_StackOverflowCheck here.
    // Check the stack for overflow.
    {
      Label done;
      __ shlq(rbx, Immediate(kSystemPointerSizeLog2));
      __ movq(kScratchRegister, rsp);
      __ subq(kScratchRegister, rbx);

      // We are not trying to catch interruptions (i.e. debug break and
      // preemption) here, so check the "real stack limit".
      __ cmpq(kScratchRegister,
              __ StackLimitAsOperand(StackLimitKind::kRealStackLimit));
      __ j(above_equal, &done, Label::kNear);
      {
        FrameScope scope(masm, StackFrame::MANUAL);
        __ EnterFrame(StackFrame::INTERNAL);
        __ CallRuntime(Runtime::kThrowStackOverflow);
      }
      __ bind(&done);
    }

    // Save Return Address and Receiver into registers.
    __ Pop(r8);
    __ Pop(r10);

    // Push [[BoundArguments]] to the stack.
    {
      Label loop;
      __ LoadTaggedField(
          rcx, FieldOperand(rdi, JSBoundFunction::kBoundArgumentsOffset));
      __ SmiUntagFieldUnsigned(
          rbx, FieldOperand(rcx, offsetof(FixedArray, length_)));
      __ addq(rax, rbx);  // Adjust effective number of arguments.
      __ bind(&loop);
      // Instead of doing decl(rbx) here subtract kTaggedSize from the header
      // offset in order to be able to move decl(rbx) right before the loop
      // condition. This is necessary in order to avoid flags corruption by
      // pointer decompression code.
      __ LoadTaggedField(
          r12, FieldOperand(rcx, rbx, times_tagged_size,
                            OFFSET_OF_DATA_START(FixedArray) - kTaggedSize));
      __ Push(r12);
      __ decl(rbx);
      __ j(greater, &loop);
    }

    // Recover Receiver and Return Address.
    __ Push(r10);
    __ Push(r8);
  }
  __ bind(&no_bound_arguments);
}

}  // namespace

// static
void Builtins::Generate_CallBoundFunctionImpl(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdi : the function to call (checked to be a JSBoundFunction)
  // -----------------------------------
  __ AssertBoundFunction(rdi);

  // Patch the receiver to [[BoundThis]].
  StackArgumentsAccessor args(rax);
  __ LoadTaggedField(rbx, FieldOperand(rdi, JSBoundFunction::kBoundThisOffset));
  __ movq(args.GetReceiverOperand(), rbx);

  // Push the [[BoundArguments]] onto the stack.
  Generate_PushBoundArguments(masm);

  // Call the [[BoundTargetFunction]] via the Call builtin.
  __ LoadTaggedField(
      rdi, FieldOperand(rdi, JSBoundFunction::kBoundTargetFunctionOffset));
  __ TailCallBuiltin(Builtins::Call());
}

// static
void Builtins::Generate_Call(MacroAssembler* masm, ConvertReceiverMode mode) {
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdi : the target to call (can be any Object)
  // -----------------------------------
  Register argc = rax;
  Register target = rdi;
  Register map = rcx;
  Register instance_type = rdx;
  DCHECK(!AreAliased(argc, target, map, instance_type));

  StackArgumentsAccessor args(argc);

  Label non_callable, class_constructor;
  __ JumpIfSmi(target, &non_callable);
  __ LoadMap(map, target);
  __ CmpInstanceTypeRange(map, instance_type, FIRST_CALLABLE_JS_FUNCTION_TYPE,
                          LAST_CALLABLE_JS_FUNCTION_TYPE);
  __ TailCallBuiltin(Builtins::CallFunction(mode), below_equal);

  __ cmpw(instance_type, Immediate(JS_BOUND_FUNCTION_TYPE));
  __ TailCallBuiltin(Builtin::kCallBoundFunction, equal);

  // Check if target has a [[Call]] internal method.
  __ testb(FieldOperand(map, Map::kBitFieldOffset),
           Immediate(Map::Bits1::IsCallableBit::kMask));
  __ j(zero, &non_callable, Label::kNear);

  // Check if target is a proxy and call CallProxy external builtin
  __ cmpw(instance_type, Immediate(JS_PROXY_TYPE));
  __ TailCallBuiltin(Builtin::kCallProxy, equal);

  // Check if target is a wrapped function and call CallWrappedFunction external
  // builtin
  __ cmpw(instance_type, Immediate(JS_WRAPPED_FUNCTION_TYPE));
  __ TailCallBuiltin(Builtin::kCallWrappedFunction, equal);

  // ES6 section 9.2.1 [[Call]] ( thisArgument, argumentsList)
  // Check that the function is not a "classConstructor".
  __ cmpw(instance_type, Immediate(JS_CLASS_CONSTRUCTOR_TYPE));
  __ j(equal, &class_constructor);

  // 2. Call to something else, which might have a [[Call]] internal method (if
  // not we raise an exception).

  // Overwrite the original receiver with the (original) target.
  __ movq(args.GetReceiverOperand(), target);
  // Let the "call_as_function_delegate" take care of the rest.
  __ LoadNativeContextSlot(target, Context::CALL_AS_FUNCTION_DELEGATE_INDEX);
  __ TailCallBuiltin(
      Builtins::CallFunction(ConvertReceiverMode::kNotNullOrUndefined));

  // 3. Call to something that is not callable.
  __ bind(&non_callable);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(target);
    __ CallRuntime(Runtime::kThrowCalledNonCallable);
    __ Trap();  // Unreachable.
  }

  // 4. The function is a "classConstructor", need to raise an exception.
  __ bind(&class_constructor);
  {
    FrameScope frame(masm, StackFrame::INTERNAL);
    __ Push(target);
    __ CallRuntime(Runtime::kThrowConstructorNonCallableError);
    __ Trap();  // Unreachable.
  }
}

// static
void Builtins::Generate_ConstructFunction(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdx : the new target (checked to be a constructor)
  //  -- rdi : the constructor to call (checked to be a JSFunction)
  // -----------------------------------
  __ AssertConstructor(rdi);
  __ AssertFunction(rdi);

  // Calling convention for function specific ConstructStubs require
  // rbx to contain either an AllocationSite or undefined.
  __ LoadRoot(rbx, RootIndex::kUndefinedValue);

  // Jump to JSBuiltinsConstructStub or JSConstructStubGeneric.
  const TaggedRegister shared_function_info(rcx);
  __ LoadTaggedField(shared_function_info,
                     FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
  __ testl(FieldOperand(shared_function_info, SharedFunctionInfo::kFlagsOffset),
           Immediate(SharedFunctionInfo::ConstructAsBuiltinBit::kMask));
  __ TailCallBuiltin(Builtin::kJSBuiltinsConstructStub, not_zero);

  __ TailCallBuiltin(Builtin::kJSConstructStubGeneric);
}

// static
void Builtins::Generate_ConstructBoundFunction(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdx : the new target (checked to be a constructor)
  //  -- rdi : the constructor to call (checked to be a JSBoundFunction)
  // -----------------------------------
  __ AssertConstructor(rdi);
  __ AssertBoundFunction(rdi);

  // Push the [[BoundArguments]] onto the stack.
  Generate_PushBoundArguments(masm);

  // Patch new.target to [[BoundTargetFunction]] if new.target equals target.
  {
    Label done;
    __ cmpq(rdi, rdx);
    __ j(not_equal, &done, Label::kNear);
    __ LoadTaggedField(
        rdx, FieldOperand(rdi, JSBoundFunction::kBoundTargetFunctionOffset));
    __ bind(&done);
  }

  // Construct the [[BoundTargetFunction]] via the Construct builtin.
  __ LoadTaggedField(
      rdi, FieldOperand(rdi, JSBoundFunction::kBoundTargetFunctionOffset));
  __ TailCallBuiltin(Builtin::kConstruct);
}

// static
void Builtins::Generate_Construct(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax : the number of arguments
  //  -- rdx : the new target (either the same as the constructor or
  //           the JSFunction on which new was invoked initially)
  //  -- rdi : the constructor to call (can be any Object)
  // -----------------------------------
  Register argc = rax;
  Register target = rdi;
  Register map = rcx;
  Register instance_type = r8;
  DCHECK(!AreAliased(argc, target, map, instance_type));

  StackArgumentsAccessor args(argc);

  // Check if target is a Smi.
  Label non_constructor;
  __ JumpIfSmi(target, &non_constructor);

  // Check if target has a [[Construct]] internal method.
  __ LoadMap(map, target);
  __ testb(FieldOperand(map, Map::kBitFieldOffset),
           Immediate(Map::Bits1::IsConstructorBit::kMask));
  __ j(zero, &non_constructor);

  // Dispatch based on instance type.
  __ CmpInstanceTypeRange(map, instance_type, FIRST_JS_FUNCTION_TYPE,
                          LAST_JS_FUNCTION_TYPE);
  __ TailCallBuiltin(Builtin::kConstructFunction, below_equal);

  // Only dispatch to bound functions after checking whether they are
  // constructors.
  __ cmpw(instance_type, Immediate(JS_BOUND_FUNCTION_TYPE));
  __ TailCallBuiltin(Builtin::kConstructBoundFunction, equal);

  // Only dispatch to proxies after checking whether they are constructors.
  __ cmpw(instance_type, Immediate(JS_PROXY_TYPE));
  __ TailCallBuiltin(Builtin::kConstructProxy, equal);

  // Called Construct on an exotic Object with a [[Construct]] internal method.
  {
    // Overwrite the original receiver with the (original) target.
    __ movq(args.GetReceiverOperand(), target);
    // Let the "call_as_constructor_delegate" take care of the rest.
    __ LoadNativeContextSlot(target,
                             Context::CALL_AS_CONSTRUCTOR_DELEGATE_INDEX);
    __ TailCallBuiltin(Builtins::CallFunction());
  }

  // Called Construct on an Object that doesn't have a [[Construct]] internal
  // method.
  __ bind(&non_constructor);
  __ TailCallBuiltin(Builtin::kConstructedNonConstructable);
}

namespace {

void Generate_OSREntry(MacroAssembler* masm, Register entry_address) {
  // Drop the return address on the stack and jump to the OSR entry
  // point of the function.
  __ Drop(1);
  // TODO(sroettger): Use the notrack prefix since not all OSR entries emit an
  // endbr instruction yet.
  __ jmp(entry_address, /*notrack=*/true);
}

enum class OsrSourceTier {
  kInterpreter,
  kBaseline,
  kMaglev,
};

void OnStackReplacement(MacroAssembler* masm, OsrSourceTier source,
                        Register maybe_target_code,
                        Register expected_param_count) {
  Label jump_to_optimized_code;
  {
    // If maybe_target_code is not null, no need to call into runtime. A
    // precondition here is: if maybe_target_code is an InstructionStream
    // object, it must NOT be marked_for_deoptimization (callers must ensure
    // this).
    __ testq(maybe_target_code, maybe_target_code);
    __ j(not_equal, &jump_to_optimized_code, Label::kNear);
  }

  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    // Preserve arguments
    __ Push(expected_param_count);
    __ CallRuntime(Runtime::kCompileOptimizedOSR);
    DCHECK_EQ(maybe_target_code, rax);
    __ Pop(expected_param_count);
  }

  // If the code object is null, just return to the caller.
  __ testq(rax, rax);
  __ j(not_equal, &jump_to_optimized_code, Label::kNear);
  __ ret(0);

  __ bind(&jump_to_optimized_code);

  if (source == OsrSourceTier::kMaglev) {
    // Maglev doesn't enter OSR'd code itself, since OSR depends on the
    // unoptimized (~= Ignition) stack frame layout. Instead, return to Maglev
    // code and let it deoptimize.
    __ ret(0);
    return;
  }

  const Register scratch(rcx);
  CHECK(!AreAliased(maybe_target_code, expected_param_count, scratch));

  // OSR entry tracing.
  {
    Label next;
    __ cmpb(
        __ ExternalReferenceAsOperand(
            ExternalReference::address_of_log_or_trace_osr(), kScratchRegister),
        Immediate(0));
    __ j(equal, &next, Label::kNear);

    {
      FrameScope scope(masm, StackFrame::INTERNAL);
      // Preserve arguments
      __ Push(maybe_target_code);
      __ Push(expected_param_count);
      __ CallRuntime(Runtime::kLogOrTraceOptimizedOSREntry, 0);
      __ Pop(expected_param_count);
      __ Pop(maybe_target_code);
    }

    __ bind(&next);
  }

  if (source == OsrSourceTier::kInterpreter) {
    // Drop the handler frame that is be sitting on top of the actual
    // JavaScript frame.
    __ leave();
  }

  // Check we are actually jumping to an OSR code object. This among other
  // things ensures that the object contains deoptimization data below.
  __ movl(scratch, FieldOperand(maybe_target_code, Code::kOsrOffsetOffset));
  __ cmpl(scratch, Immediate(BytecodeOffset::None().ToInt()));
  __ SbxCheck(Condition::not_equal, AbortReason::kExpectedOsrCode);

  // Check the target has a matching parameter count. This ensures that the OSR
  // code will correctly tear down our frame when leaving.
  __ movzxwq(scratch,
             FieldOperand(maybe_target_code, Code::kParameterCountOffset));
  __ SmiUntag(expected_param_count);
  __ cmpq(scratch, expected_param_count);
  __ SbxCheck(Condition::equal, AbortReason::kOsrUnexpectedStackSize);

  __ LoadProtectedPointerField(
      scratch, FieldOperand(maybe_target_code,
                            Code::kDeoptimizationDataOrInterpreterDataOffset));

  // Load the OSR entrypoint offset from the deoptimization data.
  __ SmiUntagField(
      scratch,
      FieldOperand(scratch, TrustedFixedArray::OffsetOfElementAt(
                                DeoptimizationData::kOsrPcOffsetIndex)));

  __ LoadCodeInstructionStart(maybe_target_code, maybe_target_code,
                              kJSEntrypointTag);

  // Compute the target address = code_entry + osr_offset
  __ addq(maybe_target_code, scratch);

  Generate_OSREntry(masm, maybe_target_code);
}

}  // namespace

void Builtins::Generate_InterpreterOnStackReplacement(MacroAssembler* masm) {
  using D = OnStackReplacementDescriptor;
  static_assert(D::kParameterCount == 2);
  OnStackReplacement(masm, OsrSourceTier::kInterpreter,
                     D::MaybeTargetCodeRegister(),
                     D::ExpectedParameterCountRegister());
}

void Builtins::Generate_BaselineOnStackReplacement(MacroAssembler* masm) {
  using D = OnStackReplacementDescriptor;
  static_assert(D::kParameterCount == 2);
  __ movq(kContextRegister,
          MemOperand(rbp, BaselineFrameConstants::kContextOffset));
  OnStackReplacement(masm, OsrSourceTier::kBaseline,
                     D::MaybeTargetCodeRegister(),
                     D::ExpectedParameterCountRegister());
}

#ifdef V8_ENABLE_MAGLEV

// static
void Builtins::Generate_MaglevFunctionEntryStackCheck(MacroAssembler* masm,
                                                      bool save_new_target) {
  // Input (rax): Stack size (Smi).
  // This builtin can be invoked just after Maglev's prologue.
  // All registers are available, except (possibly) new.target.
  ASM_CODE_COMMENT(masm);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ AssertSmi(rax);
    if (save_new_target) {
      if (PointerCompressionIsEnabled()) {
        __ AssertSmiOrHeapObjectInMainCompressionCage(
            kJavaScriptCallNewTargetRegister);
      }
      __ Push(kJavaScriptCallNewTargetRegister);
    }
    __ Push(rax);
    __ CallRuntime(Runtime::kStackGuardWithGap, 1);
    if (save_new_target) {
      __ Pop(kJavaScriptCallNewTargetRegister);
    }
  }
  __ Ret();
}

#endif  // V8_ENABLE_MAGLEV

namespace {

void Generate_RestoreFrameDescriptionRegisters(MacroAssembler* masm,
                                               Register frame_description) {
  // Set the xmm (simd / double) registers.
  const RegisterConfiguration* config = RegisterConfiguration::Default();
  int simd128_regs_offset = FrameDescription::simd128_registers_offset();
  for (int i = 0; i < config->num_allocatable_simd128_registers(); ++i) {
    int code = config->GetAllocatableSimd128Code(i);
    XMMRegister xmm_reg = XMMRegister::from_code(code);
    int src_offset = code * kSimd128Size + simd128_regs_offset;
    __ movdqu(xmm_reg, Operand(frame_description, src_offset));
  }

  // Restore the non-xmm registers from the stack.
  for (int i = Register::kNumRegisters - 1; i >= 0; i--) {
    Register r = Register::from_code(i);
    // Do not restore rsp and kScratchRegister.
    if (r == rsp || r == kScratchRegister) continue;
    __ popq(r);
  }
}

}  // namespace

#ifdef V8_ENABLE_CET_SHADOW_STACK
// AdaptShadowStackForDeopt assists the deoptimizer in getting continuation
// addresses placed on the shadow stack. This can only be done with a call
// instruction. Earlier in the deoptimization process, the user stack was
// seeded with return addresses into the continuations. At this stage, we
// make calls into the continuations such that the shadow stack contains
// precisely those necessary return addresses back into those continuations,
// and in the appropriate order that the shadow stack and the user stack
// perfectly match up at the points where return instructions are executed.
//
// The stack layout on entry to AdaptShadowStackForDeopt is as follows:
//
// ReturnAddress_1
// ReturnAddress_2
// ...
// ReturnAddresss_N
// LastFrameDescription (for restoring registers)
// savedRegister_1
// savedRegister_2
// ...
//
// kAdaptShadowStackCountRegister, on entry, has the value N, matching the
// number of identifiers to pop from the stack above. It is decremented each
// time AdaptShadowStackForDeopt pops a return address from the stack. This
// happens once per invocation of AdaptShadowStackForDeopt. When the value
// is 0, the function jumps to the last return address and will not be called
// again for this deoptimization process.
//
// The other cpu registers have already been populated with the required values
// to kick off execution running the builtin continuation associated with
// ReturnAddress_N on the stack above. AdaptShadowStackForDeopt uses
// kScratchRegister and kAdaptShadowStackRegister for its own work, and
// that is why those registers are additionaly saved on the stack, to be
// restored at the end of the process.

// kAdaptShadowStackDispatchFirstEntryOffset marks the "kick-off" location in
// AdaptShadowStackForDeopt for the process.
constexpr int kAdaptShadowStackDispatchFirstEntryOffset = 1;

// kAdaptShadowStackCountRegister contains the number of identifiers on
// the stack to be consumed via repeated calls into AdaptShadowStackForDeopt.
constexpr Register kAdaptShadowStackCountRegister = r11;

void Builtins::Generate_AdaptShadowStackForDeopt(MacroAssembler* masm) {
  Register count_reg = kAdaptShadowStackCountRegister;
  Register addr = rax;

  // Pop unnecessary return address on stack.
  __ popq(addr);

  // DeoptimizationEntry enters here.
  CHECK_EQ(masm->pc_offset(), kAdaptShadowStackDispatchFirstEntryOffset);

  __ decl(count_reg);
  __ popq(addr);  // Pop the next target address.

  __ pushq(count_reg);
  __ Move(kCArgRegs[0], ExternalReference::isolate_address());
  __ movq(kCArgRegs[1], addr);
  __ PrepareCallCFunction(2);
  {
    AllowExternalCallThatCantCauseGC scope(masm);
    // We should block jumps to arbitrary locations for a security reason.
    // This function will crash if the address is not in the allow list.
    // And, return the given address if it is valid.
    __ CallCFunction(ExternalReference::ensure_valid_return_address(), 2);
  }
  __ popq(count_reg);
  // Now `kReturnRegister0` is the address we want to jump to.

  __ cmpl(count_reg, Immediate(0));
  Label finished;
  __ j(equal, &finished, Label::kNear);
  // This will jump to CallToAdaptShadowStackForDeopt which call back into this
  // function and continue adapting shadow stack.
  __ jmp(kReturnRegister0);

  __ bind(&finished);
  __ movb(__ ExternalReferenceAsOperand(IsolateFieldId::kStackIsIterable),
          Immediate(1));
  __ movq(kScratchRegister, kReturnRegister0);

  __ popq(rbx);  // Restore the last FrameDescription.
  Generate_RestoreFrameDescriptionRegisters(masm, rbx);
  __ jmp(kScratchRegister);
}
#endif  // V8_ENABLE_CET_SHADOW_STACK

#if V8_ENABLE_WEBASSEMBLY

// Returns the offset beyond the last saved FP register.
int SaveWasmParams(MacroAssembler* masm) {
  // Save all parameter registers (see wasm-linkage.h). They might be
  // overwritten in the subsequent runtime call. We don't have any callee-saved
  // registers in wasm, so no need to store anything else.
  static_assert(WasmLiftoffSetupFrameConstants::kNumberOfSavedGpParamRegs + 1 ==
                    arraysize(wasm::kGpParamRegisters),
                "frame size mismatch");
  for (Register reg : wasm::kGpParamRegisters) {
    __ Push(reg);
  }
  static_assert(WasmLiftoffSetupFrameConstants::kNumberOfSavedFpParamRegs ==
                    arraysize(wasm::kFpParamRegisters),
                "frame size mismatch");
  __ AllocateStackSpace(kSimd128Size * arraysize(wasm::kFpParamRegisters));
  int offset = 0;
  for (DoubleRegister reg : wasm::kFpParamRegisters) {
    __ movdqu(Operand(rsp, offset), reg);
    offset += kSimd128Size;
  }
  return offset;
}

// Consumes the offset beyond the last saved FP register (as returned by
// {SaveWasmParams}).
void RestoreWasmParams(MacroAssembler* masm, int offset) {
  for (DoubleRegister reg : base::Reversed(wasm::kFpParamRegisters)) {
    offset -= kSimd128Size;
    __ movdqu(reg, Operand(rsp, offset));
  }
  DCHECK_EQ(0, offset);
  __ addq(rsp, Immediate(kSimd128Size * arraysize(wasm::kFpParamRegisters)));
  for (Register reg : base::Reversed(wasm::kGpParamRegisters)) {
    __ Pop(reg);
  }
}

// When this builtin is called, the topmost stack entry is the calling pc.
// This is replaced with the following:
//
// [    calling pc      ]  <-- rsp; popped by {ret}.
// [  feedback vector   ]
// [ Wasm instance data ]
// [ WASM frame marker  ]
// [    saved rbp       ]  <-- rbp; this is where "calling pc" used to be.
void Builtins::Generate_WasmLiftoffFrameSetup(MacroAssembler* masm) {
  Register func_index = wasm::kLiftoffFrameSetupFunctionReg;
  Register vector = r15;
  Register calling_pc = rdi;

  __ Pop(calling_pc);
  __ Push(rbp);
  __ Move(rbp, rsp);
  __ Push(Immediate(StackFrame::TypeToMarker(StackFrame::WASM)));
  __ LoadTaggedField(
      vector, FieldOperand(kWasmImplicitArgRegister,
                           WasmTrustedInstanceData::kFeedbackVectorsOffset));
  __ LoadTaggedField(vector, FieldOperand(vector, func_index, times_tagged_size,
                                          OFFSET_OF_DATA_START(FixedArray)));
  Label allocate_vector, done;
  __ JumpIfSmi(vector, &allocate_vector);
  __ bind(&done);
  __ Push(kWasmImplicitArgRegister);
  __ Push(vector);
  __ Push(calling_pc);
  __ ret(0);

  __ bind(&allocate_vector);
  // Feedback vector doesn't exist yet. Call the runtime to allocate it.
  // We temporarily change the frame type for this, because we need special
  // handling by the stack walker in case of GC.
  // For the runtime call, we create the following stack layout:
  //
  // [ reserved slot for NativeModule ]  <-- arg[2]
  // [  ("declared") function index   ]  <-- arg[1] for runtime func.
  // [       Wasm instance data       ]  <-- arg[0]
  // [ ...spilled Wasm parameters...  ]
  // [           calling pc           ]
  // [   WASM_LIFTOFF_SETUP marker    ]
  // [           saved rbp            ]
  __ movq(Operand(rbp, TypedFrameConstants::kFrameTypeOffset),
          Immediate(StackFrame::TypeToMarker(StackFrame::WASM_LIFTOFF_SETUP)));
  __ set_has_frame(true);
  __ Push(calling_pc);
  int offset = SaveWasmParams(masm);

  // Arguments to the runtime function: instance data, func_index.
  __ Push(kWasmImplicitArgRegister);
  __ SmiTag(func_index);
  __ Push(func_index);
  // Allocate a stack slot where the runtime function can spill a pointer
  // to the NativeModule.
  __ Push(rsp);
  __ Move(kContextRegister, Smi::zero());
  __ CallRuntime(Runtime::kWasmAllocateFeedbackVector, 3);
  __ movq(vector, kReturnRegister0);

  RestoreWasmParams(masm, offset);
  __ Pop(calling_pc);
  // Restore correct frame type.
  __ movq(Operand(rbp, TypedFrameConstants::kFrameTypeOffset),
          Immediate(StackFrame::TypeToMarker(StackFrame::WASM)));
  __ jmp(&done);
}

void Builtins::Generate_WasmCompileLazy(MacroAssembler* masm) {
  // The function index was pushed to the stack by the caller as int32.
  __ Pop(r15);
  // Convert to Smi for the runtime call.
  __ SmiTag(r15);

  {
    HardAbortScope hard_abort(masm);  // Avoid calls to Abort.
    FrameScope scope(masm, StackFrame::INTERNAL);

    int offset = SaveWasmParams(masm);

    // Push arguments for the runtime function.
    __ Push(kWasmImplicitArgRegister);
    __ Push(r15);
    // Initialize the JavaScript context with 0. CEntry will use it to
    // set the current context on the isolate.
    __ Move(kContextRegister, Smi::zero());
    __ CallRuntime(Runtime::kWasmCompileLazy, 2);
    // The runtime function returns the jump table slot offset as a Smi. Use
    // that to compute the jump target in r15.
    __ SmiUntagUnsigned(kReturnRegister0);
    __ movq(r15, kReturnRegister0);

    RestoreWasmParams(masm, offset);
    // After the instance data register has been restored, we can add the jump
    // table start to the jump table offset already stored in r15.
    __ addq(r15,
            MemOperand(kWasmImplicitArgRegister,
                       wasm::ObjectAccess::ToTagged(
                           WasmTrustedInstanceData::kJumpTableStartOffset)));
  }

  // Finally, jump to the jump table slot for the function.
  __ jmp(r15);
}

void Builtins::Generate_WasmDebugBreak(MacroAssembler* masm) {
  HardAbortScope hard_abort(masm);  // Avoid calls to Abort.
  {
    FrameScope scope(masm, StackFrame::WASM_DEBUG_BREAK);

    // Save all parameter registers. They might hold live values, we restore
    // them after the runtime call.
    for (Register reg :
         base::Reversed(WasmDebugBreakFrameConstants::kPushedGpRegs)) {
      __ Push(reg);
    }

    constexpr int kFpStackSize =
        kSimd128Size * WasmDebugBreakFrameConstants::kNumPushedFpRegisters;
    __ AllocateStackSpace(kFpStackSize);
    int offset = kFpStackSize;
    for (DoubleRegister reg :
         base::Reversed(WasmDebugBreakFrameConstants::kPushedFpRegs)) {
      offset -= kSimd128Size;
      __ movdqu(Operand(rsp, offset), reg);
    }

    // Initialize the JavaScript context with 0. CEntry will use it to
    // set the current context on the isolate.
    __ Move(kContextRegister, Smi::zero());
    __ CallRuntime(Runtime::kWasmDebugBreak, 0);

    // Restore registers.
    for (DoubleRegister reg : WasmDebugBreakFrameConstants::kPushedFpRegs) {
      __ movdqu(reg, Operand(rsp, offset));
      offset += kSimd128Size;
    }
    __ addq(rsp, Immediate(kFpStackSize));
    for (Register reg : WasmDebugBreakFrameConstants::kPushedGpRegs) {
      __ Pop(reg);
    }
  }

  __ ret(0);
}

namespace {
void LoadJumpBuffer(MacroAssembler* masm, Register stack, bool load_pc,
                    wasm::JumpBuffer::StackState expected_state) {
  __ movq(rsp, MemOperand(stack, wasm::kStackSpOffset));
  __ movq(rbp, MemOperand(stack, wasm::kStackFpOffset));
  if (load_pc) {
    __ jmp(MemOperand(stack, wasm::kStackPcOffset));
  }
  // The stack limit is set separately under the ExecutionAccess lock.
}

void LoadTargetJumpBuffer(MacroAssembler* masm, Register target_stack,
                          wasm::JumpBuffer::StackState expected_state) {
  MemOperand GCScanSlotPlace =
      MemOperand(rbp, StackSwitchFrameConstants::kGCScanSlotCountOffset);
  __ Move(GCScanSlotPlace, 0);
  // Switch stack!
  LoadJumpBuffer(masm, target_stack, false, expected_state);
}

// Updates the stack limit and central stack info, and validates the switch.
void SwitchStacks(MacroAssembler* masm, ExternalReference fn,
                  Register old_stack, Label* saved_pc, Register maybe_suspender,
                  const std::initializer_list<Register> keep) {
  for (auto reg : keep) {
    __ Push(reg);
  }
  {
    FrameScope scope(masm, StackFrame::MANUAL);
    DCHECK(old_stack.is_valid());
    __ Move(kCArgRegs[1], old_stack);
    bool is_return = fn == ExternalReference::wasm_return_stack();
    DCHECK_IMPLIES(is_return, maybe_suspender == no_reg);
    int num_args = is_return ? 2 : maybe_suspender.is_valid() ? 6 : 5;
    __ PrepareCallCFunction(num_args);
    if (maybe_suspender.is_valid()) {
      DCHECK_NE(maybe_suspender, kCArgRegs[1]);
#if V8_TARGET_OS_WIN
      __ movq(MemOperand(rsp, 5 * kSystemPointerSize), maybe_suspender);
#else
      __ Move(kCArgRegs[5], maybe_suspender);
#endif
    }
    if (!is_return) {
      __ Move(kCArgRegs[2], rsp);
      __ Move(kCArgRegs[3], rbp);
#if V8_TARGET_OS_WIN
      __ leaq(kScratchRegister, MemOperand(saved_pc, 0));
      __ movq(MemOperand(rsp, 4 * kSystemPointerSize), kScratchRegister);
#else
      __ leaq(kCArgRegs[4], MemOperand(saved_pc, 0));
#endif
    }
    __ Move(kCArgRegs[0], ExternalReference::isolate_address());
    __ CallCFunction(fn, num_args);
  }
  for (auto it = std::rbegin(keep); it != std::rend(keep); ++it) {
    __ Pop(*it);
  }
}

void ReloadParentStack(MacroAssembler* masm, Register promise,
                       Register return_value, Register context, Register tmp1,
                       Register tmp2) {
  Register active_stack = tmp1;
  __ LoadRootRelative(active_stack, IsolateData::active_stack_offset());

  Register parent = tmp2;
  __ Move(parent, MemOperand(active_stack, wasm::kStackParentOffset));
  __ StoreRootRelative(IsolateData::active_stack_offset(), parent);
  // Switch stack!
  SwitchStacks(masm, ExternalReference::wasm_return_stack(), active_stack,
               nullptr, no_reg, {promise, return_value, context, parent});
  LoadJumpBuffer(masm, parent, false, wasm::JumpBuffer::Inactive);
}

// Loads the context field of the WasmTrustedInstanceData or WasmImportData
// depending on the data's type, and places the result in the input register.
void GetContextFromImplicitArg(MacroAssembler* masm, Register data) {
  __ LoadTaggedField(kScratchRegister,
                     FieldOperand(data, HeapObject::kMapOffset));
  __ CmpInstanceType(kScratchRegister, WASM_TRUSTED_INSTANCE_DATA_TYPE);
  Label instance;
  Label end;
  __ j(equal, &instance);
  __ LoadTaggedField(data,
                     FieldOperand(data, WasmImportData::kNativeContextOffset));
  __ jmp(&end);
  __ bind(&instance);
  __ LoadTaggedField(
      data, FieldOperand(data, WasmTrustedInstanceData::kNativeContextOffset));
  __ bind(&end);
}

void RestoreParentSuspender(MacroAssembler* masm, Register tmp1) {
  Register suspender = tmp1;
  __ LoadRootRelative(suspender, IsolateData::active_suspender_offset());
  __ LoadProtectedPointerField(
      suspender, FieldOperand(suspender, WasmSuspenderObject::kParentOffset));
  __ StoreRootRelative(IsolateData::active_suspender_offset(), suspender);
}

void ResetStackSwitchFrameStackSlots(MacroAssembler* masm) {
  __ Move(kScratchRegister, Smi::zero());
  __ movq(MemOperand(rbp, StackSwitchFrameConstants::kImplicitArgOffset),
          kScratchRegister);
  __ movq(MemOperand(rbp, StackSwitchFrameConstants::kResultArrayOffset),
          kScratchRegister);
}

void SwitchToAllocatedStack(MacroAssembler* masm, Register wasm_instance,
                            Register wrapper_buffer, Register original_fp,
                            Register new_wrapper_buffer, Register scratch,
                            Label* suspend) {
  ResetStackSwitchFrameStackSlots(masm);
  Register parent_stack = new_wrapper_buffer;
  __ LoadRootRelative(parent_stack, IsolateData::active_stack_offset());
  __ Move(parent_stack, MemOperand(parent_stack, wasm::kStackParentOffset));
  SwitchStacks(masm, ExternalReference::wasm_start_stack(), parent_stack,
               suspend, no_reg, {kWasmImplicitArgRegister, wrapper_buffer});
  parent_stack = no_reg;
  Register target_stack = scratch;
  __ LoadRootRelative(target_stack, IsolateData::active_stack_offset());
  // Save the old stack's rbp in r9, and use it to access the parameters in
  // the parent frame.
  __ movq(original_fp, rbp);
  LoadTargetJumpBuffer(masm, target_stack, wasm::JumpBuffer::Suspended);
  // Return address slot. The builtin itself returns by switching to the parent
  // jump buffer and does not actually use this slot, but it is read by the
  // profiler.
  __ Push(Immediate(kNullAddress));
  // Push the loaded rbp. We know it is null, because there is no frame yet,
  // so we could also push 0 directly. In any case we need to push it, because
  // this marks the base of the stack segment for the stack frame iterator.
  __ EnterFrame(StackFrame::STACK_SWITCH);
  int stack_space =
      StackSwitchFrameConstants::kNumSpillSlots * kSystemPointerSize +
      JSToWasmWrapperFrameConstants::kWrapperBufferSize;
  __ AllocateStackSpace(stack_space);
  __ movq(new_wrapper_buffer, rsp);
  // Copy data needed for return handling from old wrapper buffer to new one.
  // kWrapperBufferRefReturnCount will be copied too, because 8 bytes are copied
  // at the same time.
  static_assert(JSToWasmWrapperFrameConstants::kWrapperBufferRefReturnCount ==
                JSToWasmWrapperFrameConstants::kWrapperBufferReturnCount + 4);
  __ movq(kScratchRegister,
          MemOperand(wrapper_buffer,
                     JSToWasmWrapperFrameConstants::kWrapperBufferReturnCount));
  __ movq(MemOperand(new_wrapper_buffer,
                     JSToWasmWrapperFrameConstants::kWrapperBufferReturnCount),
          kScratchRegister);
  __ movq(
      kScratchRegister,
      MemOperand(
          wrapper_buffer,
          JSToWasmWrapperFrameConstants::kWrapperBufferSigRepresentationArray));
  __ movq(
      MemOperand(
          new_wrapper_buffer,
          JSToWasmWrapperFrameConstants::kWrapperBufferSigRepresentationArray),
      kScratchRegister);
}

void SwitchBackAndReturnPromise(MacroAssembler* masm, Register tmp1,
                                Register tmp2, wasm::Promise mode,
                                Label* return_promise) {
  // The return value of the wasm function becomes the parameter of the
  // FulfillPromise builtin, and the promise is the return value of this
  // wrapper.
  static const Builtin_FulfillPromise_InterfaceDescriptor desc;
  static_assert(kReturnRegister0 == desc.GetRegisterParameter(0));
  Register promise = desc.GetRegisterParameter(0);
  Register return_value = desc.GetRegisterParameter(1);
  if (mode == wasm::kPromise) {
    __ movq(return_value, kReturnRegister0);
    __ LoadRootRelative(promise, IsolateData::active_suspender_offset());
    __ LoadTaggedField(
        promise, FieldOperand(promise, WasmSuspenderObject::kPromiseOffset));
  }

  __ movq(kContextRegister,
          MemOperand(rbp, StackSwitchFrameConstants::kImplicitArgOffset));
  GetContextFromImplicitArg(masm, kContextRegister);
  ReloadParentStack(masm, promise, return_value, kContextRegister, tmp1, tmp2);
  RestoreParentSuspender(masm, tmp1);

  if (mode == wasm::kPromise) {
    __ Move(MemOperand(rbp, StackSwitchFrameConstants::kGCScanSlotCountOffset),
            1);
    __ Push(promise);
    __ CallBuiltin(Builtin::kFulfillPromise);
    __ Pop(promise);
  }

  __ bind(return_promise);
}

void GenerateExceptionHandlingLandingPad(MacroAssembler* masm,
                                         Label* return_promise) {
  int catch_handler = __ pc_offset();

  __ endbr64();

  // Restore rsp to free the reserved stack slots for the sections.
  __ leaq(rsp, MemOperand(rbp, StackSwitchFrameConstants::kLastSpillOffset));

  // The exception becomes the parameter of the RejectPromise builtin, and the
  // promise is the return value of this wrapper.
  static const Builtin_RejectPromise_InterfaceDescriptor desc;
  Register promise = desc.GetRegisterParameter(0);
  Register reason = desc.GetRegisterParameter(1);
  Register debug_event = desc.GetRegisterParameter(2);
  __ movq(reason, kReturnRegister0);
  __ LoadRootRelative(promise, IsolateData::active_suspender_offset());
  __ LoadTaggedField(
      promise, FieldOperand(promise, WasmSuspenderObject::kPromiseOffset));
  __ movq(kContextRegister,
          MemOperand(rbp, StackSwitchFrameConstants::kImplicitArgOffset));
  GetContextFromImplicitArg(masm, kContextRegister);

  ReloadParentStack(masm, promise, reason, kContextRegister, r8, rdi);
  RestoreParentSuspender(masm, r8);

  __ Move(MemOperand(rbp, StackSwitchFrameConstants::kGCScanSlotCountOffset),
          1);
  __ Push(promise);
  __ LoadRoot(debug_event, RootIndex::kTrueValue);
  __ CallBuiltin(Builtin::kRejectPromise);
  __ Pop(promise);

  // Run the rest of the wrapper normally (switch to the old stack,
  // deconstruct the frame, ...).
  __ jmp(return_promise);

  masm->isolate()->builtins()->SetJSPIPromptHandlerOffset(catch_handler);
}

void JSToWasmWrapperHelper(MacroAssembler* masm, wasm::Promise mode) {
  bool stack_switch = mode == wasm::kPromise || mode == wasm::kStressSwitch;
  __ EnterFrame(stack_switch ? StackFrame::STACK_SWITCH
                             : StackFrame::JS_TO_WASM);

  __ AllocateStackSpace(StackSwitchFrameConstants::kNumSpillSlots *
                        kSystemPointerSize);

  // Load the implicit argument (instance data or import data) from the frame.
  __ movq(kWasmImplicitArgRegister,
          MemOperand(rbp, JSToWasmWrapperFrameConstants::kImplicitArgOffset));

  Register wrapper_buffer =
      WasmJSToWasmWrapperDescriptor::WrapperBufferRegister();
  Register original_fp = stack_switch ? r9 : rbp;
  Register new_wrapper_buffer = stack_switch ? rbx : wrapper_buffer;
  Label suspend;
  if (stack_switch) {
    SwitchToAllocatedStack(masm, kWasmImplicitArgRegister, wrapper_buffer,
                           original_fp, new_wrapper_buffer, rax, &suspend);
  }

  __ movq(MemOperand(rbp, JSToWasmWrapperFrameConstants::kWrapperBufferOffset),
          new_wrapper_buffer);
  if (stack_switch) {
    __ movq(MemOperand(rbp, StackSwitchFrameConstants::kImplicitArgOffset),
            kWasmImplicitArgRegister);
    Register result_array = kScratchRegister;
    __ movq(result_array,
            MemOperand(original_fp,
                       JSToWasmWrapperFrameConstants::kResultArrayParamOffset));
    __ movq(MemOperand(rbp, StackSwitchFrameConstants::kResultArrayOffset),
            result_array);
  }

  Register result_size = rax;
  __ movq(
      result_size,
      MemOperand(
          wrapper_buffer,
          JSToWasmWrapperFrameConstants::kWrapperBufferStackReturnBufferSize));
  __ shlq(result_size, Immediate(kSystemPointerSizeLog2));
  __ subq(rsp, result_size);
  __ movq(
      MemOperand(
          new_wrapper_buffer,
          JSToWasmWrapperFrameConstants::kWrapperBufferStackReturnBufferStart),
      rsp);
  Register call_target = rdi;
  // param_start should not alias with any parameter registers.
  Register params_start = r11;
  __ movq(params_start,
          MemOperand(wrapper_buffer,
                     JSToWasmWrapperFrameConstants::kWrapperBufferParamStart));
  Register params_end = rbx;
  __ movq(params_end,
          MemOperand(wrapper_buffer,
                     JSToWasmWrapperFrameConstants::kWrapperBufferParamEnd));

  __ LoadWasmCodePointer(
      call_target,
      MemOperand(wrapper_buffer,
                 JSToWasmWrapperFrameConstants::kWrapperBufferCallTarget));

  Register last_stack_param = rcx;

  // The first GP parameter is the data, which we handle specially.
  int stack_params_offset =
      (arraysize(wasm::kGpParamRegisters) - 1) * kSystemPointerSize +
      arraysize(wasm::kFpParamRegisters) * kDoubleSize;

  __ leaq(last_stack_param, MemOperand(params_start, stack_params_offset));

  Label loop_start;
  __ bind(&loop_start);

  Label finish_stack_params;
  __ cmpq(last_stack_param, params_end);
  __ j(greater_equal, &finish_stack_params);

  // Push parameter
  __ subq(params_end, Immediate(kSystemPointerSize));
  __ pushq(MemOperand(params_end, 0));
  __ jmp(&loop_start);

  __ bind(&finish_stack_params);

  int next_offset = 0;
  for (size_t i = 1; i < arraysize(wasm::kGpParamRegisters); ++i) {
    // Check that {params_start} does not overlap with any of the parameter
    // registers, so that we don't overwrite it by accident with the loads
    // below.
    DCHECK_NE(params_start, wasm::kGpParamRegisters[i]);
    __ movq(wasm::kGpParamRegisters[i], MemOperand(params_start, next_offset));
    next_offset += kSystemPointerSize;
  }

  for (size_t i = 0; i < arraysize(wasm::kFpParamRegisters); ++i) {
    __ Movsd(wasm::kFpParamRegisters[i], MemOperand(params_start, next_offset));
    next_offset += kDoubleSize;
  }
  DCHECK_EQ(next_offset, stack_params_offset);

  if (stack_switch) {
    __ Move(MemOperand(rbp, StackSwitchFrameConstants::kGCScanSlotCountOffset),
            0);
  }

  // We do the call without a signature check here, since the wrapper loaded the
  // signature from the same trusted object as the call target to set up the
  // stack layout. We could add a signature hash and pass it through to verify
  // it here, but an attacker that could corrupt the signature could also
  // corrupt that signature hash (which is outside of the sandbox).
  __ CallWasmCodePointerNoSignatureCheck(call_target);

  wrapper_buffer = rcx;
  for (size_t i = 0; i < arraysize(wasm::kGpReturnRegisters); ++i) {
    DCHECK_NE(wrapper_buffer, wasm::kGpReturnRegisters[i]);
  }

  __ movq(wrapper_buffer,
          MemOperand(rbp, JSToWasmWrapperFrameConstants::kWrapperBufferOffset));

  __ Movsd(MemOperand(
               wrapper_buffer,
               JSToWasmWrapperFrameConstants::kWrapperBufferFPReturnRegister1),
           wasm::kFpReturnRegisters[0]);
  __ Movsd(MemOperand(
               wrapper_buffer,
               JSToWasmWrapperFrameConstants::kWrapperBufferFPReturnRegister2),
           wasm::kFpReturnRegisters[1]);
  __ movq(MemOperand(
              wrapper_buffer,
              JSToWasmWrapperFrameConstants::kWrapperBufferGPReturnRegister1),
          wasm::kGpReturnRegisters[0]);
  __ movq(MemOperand(
              wrapper_buffer,
              JSToWasmWrapperFrameConstants::kWrapperBufferGPReturnRegister2),
          wasm::kGpReturnRegisters[1]);

  // Call the return value builtin with
  // rax: wasm instance.
  // rbx: the result JSArray for multi-return.
  // rcx: pointer to the byte buffer which contains all parameters.
  if (stack_switch) {
    __ movq(rbx,
            MemOperand(rbp, StackSwitchFrameConstants::kResultArrayOffset));
    __ movq(rax,
            MemOperand(rbp, StackSwitchFrameConstants::kImplicitArgOffset));
  } else {
    __ movq(rbx,
            MemOperand(rbp,
                       JSToWasmWrapperFrameConstants::kResultArrayParamOffset));
    __ movq(rax,
            MemOperand(rbp, JSToWasmWrapperFrameConstants::kImplicitArgOffset));
  }
  GetContextFromImplicitArg(masm, rax);
  __ CallBuiltin(Builtin::kJSToWasmHandleReturns);

  Label return_promise;
  if (stack_switch) {
    SwitchBackAndReturnPromise(masm, r8, rdi, mode, &return_promise);
  }
  __ bind(&suspend);
  __ endbr64();

  __ LeaveFrame(stack_switch ? StackFrame::STACK_SWITCH
                             : StackFrame::JS_TO_WASM);
  __ ret(0);

  // Catch handler for the stack-switching wrapper: reject the promise with the
  // thrown exception.
  if (mode == wasm::kPromise) {
    GenerateExceptionHandlingLandingPad(masm, &return_promise);
  }
}
}  // namespace

void Builtins::Generate_JSToWasmWrapperAsm(MacroAssembler* masm) {
  JSToWasmWrapperHelper(masm, wasm::kNoPromise);
}

void Builtins::Generate_WasmReturnPromiseOnSuspendAsm(MacroAssembler* masm) {
  JSToWasmWrapperHelper(masm, wasm::kPromise);
}

void Builtins::Generate_JSToWasmStressSwitchStacksAsm(MacroAssembler* masm) {
  JSToWasmWrapperHelper(masm, wasm::kStressSwitch);
}

void Builtins::Generate_WasmToJsWrapperAsm(MacroAssembler* masm) {
  // Pop the return address into a scratch register and push it later again. The
  // return address has to be on top of the stack after all registers have been
  // pushed, so that the return instruction can find it.
  __ popq(kScratchRegister);

  int required_stack_space = arraysize(wasm::kFpParamRegisters) * kDoubleSize;
  __ subq(rsp, Immediate(required_stack_space));
  for (int i = 0; i < static_cast<int>(arraysize(wasm::kFpParamRegisters));
       ++i) {
    __ Movsd(MemOperand(rsp, i * kDoubleSize), wasm::kFpParamRegisters[i]);
  }
  // Push the GP registers in reverse order so that they are on the stack like
  // in an array, with the first item being at the lowest address.
  for (size_t i = arraysize(wasm::kGpParamRegisters) - 1; i > 0; --i) {
    __ pushq(wasm::kGpParamRegisters[i]);
  }
  // Signature slot.
  __ pushq(rax);
  // Push the return address again.
  __ pushq(kScratchRegister);
  __ TailCallBuiltin(Builtin::kWasmToJsWrapperCSA);
}

void Builtins::Generate_WasmTrapHandlerLandingPad(MacroAssembler* masm) {
  __ addq(
      kWasmTrapHandlerFaultAddressRegister,
      Immediate(WasmFrameConstants::kProtectedInstructionReturnAddressOffset));
  __ pushq(kWasmTrapHandlerFaultAddressRegister);
#ifdef V8_ENABLE_CET_SHADOW_STACK
  // This landing pad pushes kWasmTrapHandlerFaultAddressRegister to the stack
  // as a return address, allowing `Isolate::UnwindAndFindHandler` to locate it.
  // However, this creates a mismatch in the return address count between the
  // shadow stack and the real stack. To resolve this, we push a dummy value
  // onto the shadow stack to maintain the correct count. This should be used
  // only for unwinding, not for returning.
  Label push_dummy_on_shadow_stack;
  __ call(&push_dummy_on_shadow_stack);
  __ Trap();  // Unreachable.
  __ bind(&push_dummy_on_shadow_stack);
  // Remove the return address pushed onto the real stack.
  __ addq(rsp, Immediate(kSystemPointerSize));
#endif  // V8_ENABLE_CET_SHADOW_STACK
  __ TailCallBuiltin(Builtin::kWasmTrapHandlerThrowTrap);
}

void Builtins::Generate_WasmSuspend(MacroAssembler* masm) {
  // Set up the stackframe.
  __ EnterFrame(StackFrame::STACK_SWITCH);

  Register suspender = rax;

  __ AllocateStackSpace(StackSwitchFrameConstants::kNumSpillSlots *
                        kSystemPointerSize);
  // Set a sentinel value for the spill slots visited by the GC.
  ResetStackSwitchFrameStackSlots(masm);

  // -------------------------------------------
  // Save current state in active jump buffer.
  // -------------------------------------------
  Label resume;
  Register stack = rbx;
  __ LoadRootRelative(stack, IsolateData::active_stack_offset());
  // live: [rax, rbx]

  Register suspender_stack = rdx;
  __ LoadExternalPointerField(
      suspender_stack,
      FieldOperand(suspender, WasmSuspenderObject::kStackOffset),
      kWasmStackMemoryTag, kScratchRegister);
#ifdef DEBUG
  // -------------------------------------------
  // Check that the suspender's stack is the active stack.
  // -------------------------------------------
  // TODO(thibaudm): Once we add core stack-switching instructions, this check
  // will not hold anymore: it's possible that the active stack changed
  // (due to an internal switch), so we have to update the suspender.
  __ cmpq(suspender_stack, stack);
  Label ok;
  __ j(equal, &ok);
  __ Trap();
  __ bind(&ok);
#endif

  // -------------------------------------------
  // Update roots.
  // -------------------------------------------
  Register caller = rcx;
  __ Move(caller, MemOperand(suspender_stack, wasm::kStackParentOffset));
  __ StoreRootRelative(IsolateData::active_stack_offset(), caller);
  Register parent = rdx;
  __ LoadProtectedPointerField(
      parent, FieldOperand(suspender, WasmSuspenderObject::kParentOffset));
  __ StoreRootRelative(IsolateData::active_suspender_offset(), parent);
  parent = no_reg;
  // live: [suspender:rax, stack:rbx, caller:rcx]

  // -------------------------------------------
  // Load jump buffer.
  // -------------------------------------------
  SwitchStacks(masm, ExternalReference::wasm_suspend_stack(), stack, &resume,
               no_reg, {caller, suspender});
  __ LoadTaggedField(
      kReturnRegister0,
      FieldOperand(suspender, WasmSuspenderObject::kPromiseOffset));
  MemOperand GCScanSlotPlace =
      MemOperand(rbp, StackSwitchFrameConstants::kGCScanSlotCountOffset);
  __ Move(GCScanSlotPlace, 0);
  LoadJumpBuffer(masm, caller, true, wasm::JumpBuffer::Inactive);
  __ Trap();
  __ bind(&resume);
  __ endbr64();
  __ LeaveFrame(StackFrame::STACK_SWITCH);
  __ ret(0);
}

namespace {
// Resume the suspender stored in the closure. We generate two variants of this
// builtin: the onFulfilled variant resumes execution at the saved PC and
// forwards the value, the onRejected variant throws the value.

void Generate_WasmResumeHelper(MacroAssembler* masm, wasm::OnResume on_resume) {
  __ EnterFrame(StackFrame::STACK_SWITCH);

  Register param_count = rax;
  __ decq(param_count);                    // Exclude receiver.
  Register closure = kJSFunctionRegister;  // rdi

  __ AllocateStackSpace(StackSwitchFrameConstants::kNumSpillSlots *
                        kSystemPointerSize);
  // Set a sentinel value for the spill slots visited by the GC.
  ResetStackSwitchFrameStackSlots(masm);

  param_count = no_reg;

  // -------------------------------------------
  // Load suspender from closure.
  // -------------------------------------------
  Register sfi = closure;
  __ LoadTaggedField(
      sfi,
      MemOperand(
          closure,
          wasm::ObjectAccess::SharedFunctionInfoOffsetInTaggedJSFunction()));
  Register resume_data = sfi;
  __ LoadTaggedField(
      resume_data,
      FieldOperand(sfi, SharedFunctionInfo::kUntrustedFunctionDataOffset));
  // Already move the suspender in the correct argument register for the C call.
  Register suspender = kCArgRegs[2];
  DCHECK(!AreAliased(suspender, resume_data));
  __ LoadTrustedPointerField(
      suspender,
      FieldOperand(resume_data, WasmResumeData::kTrustedSuspenderOffset),
      kWasmSuspenderIndirectPointerTag, kScratchRegister);
  closure = no_reg;
  sfi = no_reg;

  // -------------------------------------------
  // Save current state.
  // -------------------------------------------
  Label suspend;
  Register active_stack = r9;
  __ LoadRootRelative(active_stack, IsolateData::active_stack_offset());

  // -------------------------------------------
  // Call the C function.
  // -------------------------------------------
  Register target_stack = rbx;
  __ LoadExternalPointerField(
      target_stack, FieldOperand(suspender, WasmSuspenderObject::kStackOffset),
      kWasmStackMemoryTag, kScratchRegister);
  __ StoreRootRelative(IsolateData::active_stack_offset(), target_stack);
  SwitchStacks(masm, ExternalReference::wasm_resume_stack(), active_stack,
               &suspend, suspender, {target_stack});
  suspender = no_reg;

  // -------------------------------------------
  // Load state from target jmpbuf (longjmp).
  // -------------------------------------------
  // Move resolved value to return register.
  __ movq(kReturnRegister0, Operand(rbp, 3 * kSystemPointerSize));
  __ Move(MemOperand(rbp, StackSwitchFrameConstants::kGCScanSlotCountOffset),
          0);
  if (on_resume == wasm::OnResume::kThrow) {
    // Switch to the target stack without restoring the PC.
    LoadJumpBuffer(masm, target_stack, false, wasm::JumpBuffer::Suspended);
    // Pop this frame now. The unwinder expects that the first STACK_SWITCH
    // frame is the outermost one.
    __ LeaveFrame(StackFrame::STACK_SWITCH);
    // Forward the onRejected value to kThrow.
    __ pushq(kReturnRegister0);
    __ Move(kContextRegister, Smi::zero());
    __ CallRuntime(Runtime::kThrow);
  } else {
    // Resume the stack normally.
    LoadJumpBuffer(masm, target_stack, true, wasm::JumpBuffer::Suspended);
  }
  __ Trap();
  __ bind(&suspend);
  __ endbr64();
  __ LeaveFrame(StackFrame::STACK_SWITCH);
  // Pop receiver + parameter.
  __ ret(2 * kSystemPointerSize);
}
}  // namespace

void Builtins::Generate_WasmResume(MacroAssembler* masm) {
  Generate_WasmResumeHelper(masm, wasm::OnResume::kContinue);
}

void Builtins::Generate_WasmReject(MacroAssembler* masm) {
  Generate_WasmResumeHelper(masm, wasm::OnResume::kThrow);
}

void Builtins::Generate_WasmOnStackReplace(MacroAssembler* masm) {
  MemOperand OSRTargetSlot(rbp, -wasm::kOSRTargetOffset);
  __ movq(kScratchRegister, OSRTargetSlot);
  __ Move(OSRTargetSlot, 0);
  __ jmp(kScratchRegister);
}

namespace {
static constexpr Register kOldSPRegister = r12;

void SwitchToTheCentralStackIfNeeded(MacroAssembler* masm,
                                     int r12_stack_slot_index) {
  using ER = ExternalReference;

  // Store r12 value on the stack to restore on exit from the builtin.
  __ movq(ExitFrameStackSlotOperand(r12_stack_slot_index * kSystemPointerSize),
          r12);

  // kOldSPRegister used as a switch flag, if it is zero - no switch performed
  // if it is not zero, it contains old sp value.
  __ Move(kOldSPRegister, 0);

  // Using arg1-2 regs as temporary registers, because they will be rewritten
  // before exiting to native code anyway.
  DCHECK(
      !AreAliased(kCArgRegs[0], kCArgRegs[1], kOldSPRegister, rax, rbx, r15));

  ER on_central_stack_flag = ER::Create(
      IsolateAddressId::kIsOnCentralStackFlagAddress, masm->isolate());

  Label do_not_need_to_switch;
  __ cmpb(__ ExternalReferenceAsOperand(on_central_stack_flag), Immediate(0));
  __ j(not_zero, &do_not_need_to_switch);

  // Perform switching to the central stack.

  __ movq(kOldSPRegister, rsp);

  static constexpr Register argc_input = rax;
  Register central_stack_sp = kCArgRegs[1];
  DCHECK(!AreAliased(central_stack_sp, argc_input));
  {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ pushq(argc_input);

    __ Move(kCArgRegs[0], ER::isolate_address());
    __ Move(kCArgRegs[1], kOldSPRegister);
    __ PrepareCallCFunction(2);
    __ CallCFunction(ER::wasm_switch_to_the_central_stack(), 2,
                     SetIsolateDataSlots::kNo);
    __ movq(central_stack_sp, kReturnRegister0);

    __ popq(argc_input);
  }

  static constexpr int kReturnAddressSlotOffset = 1 * kSystemPointerSize;
  __ subq(central_stack_sp, Immediate(kReturnAddressSlotOffset));
  __ movq(rsp, central_stack_sp);
  // rsp should be aligned by 16 bytes,
  // but it is not guaranteed for stored SP.
  __ AlignStackPointer();

#ifdef V8_TARGET_OS_WIN
  // When we switch stack we leave home space allocated on the old stack.
  // Allocate home space on the central stack to prevent stack corruption.
  __ subq(rsp, Immediate(kWindowsHomeStackSlots * kSystemPointerSize));
#endif  // V8_TARGET_OS_WIN

  // Update the sp saved in the frame.
  // It will be used to calculate the callee pc during GC.
  // The pc is going to be on the new stack segment, so rewrite it here.
  __ movq(Operand(rbp, ExitFrameConstants::kSPOffset), rsp);

  __ bind(&do_not_need_to_switch);
}

void SwitchFromTheCentralStackIfNeeded(MacroAssembler* masm,
                                       int r12_stack_slot_index) {
  using ER = ExternalReference;

  Label no_stack_change;
  __ cmpq(kOldSPRegister, Immediate(0));
  __ j(equal, &no_stack_change);
  __ movq(rsp, kOldSPRegister);

  {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ pushq(kReturnRegister0);
    __ pushq(kReturnRegister1);

    __ Move(kCArgRegs[0], ER::isolate_address());
    __ PrepareCallCFunction(1);
    __ CallCFunction(ER::wasm_switch_from_the_central_stack(), 1,
                     SetIsolateDataSlots::kNo);

    __ popq(kReturnRegister1);
    __ popq(kReturnRegister0);
  }

  __ bind(&no_stack_change);

  // Restore previous value of r12.
  __ movq(r12,
          ExitFrameStackSlotOperand(r12_stack_slot_index * kSystemPointerSize));
}

}  // namespace

#endif  // V8_ENABLE_WEBASSEMBLY

void Builtins::Generate_CEntry(MacroAssembler* masm, int result_size,
                               ArgvMode argv_mode, bool builtin_exit_frame,
                               bool switch_to_central_stack) {
  CHECK(result_size == 1 || result_size == 2);

  using ER = ExternalReference;

  // TODO(422994386): At the moment, all C++ code expects to run unsandboxed.
  // In the future, we should be able to also run C++ builtins and runtime
  // functions in sandboxed execution mode. At that point, we'll either need a
  // dedicated CEntry variants for switching out of sandboxed execution mode or
  // we'll need to perform the mode switch at the start of the CPP function.
  // The latter is only possible if we do not want/need to eventually require
  // going through dedicated trampoline builtins for switching the sandboxing
  // mode, so that needs to be taken into account.
  __ ExitSandbox();
  // This builtins transitions out of sandboxed execution mode.
  __ SetSandboxingModeForCurrentBuiltin(CodeSandboxingMode::kUnsandboxed);

  // rax: number of arguments including receiver
  // rbx: pointer to C function  (C callee-saved)
  // rbp: frame pointer of calling JS frame (restored after C call)
  // rsp: stack pointer  (restored after C call)
  // rsi: current context (restored)
  //
  // If argv_mode == ArgvMode::kRegister:
  // r15: pointer to the first argument

  const int kSwitchToTheCentralStackSlots = switch_to_central_stack ? 1 : 0;
#ifdef V8_TARGET_OS_WIN
  // Windows 64-bit ABI only allows a single-word to be returned in register
  // rax. Larger return sizes must be written to an address passed as a hidden
  // first argument.
  static constexpr int kMaxRegisterResultSize = 1;
  const int kReservedStackSlots = kSwitchToTheCentralStackSlots +
      (result_size <= kMaxRegisterResultSize ? 0 : result_size);
#else
  // Simple results are returned in rax, and a struct of two pointers are
  // returned in rax+rdx.
  static constexpr int kMaxRegisterResultSize = 2;
  const int kReservedStackSlots = kSwitchToTheCentralStackSlots;
  CHECK_LE(result_size, kMaxRegisterResultSize);
#endif  // V8_TARGET_OS_WIN
#if V8_ENABLE_WEBASSEMBLY
  const int kR12SpillSlot = kReservedStackSlots - 1;
#endif  // V8_ENABLE_WEBASSEMBLY

  __ EnterExitFrame(
      kReservedStackSlots,
      builtin_exit_frame ? StackFrame::BUILTIN_EXIT : StackFrame::EXIT, rbx);

  // Set up argv in a callee-saved register. It is reused below so it must be
  // retained across the C call. In case of ArgvMode::kRegister, r15 has
  // already been set by the caller.
  static constexpr Register kArgvRegister = r15;
  if (argv_mode == ArgvMode::kStack) {
    int offset =
        StandardFrameConstants::kFixedFrameSizeAboveFp - kReceiverOnStackSize;
    __ leaq(kArgvRegister,
            Operand(rbp, rax, times_system_pointer_size, offset));
  }

  // rbx: pointer to builtin function  (C callee-saved).
  // rbp: frame pointer of exit frame  (restored after C call).
  // rsp: stack pointer (restored after C call).
  // rax: number of arguments including receiver
  // r15: argv pointer (C callee-saved).

#if V8_ENABLE_WEBASSEMBLY
  if (switch_to_central_stack) {
    SwitchToTheCentralStackIfNeeded(masm, kR12SpillSlot);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  // Check stack alignment.
  if (v8_flags.debug_code) {
    __ CheckStackAlignment();
  }

  // Call C function. The arguments object will be created by stubs declared by
  // DECLARE_RUNTIME_FUNCTION().
  if (result_size <= kMaxRegisterResultSize) {
    // Pass a pointer to the Arguments object as the first argument.
    // Return result in single register (rax), or a register pair (rax, rdx).
    __ movq(kCArgRegs[0], rax);            // argc.
    __ movq(kCArgRegs[1], kArgvRegister);  // argv.
    __ Move(kCArgRegs[2], ER::isolate_address());
  } else {
#ifdef V8_TARGET_OS_WIN
    DCHECK_LE(result_size, 2);
    // Pass a pointer to the result location as the first argument.
    __ leaq(kCArgRegs[0], ExitFrameStackSlotOperand(0 * kSystemPointerSize));
    // Pass a pointer to the Arguments object as the second argument.
    __ movq(kCArgRegs[1], rax);            // argc.
    __ movq(kCArgRegs[2], kArgvRegister);  // argv.
    __ Move(kCArgRegs[3], ER::isolate_address());
#else
    UNREACHABLE();
#endif  // V8_TARGET_OS_WIN
  }
  __ call(rbx);

#ifdef V8_TARGET_OS_WIN
  if (result_size > kMaxRegisterResultSize) {
    // Read result values stored on stack.
    DCHECK_EQ(result_size, 2);
    __ movq(kReturnRegister0,
            ExitFrameStackSlotOperand(0 * kSystemPointerSize));
    __ movq(kReturnRegister1,
            ExitFrameStackSlotOperand(1 * kSystemPointerSize));
  }
#endif  // V8_TARGET_OS_WIN

  // Result is in rax or rdx:rax - do not destroy these registers!

  // Check result for exception sentinel.
  Label exception_returned;
  // The returned value may be a trusted object, living outside of the main
  // pointer compression cage, so we need to use full pointer comparison here.
  __ CompareRoot(rax, RootIndex::kException, ComparisonMode::kFullPointer);
  __ j(equal, &exception_returned);

#if V8_ENABLE_WEBASSEMBLY
  if (switch_to_central_stack) {
    SwitchFromTheCentralStackIfNeeded(masm, kR12SpillSlot);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  // Check that there is no exception, otherwise we
  // should have returned the exception sentinel.
  if (v8_flags.debug_code) {
    Label okay;
    __ LoadRoot(kScratchRegister, RootIndex::kTheHoleValue);
    ER exception_address =
        ER::Create(IsolateAddressId::kExceptionAddress, masm->isolate());
    __ cmp_tagged(kScratchRegister,
                  masm->ExternalReferenceAsOperand(exception_address));
    __ j(equal, &okay, Label::kNear);
    __ int3();
    __ bind(&okay);
  }

  __ LeaveExitFrame();
  if (argv_mode == ArgvMode::kStack) {
    // Drop arguments and the receiver from the caller stack.
    __ PopReturnAddressTo(rcx);
    __ leaq(rsp, Operand(kArgvRegister, kReceiverOnStackSize));
    __ PushReturnAddressFrom(rcx);
  }
  __ EnterSandbox();
  __ ret(0);

  // Handling of exception.
  __ bind(&exception_returned);

  ER pending_handler_context_address = ER::Create(
      IsolateAddressId::kPendingHandlerContextAddress, masm->isolate());
  ER pending_handler_entrypoint_address = ER::Create(
      IsolateAddressId::kPendingHandlerEntrypointAddress, masm->isolate());
  ER pending_handler_fp_address =
      ER::Create(IsolateAddressId::kPendingHandlerFPAddress, masm->isolate());
  ER pending_handler_sp_address =
      ER::Create(IsolateAddressId::kPendingHandlerSPAddress, masm->isolate());

  // Ask the runtime for help to determine the handler. This will set rax to
  // contain the current exception, don't clobber it.
  ER find_handler = ER::Create(Runtime::kUnwindAndFindExceptionHandler);
  {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ Move(kCArgRegs[0], 0);  // argc.
    __ Move(kCArgRegs[1], 0);  // argv.
    __ Move(kCArgRegs[2], ER::isolate_address());
    __ PrepareCallCFunction(3);
    __ CallCFunction(find_handler, 3, SetIsolateDataSlots::kNo);
  }

#ifdef V8_ENABLE_CET_SHADOW_STACK
  // Drop frames from the shadow stack.
  ER num_frames_above_pending_handler_address = ER::Create(
      IsolateAddressId::kNumFramesAbovePendingHandlerAddress, masm->isolate());
  __ movq(rcx, masm->ExternalReferenceAsOperand(
                   num_frames_above_pending_handler_address));
  __ IncsspqIfSupported(rcx, kScratchRegister);
#endif  // V8_ENABLE_CET_SHADOW_STACK

  __ EnterSandbox();

  // Retrieve the handler context, SP and FP.
  __ movq(rsi,
          masm->ExternalReferenceAsOperand(pending_handler_context_address));
  __ movq(rsp, masm->ExternalReferenceAsOperand(pending_handler_sp_address));
  __ movq(rbp, masm->ExternalReferenceAsOperand(pending_handler_fp_address));

  // If the handler is a JS frame, restore the context to the frame. Note that
  // the context will be set to (rsi == 0) for non-JS frames.
  Label skip;
  __ testq(rsi, rsi);
  __ j(zero, &skip, Label::kNear);
  __ movq(Operand(rbp, StandardFrameConstants::kContextOffset), rsi);
  __ bind(&skip);

  // Clear c_entry_fp, like we do in `LeaveExitFrame`.
  ER c_entry_fp_address =
      ER::Create(IsolateAddressId::kCEntryFPAddress, masm->isolate());
  Operand c_entry_fp_operand =
      masm->ExternalReferenceAsOperand(c_entry_fp_address);
  __ movq(c_entry_fp_operand, Immediate(0));

  // Compute the handler entry address and jump to it.
  __ movq(rdi,
          masm->ExternalReferenceAsOperand(pending_handler_entrypoint_address));
  __ jmp(rdi);
}

#if V8_ENABLE_WEBASSEMBLY
void Builtins::Generate_WasmHandleStackOverflow(MacroAssembler* masm) {
  using ER = ExternalReference;
  Register frame_base = WasmHandleStackOverflowDescriptor::FrameBaseRegister();
  Register gap = WasmHandleStackOverflowDescriptor::GapRegister();
  {
    DCHECK_NE(kCArgRegs[1], frame_base);
    DCHECK_NE(kCArgRegs[3], frame_base);
    __ movq(kCArgRegs[3], gap);
    __ movq(kCArgRegs[1], rsp);
    __ movq(kCArgRegs[2], frame_base);
    __ subq(kCArgRegs[2], kCArgRegs[1]);
#ifdef V8_TARGET_OS_WIN
    Register old_fp = rcx;
    // On windows we need preserve rbp value somewhere before entering
    // INTERNAL frame later. It will be placed on the stack as an argument.
    __ movq(old_fp, rbp);
#else
    __ movq(kCArgRegs[4], rbp);
#endif
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ pushq(kCArgRegs[3]);
    __ PrepareCallCFunction(5);
    // On windows put the arguments on the stack (PrepareCallCFunction
    // has created space for this).
#ifdef V8_TARGET_OS_WIN
    __ movq(Operand(rsp, 4 * kSystemPointerSize), old_fp);
#endif
    __ Move(kCArgRegs[0], ER::isolate_address());
    __ CallCFunction(ER::wasm_grow_stack(), 5);
    __ popq(gap);
    DCHECK_NE(kReturnRegister0, gap);
  }
  Label call_runtime;
  // wasm_grow_stack returns zero if it cannot grow a stack.
  __ testq(kReturnRegister0, kReturnRegister0);
  __ j(zero, &call_runtime, Label::kNear);
  // Calculate old FP - SP offset to adjust FP accordingly to new SP.
  __ subq(rbp, rsp);
  __ addq(rbp, kReturnRegister0);
  __ movq(rsp, kReturnRegister0);
  __ movq(kScratchRegister,
          Immediate(StackFrame::TypeToMarker(StackFrame::WASM_SEGMENT_START)));
  __ movq(MemOperand(rbp, TypedFrameConstants::kFrameTypeOffset),
          kScratchRegister);
  __ ret(0);

  // If wasm_grow_stack returns zero, interruption or stack overflow
  // should be handled by runtime call.
  {
    __ bind(&call_runtime);
    __ movq(kWasmImplicitArgRegister,
            MemOperand(rbp, WasmFrameConstants::kWasmInstanceDataOffset));
    __ LoadTaggedField(
        kContextRegister,
        FieldOperand(kWasmImplicitArgRegister,
                     WasmTrustedInstanceData::kNativeContextOffset));
    FrameScope scope(masm, StackFrame::MANUAL);
    __ EnterFrame(StackFrame::INTERNAL);
    __ SmiTag(gap);
    __ pushq(gap);
    __ CallRuntime(Runtime::kWasmStackGuard);
    __ LeaveFrame(StackFrame::INTERNAL);
    __ ret(0);
  }
}
#endif  // V8_ENABLE_WEBASSEMBLY

void Builtins::Generate_DoubleToI(MacroAssembler* masm) {
  Label check_negative, process_64_bits, done;

  // Account for return address and saved regs.
  const int kArgumentOffset = 4 * kSystemPointerSize;

  MemOperand mantissa_operand(MemOperand(rsp, kArgumentOffset));
  MemOperand exponent_operand(
      MemOperand(rsp, kArgumentOffset + kDoubleSize / 2));

  // The result is returned on the stack.
  MemOperand return_operand = mantissa_operand;

  Register scratch1 = rbx;

  // Since we must use rcx for shifts below, use some other register (rax)
  // to calculate the result if ecx is the requested return register.
  Register result_reg = rax;
  // Save ecx if it isn't the return register and therefore volatile, or if it
  // is the return register, then save the temp register we use in its stead
  // for the result.
  Register save_reg = rax;
  __ pushq(rcx);
  __ pushq(scratch1);
  __ pushq(save_reg);

  __ movl(scratch1, mantissa_operand);
  __ Movsd(kScratchDoubleReg, mantissa_operand);
  __ movl(rcx, exponent_operand);

  __ andl(rcx, Immediate(HeapNumber::kExponentMask));
  __ shrl(rcx, Immediate(HeapNumber::kExponentShift));
  __ leal(result_reg, MemOperand(rcx, -HeapNumber::kExponentBias));
  __ cmpl(result_reg, Immediate(HeapNumber::kMantissaBits));
  __ j(below, &process_64_bits, Label::kNear);

  // Result is entirely in lower 32-bits of mantissa
  int delta =
      HeapNumber::kExponentBias + base::Double::kPhysicalSignificandSize;
  __ subl(rcx, Immediate(delta));
  __ xorl(result_reg, result_reg);
  __ cmpl(rcx, Immediate(31));
  __ j(above, &done, Label::kNear);
  __ shll_cl(scratch1);
  __ jmp(&check_negative, Label::kNear);

  __ bind(&process_64_bits);
  __ Cvttsd2siq(result_reg, kScratchDoubleReg);
  __ jmp(&done, Label::kNear);

  // If the double was negative, negate the integer result.
  __ bind(&check_negative);
  __ movl(result_reg, scratch1);
  __ negl(result_reg);
  __ cmpl(exponent_operand, Immediate(0));
  __ cmovl(greater, result_reg, scratch1);

  // Restore registers
  __ bind(&done);
  __ movl(return_operand, result_reg);
  __ popq(save_reg);
  __ popq(scratch1);
  __ popq(rcx);
  __ ret(0);
}

// TODO(jgruber): Instead of explicitly setting up implicit_args_ on the stack
// in CallApiCallback, we could use the calling convention to set up the stack
// correctly in the first place.
//
// TODO(jgruber): I suspect that most of CallApiCallback could be implemented
// as a C++ trampoline, vastly simplifying the assembly implementation.

void Builtins::Generate_CallApiCallbackImpl(MacroAssembler* masm,
                                            CallApiCallbackMode mode) {
  // ----------- S t a t e -------------
  // CallApiCallbackMode::kOptimizedNoProfiling/kOptimized modes:
  //  -- rdx                 : api function address
  // Both modes:
  //  -- rcx                 : arguments count (not including the receiver)
  //  -- rbx                 : FunctionTemplateInfo
  //  -- rsi                 : context
  //  -- rsp[0]              : return address
  //  -- rsp[8]              : argument 0 (receiver)
  //  -- rsp[16]             : argument 1
  //  -- ...
  //  -- rsp[argc * 8]       : argument (argc - 1)
  //  -- rsp[(argc + 1) * 8] : argument argc
  // -----------------------------------

  Register function_callback_info_arg = kCArgRegs[0];

  Register api_function_address = no_reg;
  Register argc = no_reg;
  Register func_templ = no_reg;
  Register topmost_script_having_context = no_reg;
  Register scratch = rax;
  Register scratch2 = no_reg;

  switch (mode) {
    case CallApiCallbackMode::kGeneric:
      scratch2 = r9;
      argc = CallApiCallbackGenericDescriptor::ActualArgumentsCountRegister();
      topmost_script_having_context = CallApiCallbackGenericDescriptor::
          TopmostScriptHavingContextRegister();
      func_templ =
          CallApiCallbackGenericDescriptor::FunctionTemplateInfoRegister();
      break;

    case CallApiCallbackMode::kOptimizedNoProfiling:
    case CallApiCallbackMode::kOptimized:
      // Caller context is always equal to current context because we don't
      // inline Api calls cross-context.
      topmost_script_having_context = kContextRegister;
      api_function_address =
          CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister();
      argc = CallApiCallbackOptimizedDescriptor::ActualArgumentsCountRegister();
      func_templ =
          CallApiCallbackOptimizedDescriptor::FunctionTemplateInfoRegister();
      break;
  }
  DCHECK(!AreAliased(api_function_address, topmost_script_having_context, argc,
                     func_templ, scratch, scratch2, kScratchRegister));

  using FCA = FunctionCallbackArguments;
  using ER = ExternalReference;
  using FC = ApiCallbackExitFrameConstants;

  static_assert(FCA::kArgsLength == 6);
  static_assert(FCA::kNewTargetIndex == 5);
  static_assert(FCA::kTargetIndex == 4);
  static_assert(FCA::kReturnValueIndex == 3);
  static_assert(FCA::kContextIndex == 2);
  static_assert(FCA::kIsolateIndex == 1);
  static_assert(FCA::kUnusedIndex == 0);

  // Set up FunctionCallbackInfo's implicit_args on the stack as follows:
  //
  // Current state:
  //   rsp[0]: return address
  //
  // Target state:
  //   rsp[0 * kSystemPointerSize]: return address
  //   rsp[1 * kSystemPointerSize]: kUnused  <= FCA::implicit_args_
  //   rsp[2 * kSystemPointerSize]: kIsolate
  //   rsp[3 * kSystemPointerSize]: kContext
  //   rsp[4 * kSystemPointerSize]: undefined (kReturnValue)
  //   rsp[5 * kSystemPointerSize]: kTarget
  //   rsp[6 * kSystemPointerSize]: undefined (kNewTarget)
  // Existing state:
  //   rsp[7 * kSystemPointerSize]:          <= FCA:::values_

  __ StoreRootRelative(IsolateData::topmost_script_having_context_offset(),
                       topmost_script_having_context);

  if (mode == CallApiCallbackMode::kGeneric) {
    api_function_address = ReassignRegister(topmost_script_having_context);
  }

  __ PopReturnAddressTo(scratch);
  __ LoadRoot(kScratchRegister, RootIndex::kUndefinedValue);
  __ Push(kScratchRegister);  // kNewTarget
  __ Push(func_templ);        // kTarget
  __ Push(kScratchRegister);  // kReturnValue
  __ Push(kContextRegister);  // kContext
  __ PushAddress(ER::isolate_address());  // kIsolate
  // TODO(ishell, http://crbug.com/326505377): in case of non-constructor
  // call, don't pass kNewTarget and kUnused. Add IsConstructCall flag to
  // kIsolate field.
  __ LoadRoot(kScratchRegister, RootIndex::kUndefinedValue);
  __ Push(kScratchRegister);  // kUnused

  if (mode == CallApiCallbackMode::kGeneric) {
    __ LoadExternalPointerField(
        api_function_address,
        FieldOperand(func_templ,
                     FunctionTemplateInfo::kMaybeRedirectedCallbackOffset),
        kFunctionTemplateInfoCallbackTag, kScratchRegister);
  }

  __ PushReturnAddressFrom(scratch);
  __ EnterExitFrame(FC::getExtraSlotsCountFrom<ExitFrameConstants>(),
                    StackFrame::API_CALLBACK_EXIT, api_function_address);

  Operand argc_operand = Operand(rbp, FC::kFCIArgcOffset);
  {
    ASM_CODE_COMMENT_STRING(masm, "Initialize v8::FunctionCallbackInfo");
    // FunctionCallbackInfo::length_.
    // TODO(ishell): pass JSParameterCount(argc) to simplify things on the
    // caller end.
    __ movq(argc_operand, argc);

    // FunctionCallbackInfo::implicit_args_.
    __ leaq(scratch, Operand(rbp, FC::kImplicitArgsArrayOffset));
    __ movq(Operand(rbp, FC::kFCIImplicitArgsOffset), scratch);

    // FunctionCallbackInfo::values_ (points at JS arguments on the stack).
    __ leaq(scratch, Operand(rbp, FC::kFirstArgumentOffset));
    __ movq(Operand(rbp, FC::kFCIValuesOffset), scratch);
  }

  __ RecordComment("v8::FunctionCallback's argument.");
  __ leaq(function_callback_info_arg,
          Operand(rbp, FC::kFunctionCallbackInfoOffset));

  DCHECK(!AreAliased(api_function_address, function_callback_info_arg));

  ExternalReference thunk_ref = ER::invoke_function_callback(mode);
  Register no_thunk_arg = no_reg;

  Operand return_value_operand = Operand(rbp, FC::kReturnValueOffset);
  static constexpr int kSlotsToDropOnReturn =
      FC::kFunctionCallbackInfoArgsLength + kJSArgcReceiverSlots;

  const bool with_profiling =
      mode != CallApiCallbackMode::kOptimizedNoProfiling;
  CallApiFunctionAndReturn(masm, with_profiling, api_function_address,
                           thunk_ref, no_thunk_arg, kSlotsToDropOnReturn,
                           &argc_operand, return_value_operand);
}

void Builtins::Generate_CallApiGetter(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rsi                 : context
  //  -- rdx                 : receiver
  //  -- rcx                 : holder
  //  -- rbx                 : accessor info
  //  -- rsp[0]              : return address
  // -----------------------------------

  Register name_arg = kCArgRegs[0];
  Register property_callback_info_arg = kCArgRegs[1];

  Register api_function_address = r8;
  Register receiver = ApiGetterDescriptor::ReceiverRegister();
  Register holder = ApiGetterDescriptor::HolderRegister();
  Register callback = ApiGetterDescriptor::CallbackRegister();
  Register scratch = rax;
  Register decompr_scratch1 = COMPRESS_POINTERS_BOOL ? r15 : no_reg;

  DCHECK(!AreAliased(receiver, holder, callback, scratch, decompr_scratch1));

  // Build v8::PropertyCallbackInfo::args_ array on the stack and push property
  // name below the exit frame to make GC aware of them.
  using PCA = PropertyCallbackArguments;
  using ER = ExternalReference;
  using FC = ApiAccessorExitFrameConstants;

  static_assert(PCA::kPropertyKeyIndex == 0);
  static_assert(PCA::kShouldThrowOnErrorIndex == 1);
  static_assert(PCA::kHolderIndex == 2);
  static_assert(PCA::kIsolateIndex == 3);
  static_assert(PCA::kHolderV2Index == 4);
  static_assert(PCA::kReturnValueIndex == 5);
  static_assert(PCA::kDataIndex == 6);
  static_assert(PCA::kThisIndex == 7);
  static_assert(PCA::kArgsLength == 8);

  // Set up v8::PropertyCallbackInfo's (PCI) args_ on the stack as follows:
  // Current state:
  //   rsp[0]: return address
  //
  // Target state:
  //   rsp[0 * kSystemPointerSize]: return address
  //   rsp[1 * kSystemPointerSize]: name                      <= PCI::args_
  //   rsp[2 * kSystemPointerSize]: kShouldThrowOnErrorIndex
  //   rsp[3 * kSystemPointerSize]: kHolderIndex
  //   rsp[4 * kSystemPointerSize]: kIsolateIndex
  //   rsp[5 * kSystemPointerSize]: kHolderV2Index
  //   rsp[6 * kSystemPointerSize]: kReturnValueIndex
  //   rsp[7 * kSystemPointerSize]: kDataIndex
  //   rsp[8 * kSystemPointerSize]: kThisIndex / receiver

  __ PopReturnAddressTo(scratch);
  __ Push(receiver);
  __ PushTaggedField(FieldOperand(callback, AccessorInfo::kDataOffset),
                     decompr_scratch1);
  __ LoadRoot(kScratchRegister, RootIndex::kUndefinedValue);
  __ Push(kScratchRegister);  // return value
  __ Push(Smi::zero());       // holderV2 value
  __ PushAddress(ER::isolate_address());
  __ Push(holder);
  __ Push(Smi::FromInt(kDontThrow));  // should_throw_on_error -> kDontThrow

  // Register name = ReassignRegister(receiver);
  __ LoadTaggedField(name_arg,
                     FieldOperand(callback, AccessorInfo::kNameOffset));
  __ Push(name_arg);

  __ RecordComment("Load api_function_address");
  __ LoadExternalPointerField(
      api_function_address,
      FieldOperand(callback, AccessorInfo::kMaybeRedirectedGetterOffset),
      kAccessorInfoGetterTag, kScratchRegister);

  __ PushReturnAddressFrom(scratch);
  __ EnterExitFrame(FC::getExtraSlotsCountFrom<ExitFrameConstants>(),
                    StackFrame::API_ACCESSOR_EXIT, api_function_address);

  __ RecordComment("Create v8::PropertyCallbackInfo object on the stack.");
  // The context register (rsi) might overlap with property_callback_info_arg
  // but the context value has been saved in EnterExitFrame and thus it could
  // be used to pass arguments.
  // property_callback_info_arg = v8::PropertyCallbackInfo&
  __ leaq(property_callback_info_arg, Operand(rbp, FC::kArgsArrayOffset));

  DCHECK(!AreAliased(api_function_address, property_callback_info_arg, name_arg,
                     callback, scratch));

#ifdef V8_ENABLE_DIRECT_HANDLE
  // name_arg = Local<Name>(name), name value was pushed to GC-ed stack space.
  //__ movq(name_arg, name);
  // |name_arg| is already initialized above.
#else
  // name_arg = Local<Name>(&name), which is &args_array[kPropertyKeyIndex].
  static_assert(PCA::kPropertyKeyIndex == 0);
  __ movq(name_arg, property_callback_info_arg);
#endif

  ExternalReference thunk_ref = ER::invoke_accessor_getter_callback();
  // Pass AccessorInfo to thunk wrapper in case profiler or side-effect
  // checking is enabled.
  Register thunk_arg = callback;

  Operand return_value_operand = Operand(rbp, FC::kReturnValueOffset);
  static constexpr int kSlotsToDropOnReturn =
      FC::kPropertyCallbackInfoArgsLength;
  Operand* const kUseStackSpaceConstant = nullptr;

  const bool with_profiling = true;
  CallApiFunctionAndReturn(masm, with_profiling, api_function_address,
                           thunk_ref, thunk_arg, kSlotsToDropOnReturn,
                           kUseStackSpaceConstant, return_value_operand);
}

void Builtins::Generate_DirectCEntry(MacroAssembler* masm) {
  __ int3();  // Unused on this architecture.
}

namespace {

void Generate_DeoptimizationEntry(MacroAssembler* masm,
                                  DeoptimizeKind deopt_kind) {
  Isolate* isolate = masm->isolate();

  __ ExitSandbox();
  // This builtins transitions out of sandboxed execution mode.
  __ SetSandboxingModeForCurrentBuiltin(CodeSandboxingMode::kUnsandboxed);

  // Save all xmm (simd / double) registers, they will later be copied to the
  // deoptimizer's FrameDescription.
  static constexpr int kXmmRegsSize = kSimd128Size * XMMRegister::kNumRegisters;
  __ AllocateStackSpace(kXmmRegsSize);

  const RegisterConfiguration* config = RegisterConfiguration::Default();
  DCHECK_GE(XMMRegister::kNumRegisters,
            config->num_allocatable_simd128_registers());
  DCHECK_EQ(config->num_allocatable_simd128_registers(),
            config->num_allocatable_double_registers());
  for (int i = 0; i < config->num_allocatable_simd128_registers(); ++i) {
    int code = config->GetAllocatableSimd128Code(i);
    XMMRegister xmm_reg = XMMRegister::from_code(code);
    int offset = code * kSimd128Size;
    __ movdqu(Operand(rsp, offset), xmm_reg);
  }

  // Save all general purpose registers, they will later be copied to the
  // deoptimizer's FrameDescription.
  static constexpr int kNumberOfRegisters = Register::kNumRegisters;
  for (int i = 0; i < kNumberOfRegisters; i++) {
    __ pushq(Register::from_code(i));
  }

  static constexpr int kSavedRegistersAreaSize =
      kNumberOfRegisters * kSystemPointerSize + kXmmRegsSize;
  static constexpr int kCurrentOffsetToReturnAddress = kSavedRegistersAreaSize;
  static constexpr int kCurrentOffsetToParentSP =
      kCurrentOffsetToReturnAddress + kPCOnStackSize;

  __ Store(
      ExternalReference::Create(IsolateAddressId::kCEntryFPAddress, isolate),
      rbp);

  // Get the address of the location in the code object
  // and compute the fp-to-sp delta in register arg5.
  __ movq(kCArgRegs[2], Operand(rsp, kCurrentOffsetToReturnAddress));
  // Load the fp-to-sp-delta.
  __ leaq(kCArgRegs[3], Operand(rsp, kCurrentOffsetToParentSP));
  __ subq(kCArgRegs[3], rbp);
  __ negq(kCArgRegs[3]);

  // Allocate a new deoptimizer object.
  __ PrepareCallCFunction(5);
  __ Move(rax, 0);
  Label context_check;
  __ movq(rdi, Operand(rbp, CommonFrameConstants::kContextOrFrameTypeOffset));
  __ JumpIfSmi(rdi, &context_check);
  __ movq(rax, Operand(rbp, StandardFrameConstants::kFunctionOffset));
  __ bind(&context_check);
  __ movq(kCArgRegs[0], rax);
  __ Move(kCArgRegs[1], static_cast<int>(deopt_kind));
  // Args 3 and 4 are already in the right registers.

  // On windows put the arguments on the stack (PrepareCallCFunction
  // has created space for this). On linux pass the arguments in r8.
#ifdef V8_TARGET_OS_WIN
  Register arg5 = r15;
  __ LoadAddress(arg5, ExternalReference::isolate_address());
  __ movq(Operand(rsp, 4 * kSystemPointerSize), arg5);
#else
  // r8 is kCArgRegs[4] on Linux.
  __ LoadAddress(r8, ExternalReference::isolate_address());
#endif

  {
    AllowExternalCallThatCantCauseGC scope(masm);
    __ CallCFunction(ExternalReference::new_deoptimizer_function(), 5);
  }
  // Preserve deoptimizer object in register rax and get the input
  // frame descriptor pointer.
  __ movq(rbx, Operand(rax, Deoptimizer::input_offset()));

  // Fill in the input registers.
  for (int i = kNumberOfRegisters - 1; i >= 0; i--) {
    int offset =
        (i * kSystemPointerSize) + FrameDescription::registers_offset();
    __ PopQuad(Operand(rbx, offset));
  }

  // Fill in the xmm (simd / double) input registers.
  int simd128_regs_offset = FrameDescription::simd128_registers_offset();
  for (int i = 0; i < XMMRegister::kNumRegisters; i++) {
    int dst_offset = i * kSimd128Size + simd128_regs_offset;
    __ movdqu(kScratchDoubleReg, Operand(rsp, i * kSimd128Size));
    __ movdqu(Operand(rbx, dst_offset), kScratchDoubleReg);
  }
  __ addq(rsp, Immediate(kXmmRegsSize));

  // Mark the stack as not iterable for the CPU profiler which won't be able to
  // walk the stack without the return address.
  __ movb(__ ExternalReferenceAsOperand(IsolateFieldId::kStackIsIterable),
          Immediate(0));

  // Remove the return address from the stack.
  __ addq(rsp, Immediate(kPCOnStackSize));

  // Compute a pointer to the unwinding limit in register rcx; that is
  // the first stack slot not part of the input frame.
  __ movq(rcx, Operand(rbx, FrameDescription::frame_size_offset()));
  __ addq(rcx, rsp);

  // Unwind the stack down to - but not including - the unwinding
  // limit and copy the contents of the activation frame to the input
  // frame description.
  __ leaq(rdx, Operand(rbx, FrameDescription::frame_content_offset()));
  Label pop_loop_header;
  __ jmp(&pop_loop_header);
  Label pop_loop;
  __ bind(&pop_loop);
  __ Pop(Operand(rdx, 0));
  __ addq(rdx, Immediate(sizeof(intptr_t)));
  __ bind(&pop_loop_header);
  __ cmpq(rcx, rsp);
  __ j(not_equal, &pop_loop);

  // Compute the output frame in the deoptimizer.
  __ pushq(rax);
  __ PrepareCallCFunction(2);
  __ movq(kCArgRegs[0], rax);
  __ LoadAddress(kCArgRegs[1], ExternalReference::isolate_address());
  {
    AllowExternalCallThatCantCauseGC scope(masm);
    __ CallCFunction(ExternalReference::compute_output_frames_function(), 2);
  }
  __ popq(rax);
#ifdef V8_ENABLE_CET_SHADOW_STACK
  __ movq(r8, rax);
#endif  // V8_ENABLE_CET_SHADOW_STACK

  __ movq(rsp, Operand(rax, Deoptimizer::caller_frame_top_offset()));

  // Replace the current (input) frame with the output frames.
  Label outer_push_loop, inner_push_loop, outer_loop_header, inner_loop_header;
  // Outer loop state: rax = current FrameDescription**, rdx = one past the
  // last FrameDescription**.
  __ movl(rdx, Operand(rax, Deoptimizer::output_count_offset()));
  __ movq(rax, Operand(rax, Deoptimizer::output_offset()));
  __ leaq(rdx, Operand(rax, rdx, times_system_pointer_size, 0));
  __ jmp(&outer_loop_header);
  __ bind(&outer_push_loop);
  // Inner loop state: rbx = current FrameDescription*, rcx = loop index.
  __ movq(rbx, Operand(rax, 0));
  __ movq(rcx, Operand(rbx, FrameDescription::frame_size_offset()));
  __ jmp(&inner_loop_header);
  __ bind(&inner_push_loop);
  __ subq(rcx, Immediate(sizeof(intptr_t)));
  __ Push(Operand(rbx, rcx, times_1, FrameDescription::frame_content_offset()));
  __ bind(&inner_loop_header);
  __ testq(rcx, rcx);
  __ j(not_zero, &inner_push_loop);
  __ addq(rax, Immediate(kSystemPointerSize));
  __ bind(&outer_loop_header);
  __ cmpq(rax, rdx);
  __ j(below, &outer_push_loop);

  // Push pc and continuation from the last output frame.
  __ PushQuad(Operand(rbx, FrameDescription::pc_offset()));
  __ movq(rax, Operand(rbx, FrameDescription::continuation_offset()));
  // Skip pushing the continuation if it is zero. This is used as a marker for
  // wasm deopts that do not use a builtin call to finish the deopt.
  Label push_registers;
  __ testq(rax, rax);
  __ j(zero, &push_registers);
  __ Push(rax);
  __ bind(&push_registers);
  // Push the registers from the last output frame.
  for (int i = 0; i < kNumberOfRegisters; i++) {
    Register r = Register::from_code(i);
    // Do not restore rsp and kScratchRegister.
    if (r == rsp || r == kScratchRegister) continue;
    int offset =
        (i * kSystemPointerSize) + FrameDescription::registers_offset();
    __ PushQuad(Operand(rbx, offset));
  }

#ifdef V8_ENABLE_CET_SHADOW_STACK
  // Check v8_flags.cet_compatible.
  Label shadow_stack_push;
  __ cmpb(__ ExternalReferenceAsOperand(
              ExternalReference::address_of_cet_compatible_flag(),
              kScratchRegister),
          Immediate(0));
  __ j(not_equal, &shadow_stack_push);
#endif  // V8_ENABLE_CET_SHADOW_STACK

  Generate_RestoreFrameDescriptionRegisters(masm, rbx);

  __ movb(__ ExternalReferenceAsOperand(IsolateFieldId::kStackIsIterable),
          Immediate(1));

  __ EnterSandbox();

  // Return to the continuation point.
  __ ret(0);

#ifdef V8_ENABLE_CET_SHADOW_STACK
  // Push candidate return addresses for shadow stack onto the stack.
  __ bind(&shadow_stack_push);

  // push the last FrameDescription onto the stack for restoring xmm registers
  // later.
  __ pushq(rbx);

  // r8 = deoptimizer
  __ movl(kAdaptShadowStackCountRegister,
          Operand(r8, Deoptimizer::shadow_stack_count_offset()));
  __ movq(rax, Operand(r8, Deoptimizer::shadow_stack_offset()));

  Label check_more_pushes, next_push;
  __ Move(kScratchRegister, 0);
  __ jmp(&check_more_pushes, Label::kNear);
  __ bind(&next_push);
  // rax points to the start of the shadow stack array.
  __ pushq(Operand(rax, kScratchRegister, times_system_pointer_size, 0));
  __ incl(kScratchRegister);
  __ bind(&check_more_pushes);
  __ cmpl(kScratchRegister, kAdaptShadowStackCountRegister);
  __ j(not_equal, &next_push);

  // We drop 1 word from the shadow stack. It contains the return address from
  // DeoptimizationEntry.
  __ Move(rax, 1);
  __ IncsspqIfSupported(rax, kScratchRegister);

  // Now, kick off the process of getting our continuations onto the shadow
  // stack. Note that the stack has 2 extra words to be popped at the end
  // of the process:
  // 1) the kAdaptShadowStackCountRegister
  // 2) kScratchRegister
  __ movq(kScratchRegister,
          Operand(kRootRegister, IsolateData::BuiltinEntrySlotOffset(
                                     Builtin::kAdaptShadowStackForDeopt)));
  // We don't enter at the start of AdaptShadowStackForDeopt, because that
  // is designed to be called by builtin continuations in order to get
  // return addresses into those continuations on the stack. Therefore, we
  // have to make a special entry at kAdaptShadowStackDispatchFirstEntryOffset.
  __ addq(kScratchRegister,
          Immediate(kAdaptShadowStackDispatchFirstEntryOffset));
  __ jmp(kScratchRegister);

  __ int3();
#endif  // V8_ENABLE_CET_SHADOW_STACK
}

}  // namespace

void Builtins::Generate_DeoptimizationEntry_Eager(MacroAssembler* masm) {
  Generate_DeoptimizationEntry(masm, DeoptimizeKind::kEager);
}

void Builtins::Generate_DeoptimizationEntry_Lazy(MacroAssembler* masm) {
  Generate_DeoptimizationEntry(masm, DeoptimizeKind::kLazy);
}

void Builtins::Generate_DeoptimizationEntry_LazyAfterFastCall(
    MacroAssembler* masm) {
  // We get here directly after returning from a call to an API function from
  // generated code. As such, we'll have left sandboxed execution mode for the
  // call but haven't yet entered it again. Do so now.
  __ EnterSandbox();

  // The deoptimizer may have been triggered right after the return of a fast
  // API call. In that case, exception handling and possible stack unwinding
  // did not happen yet.
  // We check here if a there is a pending exception by comparing the
  // exception stored in the isolate with the no-exception sentinel
  // (the_hole_value). If there is an exception, we call PropagateException to
  // trigger stack unwinding.
  Label no_exception;
  __ movq(rax, __ ExternalReferenceAsOperand(ExternalReference::Create(
                   IsolateAddressId::kExceptionAddress, __ isolate())));
  __ CompareRoot(rax, RootIndex::kTheHoleValue);

  __ j(equal, &no_exception);

  __ EnterFrame(StackFrame::INTERNAL);
  const RegList kCalleeSaveRegisters = {C_CALL_CALLEE_SAVE_REGISTERS};
  const DoubleRegList kCalleeSaveFPRegisters = {
      C_CALL_CALLEE_SAVE_FP_REGISTERS};
  __ PushAll(kCalleeSaveFPRegisters);
  __ PushAll(kCalleeSaveRegisters);
  __ Move(kContextRegister, Context::kNoContext);
  // We have to reset IsolateData::fast_c_call_caller_fp(), because otherwise
  // the  stack unwinder thinks that we are still within the fast C call.
  if (v8_flags.debug_code) {
    __ movq(rax,
            __ ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
    __ testq(rax, rax);
    __ Assert(not_equal, AbortReason::kFastCallFallbackInvalid);
  }
  __ movq(__ ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP),
          Immediate(0));
  __ CallRuntime(Runtime::FunctionId::kPropagateException);
  __ PopAll(kCalleeSaveRegisters);
  __ PopAll(kCalleeSaveFPRegisters);
  __ LeaveFrame(StackFrame::INTERNAL);

  __ bind(&no_exception);
  // LINT.IfChange(DeoptAfterFastCallSetReturnValue)
  // Deoptimization expects that the return value of the API call is in the
  // return register. As we only allow deoptimization if the return type is
  // void, the return value is always `undefined`.
  // TODO(crbug.com/418936518): Handle the return value in an actual
  // deoptimization continuation.
  __ LoadRoot(kReturnRegister0, RootIndex::kUndefinedValue);
  // LINT.ThenChange()
  __ TailCallBuiltin(Builtin::kDeoptimizationEntry_Lazy);
}

// If there is baseline code on the shared function info, converts an
// interpreter frame into a baseline frame and continues execution in baseline
// code. Otherwise execution continues with bytecode.
void Builtins::Generate_InterpreterOnStackReplacement_ToBaseline(
    MacroAssembler* masm) {
  Label start;
  __ bind(&start);

  // Get function from the frame.
  Register closure = rdi;
  __ movq(closure, MemOperand(rbp, StandardFrameConstants::kFunctionOffset));

  // Get the InstructionStream object from the shared function info.
  Register code_obj = rbx;
  Register shared_function_info(code_obj);
  __ LoadTaggedField(
      shared_function_info,
      FieldOperand(closure, JSFunction::kSharedFunctionInfoOffset));

  ResetSharedFunctionInfoAge(masm, shared_function_info);

  __ LoadTrustedPointerField(
      code_obj,
      FieldOperand(shared_function_info,
                   SharedFunctionInfo::kTrustedFunctionDataOffset),
      kCodeIndirectPointerTag, kScratchRegister);

  // For OSR entry it is safe to assume we always have baseline code.
  if (v8_flags.debug_code) {
    __ IsObjectType(code_obj, CODE_TYPE, kScratchRegister);
    __ Assert(equal, AbortReason::kExpectedBaselineData);
    AssertCodeIsBaseline(masm, code_obj, r11);
  }

  // Load the feedback cell and feedback vector.
  Register feedback_cell = r8;
  Register feedback_vector = r11;
  __ LoadTaggedField(feedback_cell,
                     FieldOperand(closure, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedField(feedback_vector,
                     FieldOperand(feedback_cell, FeedbackCell::kValueOffset));

  Label install_baseline_code;
  // Check if feedback vector is valid. If not, call prepare for baseline to
  // allocate it.
  __ IsObjectType(feedback_vector, FEEDBACK_VECTOR_TYPE, kScratchRegister);
  __ j(not_equal, &install_baseline_code);

  // Save bytecode offset from the stack frame.
  __ SmiUntagUnsigned(
      kInterpreterBytecodeOffsetRegister,
      MemOperand(rbp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  // Replace bytecode offset with feedback cell.
  static_assert(InterpreterFrameConstants::kBytecodeOffsetFromFp ==
                BaselineFrameConstants::kFeedbackCellFromFp);
  __ movq(MemOperand(rbp, BaselineFrameConstants::kFeedbackCellFromFp),
          feedback_cell);
  feedback_cell = no_reg;
  // Update feedback vector cache.
  static_assert(InterpreterFrameConstants::kFeedbackVectorFromFp ==
                BaselineFrameConstants::kFeedbackVectorFromFp);
  __ movq(MemOperand(rbp, InterpreterFrameConstants::kFeedbackVectorFromFp),
          feedback_vector);
  feedback_vector = no_reg;

  // Compute baseline pc for bytecode offset.
  Register get_baseline_pc = r11;
  __ LoadAddress(get_baseline_pc,
                 ExternalReference::baseline_pc_for_next_executed_bytecode());

  __ subq(kInterpreterBytecodeOffsetRegister,
          Immediate(BytecodeArray::kHeaderSize - kHeapObjectTag));

  // Get bytecode array from the stack frame.
  __ movq(kInterpreterBytecodeArrayRegister,
          MemOperand(rbp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ pushq(kInterpreterAccumulatorRegister);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ PrepareCallCFunction(3);
    __ movq(kCArgRegs[0], code_obj);
    __ movq(kCArgRegs[1], kInterpreterBytecodeOffsetRegister);
    __ movq(kCArgRegs[2], kInterpreterBytecodeArrayRegister);
    __ CallCFunction(get_baseline_pc, 3);
  }
  __ LoadCodeInstructionStart(code_obj, code_obj, kJSEntrypointTag);
  __ addq(code_obj, kReturnRegister0);
  __ popq(kInterpreterAccumulatorRegister);

  Generate_OSREntry(masm, code_obj);
  __ Trap();  // Unreachable.

  __ bind(&install_baseline_code);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ pushq(kInterpreterAccumulatorRegister);
    __ Push(closure);
    __ CallRuntime(Runtime::kInstallBaselineCode, 1);
    __ popq(kInterpreterAccumulatorRegister);
  }
  // Retry from the start after installing baseline code.
  __ jmp(&start);
}

void Builtins::Generate_RestartFrameTrampoline(MacroAssembler* masm) {
  Generate_CallToAdaptShadowStackForDeopt(masm, true);
  masm->isolate()->heap()->SetDeoptPCOffsetAfterAdaptShadowStack(
      masm->pc_offset());

  // Restart the current frame:
  // - Look up current function on the frame.
  // - Leave the frame.
  // - Restart the frame by calling the function.

  __ movq(rdi, Operand(rbp, StandardFrameConstants::kFunctionOffset));
  __ movq(rax, Operand(rbp, StandardFrameConstants::kArgCOffset));

  __ LeaveFrame(StackFrame::INTERPRETED);

  // The arguments are already in the stack (including any necessary padding),
  // we should not try to massage the arguments again.
#ifdef V8_ENABLE_LEAPTIERING
  __ InvokeFunction(rdi, no_reg, rax, InvokeType::kJump,
                    ArgumentAdaptionMode::kDontAdapt);
#else
  __ movq(rbx, Immediate(kDontAdaptArgumentsSentinel));
  __ InvokeFunction(rdi, no_reg, rbx, rax, InvokeType::kJump);
#endif
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_X64
