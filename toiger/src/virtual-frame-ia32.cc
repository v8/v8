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
  // The virtual frame contains a receiver, the parameters, and a return
  // address (all in memory) when it is created.
  Adjust(parameter_count_ + 2);
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
  ASSERT(count >= 0);
  for (int i = 0; i < count; i++) {
    elements_.Add(Element());
  }
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
  for (int i = 0; i < elements_.length(); i++) {
    ASSERT(elements_[i].matches(expected->elements_[i]));
  }
}


void VirtualFrame::Enter() {
  Comment cmnt(masm_, "[ Enter JS frame");
  Adjust(1);
  __ push(ebp);

  frame_pointer_ = elements_.length() - 1;
  __ mov(ebp, Operand(esp));

  // Store the context and the function in the frame.
  Adjust(2);
  __ push(esi);
  __ push(edi);

  // Clear the function slot when generating debug code.
  if (FLAG_debug_code) {
    __ Set(edi, Immediate(reinterpret_cast<int>(kZapValue)));
  }
}


void VirtualFrame::Exit() {
  Comment cmnt(masm_, "[ Exit JS frame");
  // Record the location of the JS exit code for patching when setting
  // break point.
  __ RecordJSReturn();

  // Avoid using the leave instruction here, because it is too
  // short. We need the return sequence to be a least the size of a
  // call instruction to support patching the exit code in the
  // debugger. See VisitReturnStatement for the full return sequence.
  __ mov(esp, Operand(ebp));
  __ pop(ebp);
}


void VirtualFrame::AllocateStackSlots(int count) {
  ASSERT(height() == 0);
  local_count_ = count;
  Adjust(count);
  if (count > 0) {
    Comment cmnt(masm_, "[ Allocate space for locals");
    __ Set(eax, Immediate(Factory::undefined_value()));
    for (int i = 0; i < count; i++) {
      __ push(eax);
    }
  }
}


void VirtualFrame::PushTryHandler(HandlerType type) {
  // Grow the expression stack by handler size less two (the return address
  // is already pushed by a call instruction, and PushTryHandler from the
  // macro assembler will leave the top of stack in the eax register to be
  // pushed separately).
  Adjust(kHandlerSize - 2);
  __ PushTryHandler(IN_JAVASCRIPT, type);
  // TODO(1222589): remove the reliance of PushTryHandler on a cached TOS
  Push(eax);
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
                                 InvokeFlag flag,
                                 int frame_arg_count) {
  ASSERT(height() >= frame_arg_count);
  Forget(frame_arg_count);
  __ InvokeBuiltin(id, flag);
}


void VirtualFrame::CallCodeObject(Handle<Code> code,
                                  RelocInfo::Mode rmode,
                                  int frame_arg_count) {
  ASSERT(height() >= frame_arg_count);
  Forget(frame_arg_count);
  __ call(code, rmode);
}


void VirtualFrame::Drop(int count) {
  ASSERT(height() >= count);
  Forget(count);
  if (count > 0) {
    __ add(Operand(esp), Immediate(count * kPointerSize));
  }
}


void VirtualFrame::Drop() { Drop(1); }


void VirtualFrame::Pop(Register reg) {
  Forget(1);
  __ pop(reg);
}


void VirtualFrame::Pop(Operand operand) {
  Forget(1);
  __ pop(operand);
}


void VirtualFrame::Push(Register reg) {
  Adjust(1);
  __ push(reg);
}


void VirtualFrame::Push(Operand operand) {
  Adjust(1);
  __ push(operand);
}


void VirtualFrame::Push(Immediate immediate) {
  Adjust(1);
  __ push(immediate);
}

#undef __

} }  // namespace v8::internal
