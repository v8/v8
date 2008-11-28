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
    : cgen_(cgen),
      masm_(cgen->masm()),
      elements_(0),
      parameter_count_(cgen->scope()->num_parameters()),
      local_count_(0),
      stack_pointer_(parameter_count_ + 1),  // 0-based index of TOS.
      frame_pointer_(kIllegalIndex) {
  FrameElement memory_element;
  for (int i = 0; i < parameter_count_ + 2; i++) {
    elements_.Add(memory_element);
  }
}


// When cloned, a frame is a deep copy of the original.
VirtualFrame::VirtualFrame(VirtualFrame* original)
    : cgen_(original->cgen_),
      masm_(original->masm_),
      elements_(original->elements_.length()),
      parameter_count_(original->parameter_count_),
      local_count_(original->local_count_),
      stack_pointer_(original->stack_pointer_),
      frame_pointer_(original->frame_pointer_),
      frame_registers_(original->frame_registers_) {
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
    FrameElement last = elements_.RemoveLast();
    if (last.is_register()) {
      Unuse(last.reg());
    }
  }
}


void VirtualFrame::Use(Register reg) {
  frame_registers_.Use(reg);
  cgen_->allocator()->Use(reg);
}


void VirtualFrame::Unuse(Register reg) {
  frame_registers_.Unuse(reg);
  cgen_->allocator()->Unuse(reg);
}


// Clear the dirty bit for the element at a given index.  We can only
// allocate space in the actual frame for the virtual element immediately
// above the stack pointer.
void VirtualFrame::SyncElementAt(int index) {
  FrameElement element = elements_[index];

  if (!element.is_synced()) {
    if (index <= stack_pointer_) {
      // Write elements below the stack pointer to their (already allocated)
      // actual frame location.
      if (element.is_constant()) {
        __ Set(Operand(ebp, fp_relative(index)), Immediate(element.handle()));
      } else {
        ASSERT(element.is_register());
        __ mov(Operand(ebp, fp_relative(index)), element.reg());
      }
    } else {
      // Push elements above the stack pointer to allocate space and sync
      // them.  Space should have already been allocated in the actual frame
      // for all the elements below this one.
      ASSERT(index == stack_pointer_ + 1);
      stack_pointer_++;
      if (element.is_constant()) {
        __ push(Immediate(element.handle()));
      } else {
        ASSERT(element.is_register());
        __ push(element.reg());
      }
    }

    elements_[index].set_sync();
  }
}


// Spill any register if possible, making its reference count zero.
Register VirtualFrame::SpillAnyRegister() {
  // Find the leftmost (ordered by register code), least
  // internally-referenced register whose internal reference count matches
  // its external reference count (so that spilling it from the frame frees
  // it for use).
  int min_count = kMaxInt;
  int best_register_code = no_reg.code_;

  for (int i = 0; i < RegisterFile::kNumRegisters; i++) {
    int count = frame_registers_.count(i);
    if (count < min_count && count == cgen_->allocator()->count(i)) {
      min_count = count;
      best_register_code = i;
    }
  }

  if (best_register_code != no_reg.code_) {
    // Spill all occurrences of the register.  There are min_count
    // occurrences, stop when we've spilled them all to avoid syncing
    // elements unnecessarily.
    int i = 0;
    while (min_count > 0) {
      ASSERT(i < elements_.length());
      if (elements_[i].is_register() &&
          elements_[i].reg().code() == best_register_code) {
        // Found an instance of the best_register being used in the frame.
        // Spill it.
        SpillElementAt(i);
        min_count--;
      } else {
        if (i > stack_pointer_) {
          // Make sure to materialize elements on the virtual frame in
          // memory.  We rely on this to spill occurrences of the register
          // lying above the current virtual stack pointer.
          SyncElementAt(i);
        }
      }
    }
  }

  Register result = { best_register_code };
  return result;
}


// Make the type of the element at a given index be MEMORY.  We can only
// allocate space in the actual frame for the virtual element immediately
// above the stack pointer.
void VirtualFrame::SpillElementAt(int index) {
  SyncElementAt(index);
  // The element is now in memory.
  if (elements_[index].is_register()) {
    Unuse(elements_[index].reg());
  }
  elements_[index] = FrameElement();
}


// Clear the dirty bits for all elements.
void VirtualFrame::SyncAll() {
  for (int i = 0; i < elements_.length(); i++) {
    SyncElementAt(i);
  }
}


// Make the type of all elements be MEMORY.
void VirtualFrame::SpillAll() {
  for (int i = 0; i < elements_.length(); i++) {
    SpillElementAt(i);
  }
}


void VirtualFrame::PrepareForCall(int frame_arg_count) {
  ASSERT(height() >= frame_arg_count);

  // Below the stack pointer, spill all registers.
  for (int i = 0; i <= stack_pointer_; i++) {
    if (elements_[i].is_register()) {
      SpillElementAt(i);
    }
  }

  // Above the stack pointer, spill registers and sync everything else (ie,
  // constants).
  for (int i = stack_pointer_ + 1; i < elements_.length(); i++) {
    if (elements_[i].is_register()) {
      SpillElementAt(i);
    } else {
      SyncElementAt(i);
    }
  }

  // Forget the frame elements that will be popped by the call.
  Forget(frame_arg_count);
}


void VirtualFrame::EnsureMergable() {
  // We cannot merge to a frame that has constants as elements, because an
  // arbitrary frame might not have constants in those locations.
  //
  // We cannot merge to a frame that has registers as elements because we
  // haven't implemented merging for such frames yet.
  SpillAll();
}


void VirtualFrame::MergeTo(VirtualFrame* expected) {
  ASSERT(cgen_ == expected->cgen_);
  ASSERT(masm_ == expected->masm_);
  ASSERT(elements_.length() == expected->elements_.length());
  ASSERT(parameter_count_ == expected->parameter_count_);
  ASSERT(local_count_ == expected->local_count_);
  ASSERT(frame_pointer_ == expected->frame_pointer_);

  // Mergable frames do not have constants and they do not (currently) have
  // registers.  They are always fully spilled, so the only thing needed to
  // make this frame match the expected one is to spill everything.
  //
  // TODO(): Implement a non-stupid way of merging frames.
  SpillAll();

  ASSERT(stack_pointer_ == expected->stack_pointer_);
}


void VirtualFrame::Enter() {
  // Registers live on entry: esp, ebp, esi, edi.
  Comment cmnt(masm_, "[ Enter JS frame");
  EmitPush(ebp);

  frame_pointer_ = stack_pointer_;
  __ mov(ebp, Operand(esp));

  // Store the context and the function in the frame.
  Push(esi);
  // The frame owns the register reference now.
  cgen_->allocator()->Unuse(esi);

  Push(edi);
  cgen_->allocator()->Unuse(edi);
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
    FrameElement last = elements_.RemoveLast();
    if (last.is_register()) {
      Unuse(last.reg());
    }
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
    SyncAll();
    Handle<Object> undefined = Factory::undefined_value();
    FrameElement initial_value(undefined, FrameElement::SYNCED);
    Register tmp = cgen_->allocator()->Allocate();
    __ Set(tmp, Immediate(undefined));
    for (int i = 0; i < count; i++) {
      elements_.Add(initial_value);
      stack_pointer_++;
      __ push(tmp);
    }
    cgen_->allocator()->Unuse(tmp);
  }
}


void VirtualFrame::StoreToFrameSlotAt(int index) {
  // Store the value on top of the frame to the virtual frame slot at a
  // given index.  The value on top of the frame is left in place.
  ASSERT(index < elements_.length());
  FrameElement top = elements_[elements_.length() - 1];

  // The virtual frame slot is now of the same type and has the same value
  // as the frame top.
  if (elements_[index].is_register()) {
    Unuse(elements_[index].reg());
  }
  elements_[index] = top;

  if (top.is_memory()) {
    // Emit code to store memory values into the required frame slot.
    Register temp = cgen_->allocator()->Allocate();
    ASSERT(!temp.is(no_reg));
    __ mov(temp, Top());
    __ mov(Operand(ebp, fp_relative(index)), temp);
    cgen_->allocator()->Unuse(temp);
  } else {
    // We haven't actually written the value to memory.
    elements_[index].clear_sync();

    if (top.is_register()) {
      // Establish another frame-internal reference to the register.
      Use(top.reg());
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
  int num_virtual_elements = (elements_.length() - 1) - stack_pointer_;

  // Emit code to lower the stack pointer if necessary.
  if (num_virtual_elements < count) {
    int num_dropped = count - num_virtual_elements;
    stack_pointer_ -= num_dropped;
    __ add(Operand(esp), Immediate(num_dropped * kPointerSize));
  }

  // Discard elements from the virtual frame and free any registers.
  for (int i = 0; i < count; i++) {
    FrameElement dropped = elements_.RemoveLast();
    if (dropped.is_register()) {
      Unuse(dropped.reg());
    }
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
  FrameElement memory_element;
  elements_.Add(memory_element);
  stack_pointer_++;
  __ push(immediate);
}


void VirtualFrame::Push(Register reg) {
  FrameElement register_element(reg, FrameElement::NOT_SYNCED);
  Use(reg);
  elements_.Add(register_element);
}


void VirtualFrame::Push(Handle<Object> value) {
  FrameElement constant_element(value, FrameElement::NOT_SYNCED);
  elements_.Add(constant_element);
}


#ifdef DEBUG
bool VirtualFrame::IsSpilled() {
  for (int i = 0; i < elements_.length(); i++) {
    if (!elements_[i].is_memory()) {
      return false;
    }
  }
  return true;
}
#endif

#undef __

} }  // namespace v8::internal
