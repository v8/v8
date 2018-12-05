// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_ARM64

#include "src/api-arguments.h"
#include "src/arm64/assembler-arm64-inl.h"
#include "src/arm64/macro-assembler-arm64-inl.h"
#include "src/bootstrapper.h"
#include "src/code-stubs.h"
#include "src/counters.h"
#include "src/frame-constants.h"
#include "src/frames.h"
#include "src/ic/ic.h"
#include "src/ic/stub-cache.h"
#include "src/isolate.h"
#include "src/macro-assembler.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/regexp-match-info.h"
#include "src/regexp/jsregexp.h"
#include "src/regexp/regexp-macro-assembler.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

// This is the entry point from C++. 5 arguments are provided in x0-x4.
// See use of the JSEntryFunction for example in src/execution.cc.
// Input:
//   x0: code entry.
//   x1: function.
//   x2: receiver.
//   x3: argc.
//   x4: argv.
// Output:
//   x0: result.
void JSEntryStub::Generate(MacroAssembler* masm) {
  Label invoke, handler_entry, exit;

  Register code_entry = x0;

  {
    NoRootArrayScope no_root_array(masm);

    // Enable instruction instrumentation. This only works on the simulator, and
    // will have no effect on the model or real hardware.
    __ EnableInstrumentation();

    __ PushCalleeSavedRegisters();

    // Set up the reserved register for 0.0.
    __ Fmov(fp_zero, 0.0);

    // Initialize the root array register
    __ InitializeRootRegister();
  }

  // Build an entry frame (see layout below).
  StackFrame::Type marker = type();
  int64_t bad_frame_pointer = -1L;  // Bad frame pointer to fail if it is used.
  __ Mov(x13, bad_frame_pointer);
  __ Mov(x12, StackFrame::TypeToMarker(marker));
  __ Mov(x11, ExternalReference::Create(IsolateAddressId::kCEntryFPAddress,
                                        isolate()));
  __ Ldr(x10, MemOperand(x11));

  __ Push(x13, x12, xzr, x10);
  // Set up fp.
  __ Sub(fp, sp, EntryFrameConstants::kCallerFPOffset);

  // Push the JS entry frame marker. Also set js_entry_sp if this is the
  // outermost JS call.
  Label non_outermost_js, done;
  ExternalReference js_entry_sp =
      ExternalReference::Create(IsolateAddressId::kJSEntrySPAddress, isolate());
  __ Mov(x10, js_entry_sp);
  __ Ldr(x11, MemOperand(x10));

  // Select between the inner and outermost frame marker, based on the JS entry
  // sp. We assert that the inner marker is zero, so we can use xzr to save a
  // move instruction.
  DCHECK_EQ(StackFrame::INNER_JSENTRY_FRAME, 0);
  __ Cmp(x11, 0);  // If x11 is zero, this is the outermost frame.
  __ Csel(x12, xzr, StackFrame::OUTERMOST_JSENTRY_FRAME, ne);
  __ B(ne, &done);
  __ Str(fp, MemOperand(x10));

  __ Bind(&done);
  __ Push(x12, padreg);

  // The frame set up looks like this:
  // sp[0] : padding.
  // sp[1] : JS entry frame marker.
  // sp[2] : C entry FP.
  // sp[3] : stack frame marker.
  // sp[4] : stack frame marker.
  // sp[5] : bad frame pointer 0xFFF...FF   <- fp points here.

  // Jump to a faked try block that does the invoke, with a faked catch
  // block that sets the pending exception.
  __ B(&invoke);

  // Prevent the constant pool from being emitted between the record of the
  // handler_entry position and the first instruction of the sequence here.
  // There is no risk because Assembler::Emit() emits the instruction before
  // checking for constant pool emission, but we do not want to depend on
  // that.
  {
    Assembler::BlockPoolsScope block_pools(masm);
    __ bind(&handler_entry);
    handler_offset_ = handler_entry.pos();
    // Caught exception: Store result (exception) in the pending exception
    // field in the JSEnv and return a failure sentinel. Coming in here the
    // fp will be invalid because the PushTryHandler below sets it to 0 to
    // signal the existence of the JSEntry frame.
    __ Mov(x10, Operand(ExternalReference::Create(
                    IsolateAddressId::kPendingExceptionAddress, isolate())));
  }
  __ Str(code_entry, MemOperand(x10));
  __ LoadRoot(x0, RootIndex::kException);
  __ B(&exit);

  // Invoke: Link this frame into the handler chain.
  __ Bind(&invoke);

  // Push new stack handler.
  static_assert(StackHandlerConstants::kSize == 2 * kPointerSize,
                "Unexpected offset for StackHandlerConstants::kSize");
  static_assert(StackHandlerConstants::kNextOffset == 0 * kPointerSize,
                "Unexpected offset for StackHandlerConstants::kNextOffset");

  // Link the current handler as the next handler.
  __ Mov(x11, ExternalReference::Create(IsolateAddressId::kHandlerAddress,
                                        isolate()));
  __ Ldr(x10, MemOperand(x11));
  __ Push(padreg, x10);

  // Set this new handler as the current one.
  {
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.AcquireX();
    __ Mov(scratch, sp);
    __ Str(scratch, MemOperand(x11));
  }

  // If an exception not caught by another handler occurs, this handler
  // returns control to the code after the B(&invoke) above, which
  // restores all callee-saved registers (including cp and fp) to their
  // saved values before returning a failure to C.

  // Invoke the function by calling through the JS entry trampoline builtin.
  // Notice that we cannot store a reference to the trampoline code directly in
  // this stub, because runtime stubs are not traversed when doing GC.

  // Expected registers by Builtins::JSEntryTrampoline
  // x0: code entry.
  // x1: function.
  // x2: receiver.
  // x3: argc.
  // x4: argv.
  __ Call(EntryTrampoline(), RelocInfo::CODE_TARGET);

  // Pop the stack handler and unlink this frame from the handler chain.
  static_assert(StackHandlerConstants::kNextOffset == 0 * kPointerSize,
                "Unexpected offset for StackHandlerConstants::kNextOffset");
  __ Pop(x10, padreg);
  __ Mov(x11, ExternalReference::Create(IsolateAddressId::kHandlerAddress,
                                        isolate()));
  __ Drop(StackHandlerConstants::kSlotCount - 2);
  __ Str(x10, MemOperand(x11));

  __ Bind(&exit);
  // x0 holds the result.
  // The stack pointer points to the top of the entry frame pushed on entry from
  // C++ (at the beginning of this stub):
  // sp[0] : padding.
  // sp[1] : JS entry frame marker.
  // sp[2] : C entry FP.
  // sp[3] : stack frame marker.
  // sp[4] : stack frame marker.
  // sp[5] : bad frame pointer 0xFFF...FF   <- fp points here.

  // Check if the current stack frame is marked as the outermost JS frame.
  Label non_outermost_js_2;
  {
    Register c_entry_fp = x11;
    __ PeekPair(x10, c_entry_fp, 1 * kPointerSize);
    __ Cmp(x10, StackFrame::OUTERMOST_JSENTRY_FRAME);
    __ B(ne, &non_outermost_js_2);
    __ Mov(x12, js_entry_sp);
    __ Str(xzr, MemOperand(x12));
    __ Bind(&non_outermost_js_2);

    // Restore the top frame descriptors from the stack.
    __ Mov(x12, ExternalReference::Create(IsolateAddressId::kCEntryFPAddress,
                                          isolate()));
    __ Str(c_entry_fp, MemOperand(x12));
  }

  // Reset the stack to the callee saved registers.
  static_assert(EntryFrameConstants::kFixedFrameSize % (2 * kPointerSize) == 0,
                "Size of entry frame is not a multiple of 16 bytes");
  __ Drop(EntryFrameConstants::kFixedFrameSize / kPointerSize);
  // Restore the callee-saved registers and return.
  __ PopCalleeSavedRegisters();
  __ Ret();
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM64
