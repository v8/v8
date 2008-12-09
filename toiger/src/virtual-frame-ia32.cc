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
// Result implementation.


Result::Result(Register reg, CodeGenerator* cgen)
  : type_(REGISTER),
    cgen_(cgen) {
  data_.reg_ = reg;
  ASSERT(!reg().is(no_reg));
  cgen_->allocator()->Use(reg);
}


void Result::Unuse() {
  ASSERT(!reg().is(no_reg));
  cgen_->allocator()->Unuse(reg());
  data_.reg_ = no_reg;
}


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
    // occurrences, stop when we have spilled them all to avoid syncing
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
      }
      i++;
    }
  }

  ASSERT(cgen_->allocator()->count(best_register_code) == 0);
  Register result = { best_register_code };
  return result;
}


// Make the type of the element at a given index be MEMORY.  We can only
// allocate space in the actual frame for the virtual element immediately
// above the stack pointer.
void VirtualFrame::SpillElementAt(int index) {
  if (index > stack_pointer_ + 1) {
    SyncRange(stack_pointer_ + 1, index);
  }
  SyncElementAt(index);
  // The element is now in memory.
  if (elements_[index].is_register()) {
    Unuse(elements_[index].reg());
  }
  elements_[index] = FrameElement();
}


// Clear the dirty bits for the range of elements in [begin, end).
void VirtualFrame::SyncRange(int begin, int end) {
  ASSERT(begin >= 0);
  ASSERT(end <= elements_.length());
  for (int i = begin; i < end; i++) {
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


bool VirtualFrame::IsMergable() {
  // We cannot merge to a frame that has constants as elements, because an
  // arbitrary frame may not have the same constants at those locations.  We
  // cannot merge to a frame that has registers that are mulitply referenced
  // in the frame, because an arbitrary frame might not exhibit the same
  // sharing.  Thus, a frame is mergable if all elements are in memory or a
  // register and no register is multiply referenced.
  for (int i = 0; i < RegisterFile::kNumRegisters; i++) {
    if (frame_registers_.count(i) > 1) {
      return false;
    }
  }

  for (int i = 0; i < elements_.length(); i++) {
    if (!elements_[i].is_memory() && !elements_[i].is_register()) {
      return false;
    }
  }

  return true;
}


void VirtualFrame::MakeMergable() {
  Comment cmnt(masm_, "[ Make frame mergable");
  // Remove constants from the frame and ensure that no registers are
  // multiply referenced within the frame.  Allocate elements to their
  // new locations from the top down so that the topmost elements have
  // a chance to be in registers, then fill them into memory from the
  // bottom up.  (NB: Currently when spilling registers that are
  // multiply referenced, it is the lowermost occurrence that gets to
  // stay in the register.)
  FrameElement* new_elements = new FrameElement[elements_.length()];
  FrameElement memory_element;
  for (int i = elements_.length() - 1; i >= 0; i--) {
    FrameElement element = elements_[i];
    if (element.is_constant() ||
        (element.is_register() &&
         frame_registers_.count(element.reg().code()) > 1)) {
      // A simple strategy is to locate these elements in memory if they are
      // synced (avoiding a spill right now) and otherwise to prefer a
      // register for them.
      if (element.is_synced()) {
        new_elements[i] = memory_element;
      } else {
        // This code path is currently not triggered.  UNIMPLEMENTED is
        // temporarily used to trap when it becomes active so we can test
        // it.
        UNIMPLEMENTED();
        Register reg = cgen_->allocator()->AllocateWithoutSpilling();
        if (reg.is(no_reg)) {
          new_elements[i] = memory_element;
        } else {
          FrameElement register_element(reg, FrameElement::NOT_SYNCED);
          new_elements[i] = register_element;
        }
      }

      // We have not moved register references, but record that we will so
      // that we do not unnecessarily spill the last reference within the
      // frame.
      if (element.is_register()) {
        Unuse(element.reg());
      }
    } else {
      // The element is in memory or a singly-frame-referenced register.
      new_elements[i] = element;
    }
  }

  // Perform the moves.
  for (int i = 0; i < elements_.length(); i++) {
    FrameElement source = elements_[i];
    FrameElement target = new_elements[i];
    ASSERT(target.is_register() || target.is_memory());
    if (target.is_register()) {
      if (source.is_constant()) {
        // The allocator's register reference count was incremented by
        // register allocation, so we only record the new reference in the
        // frame.  The frame now owns the reference.
        frame_registers_.Use(target.reg());
        __ Set(target.reg(), Immediate(source.handle()));
      } else if (source.is_register() && !source.reg().is(target.reg())) {
        // The frame now owns the reference.
        frame_registers_.Use(target.reg());
        __ mov(target.reg(), source.reg());
      }
      elements_[i] = target;
    } else {
      // The target is memory.
      SpillElementAt(i);
    }
  }

  delete[] new_elements;
}


void VirtualFrame::MergeTo(VirtualFrame* expected) {
  Comment cmnt(masm_, "[ Merge frame");
  ASSERT(cgen_ == expected->cgen_);
  ASSERT(masm_ == expected->masm_);
  ASSERT(elements_.length() == expected->elements_.length());
  ASSERT(parameter_count_ == expected->parameter_count_);
  ASSERT(local_count_ == expected->local_count_);
  ASSERT(frame_pointer_ == expected->frame_pointer_);

  // Mergable frames have all elements in locations, either memory or
  // register.  We thus have a series of to-memory and to-register moves.
  // First perform all to-memory moves, register-to-memory moves because
  // they can free registers and constant-to-memory moves because they do
  // not use registers.
  for (int i = 0; i < elements_.length(); i++) {
    FrameElement source = elements_[i];
    FrameElement target = expected->elements_[i];
    if (target.is_memory() && !source.is_memory()) {
      ASSERT(source.is_register() || source.is_constant());
      SpillElementAt(i);
    }
  }

  // Then register-to-register moves, not yet implemented.
  for (int i = 0; i < elements_.length(); i++) {
    FrameElement source = elements_[i];
    FrameElement target = expected->elements_[i];
    ASSERT(!source.is_register() || !target.is_register());
  }

  // Finally, constant-to-register and memory-to-register.  We do these from
  // the top down so we can use pop for memory-to-register moves above the
  // expected stack pointer.
  for (int i = elements_.length() - 1; i >= 0; i--) {
    FrameElement source = elements_[i];
    FrameElement target = expected->elements_[i];
    if (target.is_register() && !source.is_register()) {
      ASSERT(source.is_constant() || source.is_memory());
      if (source.is_memory()) {
        ASSERT(i <= stack_pointer_);
        if (i <= expected->stack_pointer_) {
          // Elements below both stack pointers can just be moved.
          __ mov(target.reg(), Operand(ebp, fp_relative(i)));
        } else {
          // Elements below the current stack pointer but above the expected
          // one can be popped, bet first we may have to adjust the stack
          // pointer downward.
          if (stack_pointer_ > i + 1) {
#ifdef DEBUG
            // In debug builds check to ensure this is safe.
            for (int j = stack_pointer_; j > i; j--) {
              ASSERT(!elements_[j].is_memory());
            }
#endif
            stack_pointer_ = i + 1;
            __ add(Operand(esp),
                   Immediate((stack_pointer_ - i) * kPointerSize));
          }
          stack_pointer_--;
          __ pop(target.reg());
        }
        Use(target.reg());
      } else if (source.is_constant()) {
        // Not yet implemented.  When done, code in common with the
        // memory-to-register just above case can be factored out.
        UNIMPLEMENTED();
      }
      elements_[i] = target;
    }
  }

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
    // The locals are initialized to a constant (the undefined value), but
    // we sync them with the actual frame to allocate space for spilling
    // them later.  First sync everything above the stack pointer so we can
    // use pushes to allocate and initialize the locals.
    SyncRange(stack_pointer_ + 1, elements_.length());
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
    // We have not actually written the value to memory.
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


/*
We need comparison with literal to work.
It will get Result, is register or constant.
Pop gives it this, from register, constant, memory, or
reference to slot.

In comparison to literal, we need register case to work.
We need non-smi stub to exit and return with a non-spilled frame.
*/


Result VirtualFrame::Pop() {
  FrameElement popped = elements_.RemoveLast();
  bool pop_needed = (stack_pointer_ == elements_.length());

  if (popped.is_constant()) {
    if (pop_needed) {
      stack_pointer_--;
      __ add(Operand(esp), Immediate(kPointerSize));
    }
    return Result(popped.handle(), cgen_);
  } else if (popped.is_register()) {
    Unuse(popped.reg());
    if (pop_needed) {
      stack_pointer_--;
      __ add(Operand(esp), Immediate(kPointerSize));
    }
    return Result(popped.reg(), cgen_);
  } else {
    ASSERT(popped.is_memory());
    Register temp = cgen_->allocator()->Allocate();
    ASSERT(!temp.is(no_reg));
    ASSERT(pop_needed);
    stack_pointer_--;
    __ pop(temp);
    // The register temp is double counted, by Allocate and Result(temp).
    cgen_->allocator()->Unuse(temp);
    return Result(temp, cgen_);
  }
}

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
