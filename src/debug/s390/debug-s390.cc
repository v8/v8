// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_S390

#include "src/codegen.h"
#include "src/debug/debug.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

void EmitDebugBreakSlot(MacroAssembler* masm) {
  Label check_size;
  __ bind(&check_size);
  //   oill r3, 0
  //   oill r3, 0
  __ nop(Assembler::DEBUG_BREAK_NOP);
  __ nop(Assembler::DEBUG_BREAK_NOP);

  //   lr r0, r0    64-bit only
  //   lr r0, r0    64-bit only
  //   lr r0, r0    64-bit only
  for (int i = 8; i < Assembler::kDebugBreakSlotLength; i += 2) {
    __ nop();
  }
  DCHECK_EQ(Assembler::kDebugBreakSlotLength,
            masm->SizeOfCodeGeneratedSince(&check_size));
}

void DebugCodegen::GenerateSlot(MacroAssembler* masm, RelocInfo::Mode mode) {
  // Generate enough nop's to make space for a call instruction.
  masm->RecordDebugBreakSlot(mode);
  EmitDebugBreakSlot(masm);
}

void DebugCodegen::ClearDebugBreakSlot(Isolate* isolate, Address pc) {
  CodePatcher patcher(isolate, pc, Assembler::kDebugBreakSlotLength);
  EmitDebugBreakSlot(patcher.masm());
}

void DebugCodegen::PatchDebugBreakSlot(Isolate* isolate, Address pc,
                                       Handle<Code> code) {
  DCHECK_EQ(Code::BUILTIN, code->kind());
  CodePatcher patcher(isolate, pc, Assembler::kDebugBreakSlotLength);
  // Patch the code changing the debug break slot code from
  //
  //   oill r3, 0
  //   oill r3, 0
  //   oill r3, 0   64-bit only
  //   lr r0, r0    64-bit only
  //
  // to a call to the debug break code, using a FIXED_SEQUENCE.
  //
  //   iilf r14, <address>   6-bytes
  //   basr r14, r14A        2-bytes
  //
  // The 64bit sequence has an extra iihf.
  //
  //   iihf r14, <high 32-bits address>    6-bytes
  //   iilf r14, <lower 32-bits address>   6-bytes
  //   basr r14, r14                       2-bytes
  patcher.masm()->mov(v8::internal::r14,
                      Operand(reinterpret_cast<intptr_t>(code->entry())));
  patcher.masm()->basr(v8::internal::r14, v8::internal::r14);
}

bool DebugCodegen::DebugBreakSlotIsPatched(Address pc) {
  Instr current_instr = Assembler::instr_at(pc);
  return !Assembler::IsNop(current_instr, Assembler::DEBUG_BREAK_NOP);
}

void DebugCodegen::GenerateDebugBreakStub(MacroAssembler* masm,
                                          DebugBreakCallHelperMode mode) {
  __ RecordComment("Debug break");
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Load padding words on stack.
    __ LoadSmiLiteral(ip, Smi::FromInt(LiveEdit::kFramePaddingValue));
    for (int i = 0; i < LiveEdit::kFramePaddingInitialSize; i++) {
      __ push(ip);
    }
    __ LoadSmiLiteral(ip, Smi::FromInt(LiveEdit::kFramePaddingInitialSize));
    __ push(ip);

    if (mode == SAVE_RESULT_REGISTER) __ push(r2);

    __ mov(r2, Operand::Zero());  // no arguments
    __ mov(r3,
           Operand(ExternalReference(
               Runtime::FunctionForId(Runtime::kDebugBreak), masm->isolate())));

    CEntryStub ceb(masm->isolate(), 1);
    __ CallStub(&ceb);

    if (FLAG_debug_code) {
      for (int i = 0; i < kNumJSCallerSaved; i++) {
        Register reg = {JSCallerSavedCode(i)};
        __ mov(reg, Operand(kDebugZapValue));
      }
    }

    if (mode == SAVE_RESULT_REGISTER) __ pop(r2);

    // Don't bother removing padding bytes pushed on the stack
    // as the frame is going to be restored right away.

    // Leave the internal frame.
  }

  // Now that the break point has been handled, resume normal execution by
  // jumping to the target address intended by the caller and that was
  // overwritten by the address of DebugBreakXXX.
  ExternalReference after_break_target =
      ExternalReference::debug_after_break_target_address(masm->isolate());
  __ mov(ip, Operand(after_break_target));
  __ LoadP(ip, MemOperand(ip));
  __ JumpToJSEntry(ip);
}

void DebugCodegen::GenerateFrameDropperLiveEdit(MacroAssembler* masm) {
  // Load the function pointer off of our current stack frame.
  __ LoadP(r3, MemOperand(fp, StandardFrameConstants::kConstantPoolOffset -
                                  kPointerSize));

  // Pop return address and frame
  __ LeaveFrame(StackFrame::INTERNAL);

  ParameterCount dummy(0);
  __ FloodFunctionIfStepping(r3, no_reg, dummy, dummy);

  // Load context from the function.
  __ LoadP(cp, FieldMemOperand(r3, JSFunction::kContextOffset));

  // Clear new.target as a safety measure.
  __ LoadRoot(r5, Heap::kUndefinedValueRootIndex);

  // Get function code.
  __ LoadP(ip, FieldMemOperand(r3, JSFunction::kSharedFunctionInfoOffset));
  __ LoadP(ip, FieldMemOperand(ip, SharedFunctionInfo::kCodeOffset));
  __ AddP(ip, Operand(Code::kHeaderSize - kHeapObjectTag));

  // Re-run JSFunction, r3 is function, cp is context.
  __ Jump(ip);
}

const bool LiveEdit::kFrameDropperSupported = true;

#undef __
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_S390
