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

VirtualFrame::SpilledScope::SpilledScope(CodeGenerator* cgen)
    : cgen_(cgen),
      previous_state_(cgen->in_spilled_code()) {
  ASSERT(cgen->has_valid_frame());
  cgen->frame()->SpillAll();
  cgen->set_in_spilled_code(true);
}


VirtualFrame::SpilledScope::~SpilledScope() {
  cgen_->set_in_spilled_code(previous_state_);
}


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
  for (int i = 0; i < parameter_count_ + 2; i++) {
    elements_.Add(FrameElement::MemoryElement());
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
    elements_.Add(FrameElement::MemoryElement());
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


void VirtualFrame::Spill(Register target) {
  if (!frame_registers_.is_used(target)) return;
  for (int i = 0; i < elements_.length(); i++) {
    if (elements_[i].is_register() && elements_[i].reg().is(target)) {
      SpillElementAt(i);
    }
  }
}


// Spill any register if possible, making its external reference count zero.
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

  Register result = { best_register_code };
  if (result.is_valid()) {
    Spill(result);
    ASSERT(!cgen_->allocator()->is_used(result));
  }
  return result;
}


// Spill an element, making its type be MEMORY.
// Does not decrement usage counts, if element is a register.
void VirtualFrame::RawSpillElementAt(int index) {
  SyncElementAt(index);
  // The element is now in memory.
  elements_[index] = FrameElement::MemoryElement();
}


// Make the type of the element at a given index be MEMORY.
void VirtualFrame::SpillElementAt(int index) {
  if (elements_[index].is_register()) {
    Unuse(elements_[index].reg());
  }
  RawSpillElementAt(index);
}


// Clear the dirty bit for the element at a given index.
// The element must be on the physical stack, or the first
// element below the stack pointer (created by a single push).
void VirtualFrame::RawSyncElementAt(int index) {
  FrameElement element = elements_[index];

  if (!element.is_synced()) {
    if (index <= stack_pointer_) {
      // Write elements below the stack pointer to their (already allocated)
      // actual frame location.
      if (element.is_constant()) {
        __ mov(Operand(ebp, fp_relative(index)), Immediate(element.handle()));
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


// Clear the dirty bits for the range of elements in [begin, end).
void VirtualFrame::SyncRange(int begin, int end) {
  ASSERT(begin >= 0);
  ASSERT(end <= elements_.length());
  for (int i = begin; i < end; i++) {
    RawSyncElementAt(i);
  }
}


// Clear the dirty bit for the element at a given index.
void VirtualFrame::SyncElementAt(int index) {
  if (index > stack_pointer_ + 1) {
    SyncRange(stack_pointer_ + 1, index);
  }
  RawSyncElementAt(index);
}


// Make the type of all elements be MEMORY.
void VirtualFrame::SpillAll() {
  for (int i = 0; i < elements_.length(); i++) {
    SpillElementAt(i);
  }
}


void VirtualFrame::PrepareForCall(int frame_arg_count) {
  ASSERT(height() >= frame_arg_count);

  // Below the stack pointer, spill all registers and make sure that
  // locals have the right values by sync'ing them. The sync'ing is
  // necessary to give the debugger a consistent view of the values of
  // locals in the frame.
  for (int i = 0; i <= stack_pointer_; i++) {
    FrameElement element = elements_[i];
    if (element.is_register()) {
      SpillElementAt(i);
    } else if (element.is_valid() && i < expression_base_index()) {
      SyncElementAt(i);
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


bool VirtualFrame::RequiresMergeCode() {
  // A frame requires code to be generated to make the frame mergable if
  // there are duplicated non-synched registers or else valid elements not
  // in a (memory or register) location in the frame.  We look for valid
  // non-synced non-location elements and count occurrences of non-synced
  // registers.
  RegisterFile non_synced_regs;
  for (int i = 0; i < elements_.length(); i++) {
    FrameElement element = elements_[i];
    if (element.is_valid() && !element.is_synced()) {
      if (element.is_register()) {
        non_synced_regs.Use(elements_[i].reg());
      } else if (!element.is_memory()) {
        // Not memory or register and not synced.
        return true;
      }
    }
  }

  for (int i = 0; i < RegisterFile::kNumRegisters; i++) {
    if (non_synced_regs.count(i) > 1) {
      return true;
    }
  }

  return false;
}


void VirtualFrame::MakeMergable() {
  Comment cmnt(masm_, "[ Make frame mergable");
  // We can call MakeMergable on a frame that is not the code generator's
  // current frame, which will leave the global register counts out of sync
  // with the frame.  We simply save the current frame and restore it at the
  // end of this function.  We should find a better way to deal with this.
  VirtualFrame* original_frame = cgen_->frame();
  RegisterFile non_frame_registers;
  non_frame_registers.Use(esi);
  non_frame_registers.Use(ebp);
  non_frame_registers.Use(esp);
  cgen_->SetFrame(this, &non_frame_registers);
  ASSERT(cgen_->HasValidEntryRegisters());

  // Remove constants from the frame and ensure that no registers are
  // multiply referenced within the frame.  Allocate elements to their new
  // locations from the top down so that the topmost elements have a chance
  // to be in registers, then fill them into memory from the bottom up.
  // (NB: Currently when spilling registers that are multiply referenced, it
  // is the lowermost occurrence that gets to stay in the register.)

  // The elements of new_elements are initially invalid.
  FrameElement* new_elements = new FrameElement[elements_.length()];
  FrameElement memory_element = FrameElement::MemoryElement();
  for (int i = elements_.length() - 1; i >= 0; i--) {
    FrameElement element = elements_[i];
    if (element.is_constant() ||
        (element.is_register() &&
         frame_registers_.count(element.reg()) > 1)) {
      // A simple strategy is to locate these elements in memory if they are
      // synced (avoiding a spill right now) and otherwise to prefer a
      // register for them.
      if (element.is_synced()) {
        new_elements[i] = memory_element;
      } else {
        Result fresh = cgen_->allocator()->AllocateWithoutSpilling();
        if (fresh.is_valid()) {
          // We immediately record the frame's use of the register so that
          // it will not be allocated again.
          Use(fresh.reg());
          new_elements[i] =
              FrameElement::RegisterElement(fresh.reg(),
                                            FrameElement::NOT_SYNCED);
        } else {
          new_elements[i] = memory_element;
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
    ASSERT(!target.is_valid() || target.is_register() || target.is_memory());
    if (target.is_register()) {
      if (source.is_constant()) {
        __ mov(target.reg(), Immediate(source.handle()));
      } else if (source.is_register() && !source.reg().is(target.reg())) {
        __ mov(target.reg(), source.reg());
      }
      elements_[i] = target;
    } else if (target.is_memory()) {
      if (!source.is_memory()) {
        // Spilling a source register would decrement its reference count,
        // but we have already done that when computing new target elements,
        // so we use a raw spill.
        RawSpillElementAt(i);
      }
    }
    // Invalid elements do not need to be moved.
  }

  delete[] new_elements;
  ASSERT(cgen_->HasValidEntryRegisters());
  cgen_->SetFrame(original_frame, &non_frame_registers);
}


void VirtualFrame::MergeTo(VirtualFrame* expected) {
  Comment cmnt(masm_, "[ Merge frame");
  // We should always be merging the code generator's current frame to an
  // expected frame.
  ASSERT(cgen_->frame() == this);

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
  MergeMoveRegistersToMemory(expected);
  MergeMoveRegistersToRegisters(expected);
  MergeMoveMemoryToRegisters(expected);

  int height_difference = stack_pointer_ - expected->stack_pointer_;
  if (stack_pointer_ > expected->stack_pointer_) {
#ifdef DEBUG
    for (int i = stack_pointer_; i > expected->stack_pointer_; i--) {
      ASSERT(!elements_[i].is_memory());
      ASSERT(!elements_[i].is_synced());
    }
#endif
    __ add(Operand(esp), Immediate(height_difference * kPointerSize));
    stack_pointer_ = expected->stack_pointer_;
  } else if (stack_pointer_ < expected->stack_pointer_) {
    // Put valid data on the stack, that will only be accessed by GC.
    while (stack_pointer_ < expected->stack_pointer_) {
      __ push(Immediate(Smi::FromInt(0)));
      stack_pointer_++;
    }
  }

  // At this point, the frames should be identical.
  // TODO(): Consider an "equals" method for frames.
  ASSERT(stack_pointer_ == expected->stack_pointer_);
#ifdef DEBUG
  for (int i = 0; i < elements_.length(); i++) {
    FrameElement expect = expected->elements_[i];
    if (!expect.is_valid()) {
      ASSERT(!elements_[i].is_valid());
    } else if (expect.is_memory()) {
      ASSERT(elements_[i].is_memory());
      ASSERT(elements_[i].is_synced() && expect.is_synced());
    } else if (expect.is_register()) {
      ASSERT(elements_[i].is_register());
      ASSERT(elements_[i].reg().is(expect.reg()));
      ASSERT(elements_[i].is_synced() == expect.is_synced());
    } else {
      ASSERT(expect.is_constant());
      ASSERT(elements_[i].is_constant());
      ASSERT(elements_[i].handle().location() ==
             expect.handle().location());
      ASSERT(elements_[i].is_synced() == expect.is_synced());
    }
  }
#endif
}


void VirtualFrame::MergeMoveRegistersToMemory(VirtualFrame *expected) {
  for (int i = 0; i < elements_.length(); i++) {
    FrameElement source = elements_[i];
    FrameElement target = expected->elements_[i];
    if (target.is_memory() && !source.is_memory()) {
      ASSERT(source.is_register() || source.is_constant());
      SpillElementAt(i);
    }
  }
}


void VirtualFrame::MergeMoveRegistersToRegisters(VirtualFrame *expected) {
  int start = 0;
  int end = elements_.length() - 1;
  bool any_moves_blocked;  // Did we fail to make some moves this iteration?
  bool should_break_cycles = false;
  bool any_moves_made;  // Did we make any progress this iteration?
  do {
    any_moves_blocked = false;
    any_moves_made = false;
    int first_move_blocked = kIllegalIndex;
    int last_move_blocked = kIllegalIndex;
    for (int i = start; i <= end; i++) {
      FrameElement source = elements_[i];
      FrameElement target = expected->elements_[i];
      if (source.is_register() && target.is_register()) {
        if (target.reg().is(source.reg())) {
          if (target.is_synced() && !source.is_synced()) {
            SyncElementAt(i);
          }
          elements_[i] = target;
        } else {
          // We need to move source to target.
          if (frame_registers_.is_used(target.reg())) {
            // The move is blocked because the target contains valid data.
            // If we are stuck with only cycles remaining, then we spill source.
            // Otherwise, we just need more iterations.
            if (should_break_cycles) {
              SpillElementAt(i);
              should_break_cycles = false;
            } else {  // Record a blocked move.
              if (!any_moves_blocked) {
                first_move_blocked = i;
              }
              last_move_blocked = i;
              any_moves_blocked = true;
            }
          } else {
            // The move is not blocked.  This frame element can be moved from
            // its source register to its target register.
            if (target.is_synced() && !source.is_synced()) {
              SyncElementAt(i);
            }
            Use(target.reg());
            Unuse(source.reg());
            elements_[i] = target;
            __ mov(target.reg(), source.reg());
            any_moves_made = true;
          }
        }
      }
    }
    // Update control flags for next iteration.
    should_break_cycles = (any_moves_blocked && !any_moves_made);
    if (any_moves_blocked) {
      start = first_move_blocked;
      end = last_move_blocked;
    }
  } while (any_moves_blocked);
}


void VirtualFrame::MergeMoveMemoryToRegisters(VirtualFrame *expected) {
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
          // one can be popped, but first we may have to adjust the stack
          // pointer downward.
          if (stack_pointer_ > i) {
            // Sync elements between i and stack pointer, and bring
            // stack pointer down to i.
#ifdef DEBUG
            // In debug builds check to ensure this is safe.
            for (int j = stack_pointer_; j > i; j--) {
              ASSERT(!elements_[j].is_memory());
            }
#endif
            __ add(Operand(esp),
                   Immediate((stack_pointer_ - i) * kPointerSize));
            stack_pointer_ = i;
            }
          stack_pointer_--;
          __ pop(target.reg());
        }
      } else {
        // Source is constant.
        __ Set(target.reg(), Immediate(source.handle()));
        if (target.is_synced()) {
          SyncElementAt(i);
        }
      }
      Use(target.reg());
      elements_[i] = target;
    }
  }
}


void VirtualFrame::DetachFromCodeGenerator() {
  // Tell the global register allocator that it is free to reallocate all
  // register references contained in this frame.  The frame elements remain
  // register references, so the frame-internal reference count is not
  // decremented.
  for (int i = 0; i < elements_.length(); i++) {
    if (elements_[i].is_register()) {
      cgen_->allocator()->Unuse(elements_[i].reg());
    }
  }
}


void VirtualFrame::AttachToCodeGenerator() {
  // Tell the global register allocator that the frame-internal register
  // references are live again.
  for (int i = 0; i < elements_.length(); i++) {
    if (elements_[i].is_register()) {
      cgen_->allocator()->Use(elements_[i].reg());
    }
  }
}


void VirtualFrame::Enter() {
  // Registers live on entry: esp, ebp, esi, edi.
  Comment cmnt(masm_, "[ Enter JS frame");
  EmitPush(ebp);

  frame_pointer_ = stack_pointer_;
  __ mov(ebp, Operand(esp));

  // Store the context in the frame.  The context is kept in esi and a
  // copy is stored in the frame.  The external reference to esi
  // remains in addition to the cached copy in the frame.
  Push(esi);
  SyncElementAt(elements_.length() - 1);

  // Store the function in the frame.  The frame owns the register
  // reference now (ie, it can keep it in edi or spill it later).
  Push(edi);
  cgen_->allocator()->Unuse(edi);
  SpillElementAt(elements_.length() - 1);
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


void VirtualFrame::PrepareForReturn() {
  // Spill all locals. This is necessary to make sure all locals have
  // the right value when breaking at the return site in the debugger.
  for (int i = 0; i < expression_base_index(); i++) SpillElementAt(i);

  // Drop all non-local stack elements.
  Drop(height());

  // Validate state: The expression stack should be empty and the
  // stack pointer should have been updated to reflect this.
  ASSERT(height() == 0);
  ASSERT(stack_pointer_ == expression_base_index() - 1);
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
    FrameElement initial_value =
        FrameElement::ConstantElement(undefined, FrameElement::SYNCED);
    Result temp = cgen_->allocator()->Allocate();
    ASSERT(temp.is_valid());
    __ Set(temp.reg(), Immediate(undefined));
    for (int i = 0; i < count; i++) {
      elements_.Add(initial_value);
      stack_pointer_++;
      __ push(temp.reg());
    }
  }
}


void VirtualFrame::SetElementAt(int index, Result* value) {
  int frame_index = elements_.length() - index - 1;
  ASSERT(frame_index >= 0);
  ASSERT(frame_index < elements_.length());
  ASSERT(value->is_valid());
  FrameElement target = elements_[frame_index];

  if (target.is_register()) {
    Unuse(target.reg());
  }

  if (value->is_register()) {
    Use(value->reg());
    // Write the new value to the frame, if it is changed.
    // Otherwise, if target equals value, keep its current sync state.
    if (!target.is_register() ||
        !value->reg().is(target.reg())) {
      elements_[frame_index] =
          FrameElement::RegisterElement(value->reg(),
                                        FrameElement::NOT_SYNCED);
    }
  } else {
    ASSERT(value->is_constant());
    // Write the new value to the frame element, if it is a change.
    // Otherwise, do nothing, and keep the current sync state.
    if (!target.is_constant() ||
        !value->handle().is_identical_to(target.handle())) {
      elements_[frame_index] =
          FrameElement::ConstantElement(value->handle(),
                                        FrameElement::NOT_SYNCED);
    }
  }
  value->Unuse();
}


void VirtualFrame::SaveContextRegister() {
  FrameElement current = elements_[context_index()];
  ASSERT(current.is_register() || current.is_memory());
  if (!current.is_register() || !current.reg().is(esi)) {
    if (current.is_register()) {
      Unuse(current.reg());
    }
    Use(esi);
    elements_[context_index()] =
        FrameElement::RegisterElement(esi, FrameElement::NOT_SYNCED);
  }
}


void VirtualFrame::RestoreContextRegister() {
  FrameElement current = elements_[context_index()];
  ASSERT(current.is_register() || current.is_memory());
  if (current.is_register() && !current.reg().is(esi)) {
    Unuse(current.reg());
    Use(esi);
    __ mov(esi, current.reg());
    elements_[context_index()] =
        FrameElement::RegisterElement(esi, FrameElement::NOT_SYNCED);
  } else if (current.is_memory()) {
    Use(esi);
    __ mov(esi, Operand(ebp, fp_relative(context_index())));
    elements_[context_index()] =
        FrameElement::RegisterElement(esi, FrameElement::SYNCED);
  }
}


void VirtualFrame::LoadFrameSlotAt(int index) {
  ASSERT(index >= 0);
  ASSERT(index < elements_.length());

  FrameElement element = elements_[index];

  if (element.is_memory()) {
    ASSERT(index <= stack_pointer_);
    // Eagerly load memory elements into a register.  The element at
    // the index and the new top of the frame are backed by the same
    // register location.
    Result temp = cgen_->allocator()->Allocate();
    ASSERT(temp.is_valid());
    FrameElement new_element
        = FrameElement::RegisterElement(temp.reg(),
                                        FrameElement::SYNCED);
    Use(temp.reg());
    elements_[index] = new_element;
    __ mov(temp.reg(), Operand(ebp, fp_relative(index)));

    Use(temp.reg());
    new_element.clear_sync();
    elements_.Add(new_element);
  } else {
    // For constants and registers, add an (unsynced) copy of the element to
    // the top of the frame.
    ASSERT(element.is_register() || element.is_constant());
    if (element.is_register()) {
      Use(element.reg());
    }
    element.clear_sync();
    elements_.Add(element);
  }
}


void VirtualFrame::TakeFrameSlotAt(int index) {
  LoadFrameSlotAt(index);

  if (elements_[index].is_register()) {
    Unuse(elements_[index].reg());
  }
  elements_[index] = FrameElement::InvalidElement();
}


void VirtualFrame::StoreToFrameSlotAt(int index) {
  // Store the value on top of the frame to the virtual frame slot at a
  // given index.  The value on top of the frame is left in place.
  ASSERT(index >= 0);
  ASSERT(index < elements_.length());
  FrameElement top = elements_[elements_.length() - 1];

  if (elements_[index].is_register()) {
    Unuse(elements_[index].reg());
  }
  // The virtual frame slot will be of the same type and have the same value
  // as the frame top.
  elements_[index] = top;

  if (top.is_memory()) {
    // TODO(): consider allocating the slot to a register.
    //
    // Emit code to store memory values into the required frame slot.
    Result temp = cgen_->allocator()->Allocate();
    ASSERT(temp.is_valid());
    __ mov(temp.reg(), Top());
    __ mov(Operand(ebp, fp_relative(index)), temp.reg());
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
  ASSERT(cgen_->HasValidEntryRegisters());
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
  ASSERT(cgen_->HasValidEntryRegisters());
  PrepareForCall(frame_arg_count);
  __ CallStub(stub);
}


Result VirtualFrame::CallStub(CodeStub* stub,
                              Result* arg,
                              int frame_arg_count) {
  arg->Unuse();
  CallStub(stub, frame_arg_count);
  Result result = cgen_->allocator()->Allocate(eax);
  ASSERT(result.is_valid());
  return result;
}


Result VirtualFrame::CallStub(CodeStub* stub,
                              Result* arg0,
                              Result* arg1,
                              int frame_arg_count) {
  arg0->Unuse();
  arg1->Unuse();
  CallStub(stub, frame_arg_count);
  Result result = cgen_->allocator()->Allocate(eax);
  ASSERT(result.is_valid());
  return result;
}


Result VirtualFrame::CallRuntime(Runtime::Function* f,
                                 int frame_arg_count) {
  ASSERT(cgen_->HasValidEntryRegisters());
  PrepareForCall(frame_arg_count);
  __ CallRuntime(f, frame_arg_count);
  Result result = cgen_->allocator()->Allocate(eax);
  ASSERT(result.is_valid());
  return result;
}


Result VirtualFrame::CallRuntime(Runtime::FunctionId id,
                                 int frame_arg_count) {
  ASSERT(cgen_->HasValidEntryRegisters());
  PrepareForCall(frame_arg_count);
  __ CallRuntime(id, frame_arg_count);
  Result result = cgen_->allocator()->Allocate(eax);
  ASSERT(result.is_valid());
  return result;
}


void VirtualFrame::InvokeBuiltin(Builtins::JavaScript id,
                                 InvokeFlag flag,
                                 int frame_arg_count) {
  ASSERT(cgen_->HasValidEntryRegisters());
  PrepareForCall(frame_arg_count);
  __ InvokeBuiltin(id, flag);
}


Result VirtualFrame::CallCodeObject(Handle<Code> code,
                                    RelocInfo::Mode rmode,
                                    int frame_arg_count) {
  ASSERT(cgen_->HasValidEntryRegisters());
  PrepareForCall(frame_arg_count);
  __ call(code, rmode);
  Result result = cgen_->allocator()->Allocate(eax);
  ASSERT(result.is_valid());
  return result;
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
    Result temp = cgen_->allocator()->Allocate();
    ASSERT(temp.is_valid());
    ASSERT(pop_needed);
    stack_pointer_--;
    __ pop(temp.reg());
    return temp;
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
  elements_.Add(FrameElement::MemoryElement());
  stack_pointer_++;
  __ push(reg);
}


void VirtualFrame::EmitPush(Operand operand) {
  ASSERT(stack_pointer_ == elements_.length() - 1);
  elements_.Add(FrameElement::MemoryElement());
  stack_pointer_++;
  __ push(operand);
}


void VirtualFrame::EmitPush(Immediate immediate) {
  ASSERT(stack_pointer_ == elements_.length() - 1);
  elements_.Add(FrameElement::MemoryElement());
  stack_pointer_++;
  __ push(immediate);
}


void VirtualFrame::Push(Register reg) {
  Use(reg);
  elements_.Add(FrameElement::RegisterElement(reg,
                                              FrameElement::NOT_SYNCED));
}


void VirtualFrame::Push(Handle<Object> value) {
  elements_.Add(FrameElement::ConstantElement(value,
                                              FrameElement::NOT_SYNCED));
}


void VirtualFrame::Push(Result* result) {
  if (result->is_register()) {
    Push(result->reg());
  } else {
    ASSERT(result->is_constant());
    Push(result->handle());
  }
  result->Unuse();
}


void VirtualFrame::Nip(int num_dropped) {
  ASSERT(num_dropped >= 0);
  if (num_dropped == 0) return;
  Result tos = Pop();
  if (num_dropped > 1) {
    Drop(num_dropped - 1);
  }
  SetElementAt(0, &tos);
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
