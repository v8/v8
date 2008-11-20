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

#define __ masm_->

// -------------------------------------------------------------------------
// VirtualFrame implementation.

// On entry to a function, the virtual frame already contains the receiver,
// the parameters, and a return address.  All frame elements are in memory.
VirtualFrame::VirtualFrame(CodeGenerator* cgen)
    : masm_(cgen->masm()),
      elements_(0),
      parameter_count_(cgen->scope()->num_parameters()),
      local_count_(0),
      stack_pointer_(parameter_count_ + 1),  // 0-based index of TOS.
      frame_pointer_(kIllegalIndex) {
  for (int i = 0; i < parameter_count_ + 2; i++) {
    elements_.Add(FrameElement());
  }
}


// When cloned, a frame is a deep copy of the original.
VirtualFrame::VirtualFrame(VirtualFrame* original)
    : masm_(original->masm_),
      elements_(original->elements_.length()),
      parameter_count_(original->parameter_count_),
      local_count_(original->local_count_),
      stack_pointer_(original->stack_pointer_),
      frame_pointer_(original->frame_pointer_) {
  // Copy all the elements from the original.
  for (int i = 0; i < original->elements_.length(); i++) {
    elements_.Add(original->elements_[i]);
  }
}


// Modify the state of the virtual frame to match the actual frame by adding
// extra in-memory elements to the top of the virtual frame.  The extra
// elements will be externally materialized on the actual frame (eg, by
// pushing an exception handler).  No code is emitted.
void VirtualFrame::Adjust(int count) {
  ASSERT(count >= 0);
  ASSERT(stack_pointer_ == elements_.length() - 1);

  for (int i = 0; i < count; i++) {
    elements_.Add(FrameElement());
  }
  stack_pointer_ += count;
}


// Modify the state of the virtual frame to match the actual frame by
// removing elements from the top of the virtual frame.  The elements will
// be externally popped from the actual frame (eg, by a runtime call).  No
// code is emitted.
void VirtualFrame::Forget(int count) {
  ASSERT(count >= 0);
  ASSERT(stack_pointer_ == elements_.length() - 1);
  ASSERT(elements_.length() >= count);

  stack_pointer_ -= count;
  for (int i = 0; i < count; i++) {
    elements_.RemoveLast();
  }
}


void VirtualFrame::SpillAll() {
  int i = 0;

  // Spill dirty constants below the stack pointer.
  for (; i <= stack_pointer_; i++) {
    if (elements_[i].type() == FrameElement::CONSTANT &&
        elements_[i].is_dirty()) {
      __ mov(Operand(ebp, fp_relative(i)), Immediate(elements_[i].handle()));
      elements_[i] = FrameElement();  // The element is now in memory.
    }
  }

  // Spill all constants above the stack pointer.
  for (; i < elements_.length(); i++) {
    ASSERT(elements_[i].type() == FrameElement::CONSTANT);
    ASSERT(elements_[i].is_dirty());
    stack_pointer_++;
    __ push(Immediate(elements_[i].handle()));
    elements_[i] = FrameElement();  // The element is now in memory.
  }
}


void VirtualFrame::PrepareForCall(int frame_arg_count) {
  ASSERT(height() >= frame_arg_count);

  // The only non-memory elements of the frame are constants.  Push all of
  // them above the stack pointer to allocate space for them and to ensure
  // the arguments are flushed to memory.
  for (int i = stack_pointer_ + 1; i < elements_.length(); i++) {
    ASSERT(elements_[i].type() == FrameElement::CONSTANT);
    ASSERT(elements_[i].is_dirty());
    stack_pointer_++;
    elements_[i].clear_dirty();
    __ push(Immediate(elements_[i].handle()));
  }

  // Forget the ones that will be popped by the call.
  Forget(frame_arg_count);
}


void VirtualFrame::EnsureMergable() {
  // We cannot merge to a frame that has constants as elements, because an
  // arbitrary frame may not have constants in those locations.
  SpillAll();
}


void VirtualFrame::MergeTo(VirtualFrame* expected) {
  ASSERT(masm_ == expected->masm_);
  ASSERT(elements_.length() == expected->elements_.length());
  ASSERT(parameter_count_ == expected->parameter_count_);
  ASSERT(local_count_ == expected->local_count_);
  ASSERT(frame_pointer_ == expected->frame_pointer_);

  // The expected frame is one we can merge to (ie, currently that means
  // that all elements are in memory).  The only thing we need to do to
  // merge is make this one mergable too.
  SpillAll();

  ASSERT(stack_pointer_ == expected->stack_pointer_);
}


void VirtualFrame::Enter() {
  Comment cmnt(masm_, "[ Enter JS frame");
  EmitPush(ebp);

  frame_pointer_ = stack_pointer_;
  __ mov(ebp, Operand(esp));

  // Store the context and the function in the frame.
  EmitPush(esi);
  EmitPush(edi);

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
  stack_pointer_ = frame_pointer_;
  for (int i = elements_.length() - 1; i > stack_pointer_; i--) {
    elements_.RemoveLast();
  }

  frame_pointer_ = kIllegalIndex;
  EmitPop(ebp);
}


void VirtualFrame::AllocateStackSlots(int count) {
  ASSERT(height() == 0);
  local_count_ = count;

  if (count > 0) {
    Comment cmnt(masm_, "[ Allocate space for locals");
    // The locals are constants (the undefined value), but we sync them with
    // the actual frame to allocate space for spilling them.
    FrameElement initial_value(Factory::undefined_value());
    initial_value.clear_dirty();
    __ Set(eax, Immediate(Factory::undefined_value()));
    for (int i = 0; i < count; i++) {
      elements_.Add(initial_value);
      stack_pointer_++;
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
  EmitPush(eax);
}


void VirtualFrame::CallStub(CodeStub* stub, int frame_arg_count) {
  PrepareForCall(frame_arg_count);
  __ CallStub(stub);
}


void VirtualFrame::CallRuntime(Runtime::Function* f, int frame_arg_count) {
  PrepareForCall(frame_arg_count);
  __ CallRuntime(f, frame_arg_count);
}


void VirtualFrame::CallRuntime(Runtime::FunctionId id, int frame_arg_count) {
  PrepareForCall(frame_arg_count);
  __ CallRuntime(id, frame_arg_count);
}


void VirtualFrame::InvokeBuiltin(Builtins::JavaScript id,
                                 InvokeFlag flag,
                                 int frame_arg_count) {
  PrepareForCall(frame_arg_count);
  __ InvokeBuiltin(id, flag);
}


void VirtualFrame::CallCodeObject(Handle<Code> code,
                                  RelocInfo::Mode rmode,
                                  int frame_arg_count) {
  PrepareForCall(frame_arg_count);
  __ call(code, rmode);
}


void VirtualFrame::Drop(int count) {
  ASSERT(height() >= count);

  // Discard elements above the stack pointer.
  while (count > 0 && stack_pointer_ < elements_.length() - 1) {
    elements_.RemoveLast();
  }

  // Discard the rest of the elements and lower the stack pointer.
  Forget(count);
  if (count > 0) {
    __ add(Operand(esp), Immediate(count * kPointerSize));
  }
}


void VirtualFrame::Drop() { Drop(1); }


void VirtualFrame::EmitPop(Register reg) {
  ASSERT(stack_pointer_ == elements_.length() - 1);
  stack_pointer_--;
  elements_.RemoveLast();
  __ pop(reg);
}


void VirtualFrame::EmitPop(Operand operand) {
  ASSERT(stack_pointer_ == elements_.length() - 1);
  stack_pointer_--;
  elements_.RemoveLast();
  __ pop(operand);
}


void VirtualFrame::EmitPush(Register reg) {
  ASSERT(stack_pointer_ == elements_.length() - 1);
  elements_.Add(FrameElement());
  stack_pointer_++;
  __ push(reg);
}


void VirtualFrame::EmitPush(Operand operand) {
  ASSERT(stack_pointer_ == elements_.length() - 1);
  elements_.Add(FrameElement());
  stack_pointer_++;
  __ push(operand);
}


void VirtualFrame::EmitPush(Immediate immediate) {
  ASSERT(stack_pointer_ == elements_.length() - 1);
  elements_.Add(FrameElement());
  stack_pointer_++;
  __ push(immediate);
}


#undef __

} }  // namespace v8::internal
