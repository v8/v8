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

VirtualFrame::VirtualFrame(CodeGenerator* cgen)
    : masm_(cgen->masm()),
      elements_(0),
      parameter_count_(cgen->scope()->num_parameters()),
      local_count_(0),
      frame_pointer_(-1) {
  // The virtual frame contains a receiver and the parameters (all in
  // memory) when it is created.
  Adjust(parameter_count_ + 1);
}


VirtualFrame::VirtualFrame(VirtualFrame* original)
    : masm_(original->masm_),
      elements_(original->elements_.length()),
      parameter_count_(original->parameter_count_),
      local_count_(original->local_count_),
      frame_pointer_(original->frame_pointer_) {
  // Copy all the elements.
  for (int i = 0; i < original->elements_.length(); i++) {
    elements_.Add(original->elements_[i]);
  }
}


void VirtualFrame::Adjust(int count) {
  UNIMPLEMENTED();
}


void VirtualFrame::Forget(int count) {
  ASSERT(count >= 0);
  ASSERT(elements_.length() >= count);
  for (int i = 0; i < count; i++) {
    elements_.RemoveLast();
  }
}


void VirtualFrame::MergeTo(VirtualFrame* expected) {
  ASSERT(masm_ == expected->masm_);
  ASSERT(elements_.length() == expected->elements_.length());
  ASSERT(parameter_count_ == expected->parameter_count_);
  ASSERT(local_count_ == expected->local_count_);
  ASSERT(frame_pointer_ == expected->frame_pointer_);
  UNIMPLEMENTED();
}


void VirtualFrame::DetachFromCodeGenerator() {
  UNIMPLEMENTED();
}


void VirtualFrame::AttachToCodeGenerator() {
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


void VirtualFrame::PushTryHandler(HandlerType type) {
  // Grow the expression stack by handler size less one (the return address
  // is already pushed by a call instruction).
  Adjust(kHandlerSize - 1);
  __ PushTryHandler(IN_JAVASCRIPT, type);
}


void VirtualFrame::CallStub(CodeStub* stub, int frame_arg_count) {
  ASSERT(height() >= frame_arg_count);
  Forget(frame_arg_count);
  __ CallStub(stub);
}


void VirtualFrame::CallRuntime(Runtime::Function* f, int frame_arg_count) {
  ASSERT(height() >= frame_arg_count);
  Forget(frame_arg_count);
  __ CallRuntime(f, frame_arg_count);
}


void VirtualFrame::CallRuntime(Runtime::FunctionId id, int frame_arg_count) {
  ASSERT(height() >= frame_arg_count);
  Forget(frame_arg_count);
  __ CallRuntime(id, frame_arg_count);
}


void VirtualFrame::InvokeBuiltin(Builtins::JavaScript id,
                                 InvokeJSFlags flags,
                                 int frame_arg_count) {
  ASSERT(height() >= frame_arg_count);
  Forget(frame_arg_count);
  __ InvokeBuiltin(id, flags);
}


void VirtualFrame::CallCodeObject(Handle<Code> code,
                                  RelocInfo::Mode rmode,
                                  int frame_arg_count) {
  ASSERT(height() >= frame_arg_count);
  Forget(frame_arg_count);
  __ Call(code, rmode);
}


void VirtualFrame::Drop(int count) {
  ASSERT(height() >= count);
  Forget(count);
  if (count > 0) {
    __ add(sp, sp, Operand(count * kPointerSize));
  }
}


void VirtualFrame::Drop() { Drop(1); }


void VirtualFrame::Pop(Register reg) {
  Forget(1);
  __ pop(reg);
}


void VirtualFrame::EmitPush(Register reg) {
  Adjust(1);
  __ push(reg);
}


#undef __

} }  // namespace v8::internal
