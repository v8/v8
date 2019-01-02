// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_S390

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

// Called with the native C calling convention. The corresponding function
// signature is:
//
//  using JSEntryFunction = GeneratedCode<Object*(
//      Object * new_target, Object * target, Object * receiver, int argc,
//      Object*** args, Address root_register_value)>;
void JSEntryStub::Generate(MacroAssembler* masm) {
  // r2:                            code entry
  // r3:                            function
  // r4:                            receiver
  // r5:                            argc
  // r6:                            argv
  // [sp + 20 * kSystemPointerSize]: root register value

  Label invoke, handler_entry, exit;

  static constexpr int kPushedStackSpace =
      (kNumCalleeSaved + 2) * kPointerSize +
      kNumCalleeSavedDoubles * kDoubleSize;

  {
    NoRootArrayScope no_root_array(masm);

    // saving floating point registers
    // 64bit ABI requires f8 to f15 be saved
    // http://refspecs.linuxbase.org/ELF/zSeries/lzsabi0_zSeries.html
    __ lay(sp, MemOperand(sp, -8 * kDoubleSize));
    __ std(d8, MemOperand(sp));
    __ std(d9, MemOperand(sp, 1 * kDoubleSize));
    __ std(d10, MemOperand(sp, 2 * kDoubleSize));
    __ std(d11, MemOperand(sp, 3 * kDoubleSize));
    __ std(d12, MemOperand(sp, 4 * kDoubleSize));
    __ std(d13, MemOperand(sp, 5 * kDoubleSize));
    __ std(d14, MemOperand(sp, 6 * kDoubleSize));
    __ std(d15, MemOperand(sp, 7 * kDoubleSize));

    // zLinux ABI
    //    Incoming parameters:
    //          r2: code entry
    //          r3: function
    //          r4: receiver
    //          r5: argc
    //          r6: argv
    // [sp + 20 * kSystemPointerSize]: root register value
    //    Requires us to save the callee-preserved registers r6-r13
    //    General convention is to also save r14 (return addr) and
    //    sp/r15 as well in a single STM/STMG
    __ lay(sp, MemOperand(sp, -10 * kPointerSize));
    __ StoreMultipleP(r6, sp, MemOperand(sp, 0));

    // Initialize the root register.
    // C calling convention. The sixth argument is passed on the stack.
    static constexpr int kOffsetToRootRegisterValue =
        kPushedStackSpace + EntryFrameConstants::kRootRegisterValueOffset;
    __ LoadP(kRootRegister, MemOperand(sp, kOffsetToRootRegisterValue));
  }

  // save r6 to r1
  __ LoadRR(r1, r6);

  // Push a frame with special values setup to mark it as an entry frame.
  //   Bad FP (-1)
  //   SMI Marker
  //   SMI Marker
  //   kCEntryFPAddress
  //   Frame type
  __ lay(sp, MemOperand(sp, -5 * kPointerSize));

  // Push a bad frame pointer to fail if it is used.
  __ LoadImmP(r9, Operand(-1));

  StackFrame::Type marker = type();
  __ Load(r8, Operand(StackFrame::TypeToMarker(marker)));
  __ Load(r7, Operand(StackFrame::TypeToMarker(marker)));
  // Save copies of the top frame descriptor on the stack.
  __ mov(r6, Operand(ExternalReference::Create(
                 IsolateAddressId::kCEntryFPAddress, isolate())));
  __ LoadP(r6, MemOperand(r6));
  __ StoreMultipleP(r6, r9, MemOperand(sp, kPointerSize));
  // Set up frame pointer for the frame to be pushed.
  // Need to add kPointerSize, because sp has one extra
  // frame already for the frame type being pushed later.
  __ lay(fp, MemOperand(
                 sp, -EntryFrameConstants::kCallerFPOffset + kPointerSize));

  // restore r6
  __ LoadRR(r6, r1);

  // If this is the outermost JS call, set js_entry_sp value.
  Label non_outermost_js;
  ExternalReference js_entry_sp =
      ExternalReference::Create(IsolateAddressId::kJSEntrySPAddress, isolate());
  __ mov(r7, Operand(js_entry_sp));
  __ LoadAndTestP(r8, MemOperand(r7));
  __ bne(&non_outermost_js, Label::kNear);
  __ StoreP(fp, MemOperand(r7));
  __ Load(ip, Operand(StackFrame::OUTERMOST_JSENTRY_FRAME));
  Label cont;
  __ b(&cont, Label::kNear);
  __ bind(&non_outermost_js);
  __ Load(ip, Operand(StackFrame::INNER_JSENTRY_FRAME));

  __ bind(&cont);
  __ StoreP(ip, MemOperand(sp));  // frame-type

  // Jump to a faked try block that does the invoke, with a faked catch
  // block that sets the pending exception.
  __ b(&invoke, Label::kNear);

  __ bind(&handler_entry);
  handler_offset_ = handler_entry.pos();
  // Caught exception: Store result (exception) in the pending exception
  // field in the JSEnv and return a failure sentinel.  Coming in here the
  // fp will be invalid because the PushStackHandler below sets it to 0 to
  // signal the existence of the JSEntry frame.
  __ mov(ip, Operand(ExternalReference::Create(
                 IsolateAddressId::kPendingExceptionAddress, isolate())));

  __ StoreP(r2, MemOperand(ip));
  __ LoadRoot(r2, RootIndex::kException);
  __ b(&exit, Label::kNear);

  // Invoke: Link this frame into the handler chain.
  __ bind(&invoke);
  // Must preserve r2-r6.
  __ PushStackHandler();
  // If an exception not caught by another handler occurs, this handler
  // returns control to the code after the b(&invoke) above, which
  // restores all kCalleeSaved registers (including cp and fp) to their
  // saved values before returning a failure to C.

  // Invoke the function by calling through JS entry trampoline builtin.
  // Notice that we cannot store a reference to the trampoline code directly in
  // this stub, because runtime stubs are not traversed when doing GC.

  // Expected registers by Builtins::JSEntryTrampoline
  // r2: code entry
  // r3: function
  // r4: receiver
  // r5: argc
  // r6: argv
  __ Call(EntryTrampoline(), RelocInfo::CODE_TARGET);

  // Unlink this frame from the handler chain.
  __ PopStackHandler();
  __ bind(&exit);  // r2 holds result

  // Check if the current stack frame is marked as the outermost JS frame.
  Label non_outermost_js_2;
  __ pop(r7);
  __ CmpP(r7, Operand(StackFrame::OUTERMOST_JSENTRY_FRAME));
  __ bne(&non_outermost_js_2, Label::kNear);
  __ mov(r8, Operand::Zero());
  __ mov(r7, Operand(js_entry_sp));
  __ StoreP(r8, MemOperand(r7));
  __ bind(&non_outermost_js_2);

  // Restore the top frame descriptors from the stack.
  __ pop(r5);
  __ mov(ip, Operand(ExternalReference::Create(
                 IsolateAddressId::kCEntryFPAddress, isolate())));
  __ StoreP(r5, MemOperand(ip));

  // Reset the stack to the callee saved registers.
  __ lay(sp, MemOperand(sp, -EntryFrameConstants::kCallerFPOffset));

  // Reload callee-saved preserved regs, return address reg (r14) and sp
  __ LoadMultipleP(r6, sp, MemOperand(sp, 0));
  __ la(sp, MemOperand(sp, 10 * kPointerSize));

// saving floating point registers
#if V8_TARGET_ARCH_S390X
  // 64bit ABI requires f8 to f15 be saved
  __ ld(d8, MemOperand(sp));
  __ ld(d9, MemOperand(sp, 1 * kDoubleSize));
  __ ld(d10, MemOperand(sp, 2 * kDoubleSize));
  __ ld(d11, MemOperand(sp, 3 * kDoubleSize));
  __ ld(d12, MemOperand(sp, 4 * kDoubleSize));
  __ ld(d13, MemOperand(sp, 5 * kDoubleSize));
  __ ld(d14, MemOperand(sp, 6 * kDoubleSize));
  __ ld(d15, MemOperand(sp, 7 * kDoubleSize));
  __ la(sp, MemOperand(sp, 8 * kDoubleSize));
#else
  // 31bit ABI requires you to store f4 and f6:
  // http://refspecs.linuxbase.org/ELF/zSeries/lzsabi0_s390.html#AEN417
  __ ld(d4, MemOperand(sp));
  __ ld(d6, MemOperand(sp, kDoubleSize));
  __ la(sp, MemOperand(sp, 2 * kDoubleSize));
#endif

  __ b(r14);
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_S390
