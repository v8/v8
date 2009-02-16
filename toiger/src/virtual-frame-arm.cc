// Copyright 2008 the V8 project authors. All rights reserved.
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

#include "codegen.h"
#include "codegen-inl.h"
#include "virtual-frame.h"

namespace v8 { namespace internal {

// -------------------------------------------------------------------------
// VirtualFrame implementation.

#define __ masm_->

// Clear the dirty bit for the element at a given index if it is a
// valid element.  The stack address corresponding to the element must
// be allocated on the physical stack, or the first element above the
// stack pointer so it can be allocated by a single push instruction.
void VirtualFrame::RawSyncElementAt(int index) {
  UNIMPLEMENTED();
}


void VirtualFrame::MergeTo(VirtualFrame* expected) {
  UNIMPLEMENTED();
}


void VirtualFrame::MergeMoveRegistersToMemory(VirtualFrame* expected) {
  UNIMPLEMENTED();
}


void VirtualFrame::MergeMoveRegistersToRegisters(VirtualFrame* expected) {
  UNIMPLEMENTED();
}


void VirtualFrame::MergeMoveMemoryToRegisters(VirtualFrame *expected) {
  UNIMPLEMENTED();
}


void VirtualFrame::Enter() {
  Comment cmnt(masm_, "[ Enter JS frame");
#ifdef DEBUG
  { Label done, fail;
    __ tst(r1, Operand(kSmiTagMask));
    __ b(eq, &fail);
    __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));
    __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
    __ cmp(r2, Operand(JS_FUNCTION_TYPE));
    __ b(eq, &done);
    __ bind(&fail);
    __ stop("CodeGenerator::EnterJSFrame - r1 not a function");
    __ bind(&done);
  }
#endif  // DEBUG

  // We are about to push four values to the frame.
  Adjust(4);
  __ stm(db_w, sp, r1.bit() | cp.bit() | fp.bit() | lr.bit());
  // Adjust FP to point to saved FP.
  frame_pointer_ = elements_.length() - 2;
  __ add(fp, sp, Operand(2 * kPointerSize));
}


void VirtualFrame::Exit() {
  Comment cmnt(masm_, "[ Exit JS frame");
  // Drop the execution stack down to the frame pointer and restore the caller
  // frame pointer and return address.
  __ mov(sp, fp);
  __ ldm(ia_w, sp, fp.bit() | lr.bit());
}


void VirtualFrame::AllocateStackSlots(int count) {
  ASSERT(height() == 0);
  local_count_ = count;
  Adjust(count);
  if (count > 0) {
    Comment cmnt(masm_, "[ Allocate space for locals");
      // Initialize stack slots with 'undefined' value.
    __ mov(ip, Operand(Factory::undefined_value()));
    for (int i = 0; i < count; i++) {
      __ push(ip);
    }
  }
}


void VirtualFrame::SaveContextRegister() {
  UNIMPLEMENTED();
}


void VirtualFrame::RestoreContextRegister() {
  UNIMPLEMENTED();
}


void VirtualFrame::PushReceiverSlotAddress() {
  UNIMPLEMENTED();
}


// Before changing an element which is copied, adjust so that the
// first copy becomes the new backing store and all the other copies
// are updated.  If the original was in memory, the new backing store
// is allocated to a register.  Return a copy of the new backing store
// or an invalid element if the original was not a copy.
FrameElement VirtualFrame::AdjustCopies(int index) {
  UNIMPLEMENTED();
  return FrameElement::InvalidElement();
}


void VirtualFrame::TakeFrameSlotAt(int index) {
  UNIMPLEMENTED();
}


void VirtualFrame::StoreToFrameSlotAt(int index) {
  UNIMPLEMENTED();
}


void VirtualFrame::PushTryHandler(HandlerType type) {
  // Grow the expression stack by handler size less one (the return address
  // is already pushed by a call instruction).
  Adjust(kHandlerSize - 1);
  __ PushTryHandler(IN_JAVASCRIPT, type);
}


Result VirtualFrame::RawCallStub(CodeStub* stub, int frame_arg_count) {
  UNIMPLEMENTED();
  Result invalid(cgen_);
  return invalid;
}


Result VirtualFrame::CallRuntime(Runtime::Function* f,
                                 int frame_arg_count) {
  UNIMPLEMENTED();
  Result invalid(cgen_);
  return invalid;
}


Result VirtualFrame::CallRuntime(Runtime::FunctionId id,
                                 int frame_arg_count) {
  UNIMPLEMENTED();
  Result invalid(cgen_);
  return invalid;
}


Result VirtualFrame::InvokeBuiltin(Builtins::JavaScript id,
                                   InvokeJSFlags flags,
                                   int frame_arg_count) {
  UNIMPLEMENTED();
  Result invalid(cgen_);
  return invalid;
}


Result VirtualFrame::RawCallCodeObject(Handle<Code> code,
                                       RelocInfo::Mode rmode) {
  UNIMPLEMENTED();
  Result invalid(cgen_);
  return invalid;
}


Result VirtualFrame::CallCodeObject(Handle<Code> code,
                                    RelocInfo::Mode rmode,
                                    Result* arg,
                                    int dropped_args) {
  UNIMPLEMENTED();
  Result invalid(cgen_);
  return invalid;
}


Result VirtualFrame::CallCodeObject(Handle<Code> code,
                                    RelocInfo::Mode rmode,
                                    Result* arg0,
                                    Result* arg1,
                                    int dropped_args) {
  UNIMPLEMENTED();
  Result invalid(cgen_);
  return invalid;
}


void VirtualFrame::Drop(int count) {
  UNIMPLEMENTED();
}


Result VirtualFrame::Pop() {
  UNIMPLEMENTED();
  Result invalid(cgen_);
  return invalid;
}


void VirtualFrame::EmitPop(Register reg) {
  UNIMPLEMENTED();
}


void VirtualFrame::EmitPush(Register reg) {
  UNIMPLEMENTED();
}


#undef __

} }  // namespace v8::internal
