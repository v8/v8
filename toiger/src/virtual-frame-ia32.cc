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


FrameElement VirtualFrame::CopyElementAt(int index) {
  ASSERT(index >= 0);
  ASSERT(index < elements_.length());

  FrameElement target = elements_[index];
  FrameElement result;

  switch (target.type()) {
    case FrameElement::CONSTANT:
      // We do not copy constants and instead return a fresh unsynced
      // constant.
      result = FrameElement::ConstantElement(target.handle(),
                                             FrameElement::NOT_SYNCED);
      break;

    case FrameElement::COPY:
      // We do not allow copies of copies, so we follow one link to
      // the actual backing store of a copy before making a copy.
      index = target.index();
      ASSERT(elements_[index].is_memory() || elements_[index].is_register());
      // Fall through.

    case FrameElement::MEMORY:  // Fall through.
    case FrameElement::REGISTER:
      // All copies are backed by memory or register locations.
      result.type_ =
          FrameElement::TypeField::encode(FrameElement::COPY) |
          FrameElement::SyncField::encode(FrameElement::NOT_SYNCED);
      result.data_.index_ = index;
      break;

    case FrameElement::INVALID:
      // We should not try to copy invalid elements.
      UNREACHABLE();
      break;
  }
  return result;
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

  for (int i = 0; i < kNumRegisters; i++) {
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


// Make the type of the element at a given index be MEMORY.
void VirtualFrame::SpillElementAt(int index) {
  if (!elements_[index].is_valid()) return;

  if (elements_[index].is_register()) {
    Unuse(elements_[index].reg());
  }
  SyncElementAt(index);
  // The element is now in memory.
  elements_[index] = FrameElement::MemoryElement();
}


// Clear the dirty bit for the element at a given index if it is a
// valid element.  The stack address corresponding to the element must
// be allocated on the physical stack, or the first element above the
// stack pointer so it can be allocated by a single push instruction.
void VirtualFrame::RawSyncElementAt(int index) {
  FrameElement element = elements_[index];

  if (!element.is_valid() || element.is_synced()) return;

  if (index <= stack_pointer_) {
    // Emit code to write elements below the stack pointer to their
    // (already allocated) stack address.
    switch (element.type()) {
      case FrameElement::INVALID:  // Fall through.
      case FrameElement::MEMORY:
        // There was an early bailout for invalid and synced elements
        // (memory elements are always synced).
        UNREACHABLE();
        break;

      case FrameElement::REGISTER:
        __ mov(Operand(ebp, fp_relative(index)), element.reg());
        break;

      case FrameElement::CONSTANT:
        __ Set(Operand(ebp, fp_relative(index)), Immediate(element.handle()));
        break;

      case FrameElement::COPY: {
        int backing_index = element.index();
        FrameElement backing_element = elements_[backing_index];
        if (backing_element.is_memory()) {
          Result temp = cgen_->allocator()->Allocate();
          ASSERT(temp.is_valid());
          __ mov(temp.reg(), Operand(ebp, fp_relative(backing_index)));
          __ mov(Operand(ebp, fp_relative(index)), temp.reg());
        } else {
          ASSERT(backing_element.is_register());
          __ mov(Operand(ebp, fp_relative(index)), backing_element.reg());
        }
        break;
      }
    }

  } else {
    // Push elements above the stack pointer to allocate space and
    // sync them.  Space should have already been allocated in the
    // actual frame for all the elements below this one.
    ASSERT(index == stack_pointer_ + 1);
    stack_pointer_++;
    switch (element.type()) {
      case FrameElement::INVALID:  // Fall through.
      case FrameElement::MEMORY:
        // There was an early bailout for invalid and synced elements
        // (memory elements are always synced).
        UNREACHABLE();
        break;

      case FrameElement::REGISTER:
        __ push(element.reg());
        break;

      case FrameElement::CONSTANT:
        __ push(Immediate(element.handle()));
        break;

      case FrameElement::COPY: {
        int backing_index = element.index();
        FrameElement backing = elements_[backing_index];
        ASSERT(backing.is_memory() || backing.is_register());
        if (backing.is_memory()) {
          __ push(Operand(ebp, fp_relative(backing_index)));
        } else {
          __ push(backing.reg());
        }
        break;
      }
    }
  }

  elements_[index].set_sync();
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


void VirtualFrame::PrepareForCall(int spilled_args, int dropped_args) {
  ASSERT(height() >= dropped_args);
  ASSERT(height() >= spilled_args);
  ASSERT(dropped_args <= spilled_args);

  int arg_base_index = elements_.length() - spilled_args;
  // Spill the arguments.  We spill from the top down so that the
  // backing stores of register copies will be spilled only after all
  // the copies are spilled---it is better to spill via a
  // register-to-memory move than a memory-to-memory move.
  for (int i = elements_.length() - 1; i >= arg_base_index; i--) {
    SpillElementAt(i);
  }

  // Below the arguments, spill registers and sync everything else.
  // Syncing is necessary for the locals and parameters to give the
  // debugger a consistent view of the frame.
  for (int i = arg_base_index - 1; i >= 0; i--) {
    FrameElement element = elements_[i];
    if (element.is_register()) {
      SpillElementAt(i);
    } else if (element.is_valid()) {
      SyncElementAt(i);
    }
  }

  // Forget the frame elements that will be popped by the call.
  Forget(dropped_args);
}


void VirtualFrame::MergeTo(VirtualFrame* expected) {
  Comment cmnt(masm_, "[ Merge frame");
  // We should always be merging the code generator's current frame to an
  // expected frame.
  ASSERT(cgen_->frame() == this);

  // Adjust the stack pointer upward (toward the top of the virtual
  // frame) if necessary.
  if (stack_pointer_ < expected->stack_pointer_) {
    int difference = expected->stack_pointer_ - stack_pointer_;
    stack_pointer_ = expected->stack_pointer_;
    __ sub(Operand(esp), Immediate(difference * kPointerSize));
  }

  MergeMoveRegistersToMemory(expected);
  MergeMoveRegistersToRegisters(expected);
  MergeMoveMemoryToRegisters(expected);

  // Fix any sync bit problems.
  for (int i = 0; i <= stack_pointer_; i++) {
    FrameElement source = elements_[i];
    FrameElement target = expected->elements_[i];
    if (source.is_synced() && !target.is_synced()) {
      elements_[i].clear_sync();
    } else if (!source.is_synced() && target.is_synced()) {
      SyncElementAt(i);
    }
  }

  // Adjust the stack point downard if necessary.
  if (stack_pointer_ > expected->stack_pointer_) {
    int difference = stack_pointer_ - expected->stack_pointer_;
    stack_pointer_ = expected->stack_pointer_;
    __ add(Operand(esp), Immediate(difference * kPointerSize));
  }

  // At this point, the frames should be identical.
  ASSERT(Equals(expected));
}


void VirtualFrame::MergeMoveRegistersToMemory(VirtualFrame* expected) {
  ASSERT(stack_pointer_ >= expected->stack_pointer_);

  // Move registers, constants, and copies to memory.  Perform moves
  // from the top downward in the frame in order to leave the backing
  // stores of copies in registers.
  //
  // Moving memory-backed copies to memory requires a spare register
  // for the memory-to-memory moves.  Since we are performing a merge,
  // we use esi (which is already saved in the frame).  We keep track
  // of the index of the frame element esi is caching or kIllegalIndex
  // if esi has not been disturbed.
  int esi_caches = kIllegalIndex;
  // A "singleton" memory element.
  FrameElement memory_element = FrameElement::MemoryElement();
  for (int i = stack_pointer_; i >= 0; i--) {
    FrameElement target = expected->elements_[i];
    if (target.is_memory()) {
      FrameElement source = elements_[i];
      switch (source.type()) {
        case FrameElement::INVALID:
          // Not a legal merge move.
          UNREACHABLE();
          break;

        case FrameElement::MEMORY:
          // Already in place.
          break;

        case FrameElement::REGISTER:
          Unuse(source.reg());
          if (!source.is_synced()) {
            __ mov(Operand(ebp, fp_relative(i)), source.reg());
          }
          break;

        case FrameElement::CONSTANT:
          if (!source.is_synced()) {
            __ Set(Operand(ebp, fp_relative(i)), Immediate(source.handle()));
          }
          break;

        case FrameElement::COPY:
          if (!source.is_synced()) {
            int backing_index = source.index();
            FrameElement backing_element = elements_[backing_index];
            if (backing_element.is_memory()) {
              // If we have to spill a register, we spill esi.
              if (esi_caches != backing_index) {
                esi_caches = backing_index;
                __ mov(esi, Operand(ebp, fp_relative(backing_index)));
              }
              __ mov(Operand(ebp, fp_relative(i)), esi);
            } else {
              ASSERT(backing_element.is_register());
              __ mov(Operand(ebp, fp_relative(i)), backing_element.reg());
            }
          }
          break;
      }
      elements_[i] = memory_element;
    }
  }

  if (esi_caches != kIllegalIndex) {
    __ mov(esi, Operand(ebp, fp_relative(context_index())));
  }
}


void VirtualFrame::MergeMoveRegistersToRegisters(VirtualFrame* expected) {
  // We have already done X-to-memory moves.
  ASSERT(stack_pointer_ >= expected->stack_pointer_);

  // Perform register-to-register moves.
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
            __ mov(Operand(ebp, fp_relative(i)), source.reg());
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
  // Move memory, constants, and copies to registers.  This is the
  // final step and is done from the bottom up so that the backing
  // elements of copies are in their correct locations when we
  // encounter the copies.
  for (int i = 0; i < elements_.length(); i++) {
    FrameElement source = elements_[i];
    FrameElement target = expected->elements_[i];
    if (target.is_register() && !source.is_register()) {
      switch (source.type()) {
        case FrameElement::INVALID:  // Fall through.
        case FrameElement::REGISTER:
          UNREACHABLE();
          break;

        case FrameElement::MEMORY:
          ASSERT(i <= stack_pointer_);
          __ mov(target.reg(), Operand(ebp, fp_relative(i)));
          break;

        case FrameElement::CONSTANT:
          __ Set(target.reg(), Immediate(source.handle()));
          break;

        case FrameElement::COPY: {
          FrameElement backing = elements_[source.index()];
          ASSERT(backing.is_memory() || backing.is_register());
          if (backing.is_memory()) {
            ASSERT(source.index() <= stack_pointer_);
            __ mov(target.reg(), Operand(ebp, fp_relative(source.index())));
          } else {
            __ mov(target.reg(), backing.reg());
          }
        }
      }
      // Ensure the proper sync state.  If the source was memory no
      // code needs to be emitted.
      if (target.is_synced() && !source.is_memory()) {
        SyncElementAt(i);
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
  // remains.
  EmitPush(esi);

  // Store the function in the frame.  The frame owns the register
  // reference now (ie, it can keep it in edi or spill it later).
  Push(edi);
  SyncElementAt(elements_.length() - 1);
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
  FrameElement original = elements_[frame_index];

  // Early exit if the element is the same as the one being set.
  bool same_register = original.is_register()
                    && value->is_register()
                    && original.reg().is(value->reg());
  bool same_constant = original.is_constant()
                    && value->is_constant()
                    && original.handle().is_identical_to(value->handle());
  if (same_register || same_constant) {
    value->Unuse();
    return;
  }

  // If the original may be a copy, adjust to preserve the copy-on-write
  // semantics of copied elements.
  if (original.is_register() || original.is_memory()) {
    FrameElement ignored = AdjustCopies(frame_index);
  }

  // If the original is a register reference, deallocate it.
  if (original.is_register()) {
    Unuse(original.reg());
  }

  FrameElement new_element;
  if (value->is_register()) {
    // There are two cases depending no whether the register already
    // occurs in the frame or not.
    if (register_count(value->reg()) == 0) {
      Use(value->reg());
      elements_[frame_index] =
          FrameElement::RegisterElement(value->reg(),
                                        FrameElement::NOT_SYNCED);
    } else {
      for (int i = 0; i < elements_.length(); i++) {
        FrameElement element = elements_[i];
        if (element.is_register() && element.reg().is(value->reg())) {
          // The register backing store is lower in the frame than its
          // copy.
          if (i < frame_index) {
            elements_[frame_index] = CopyElementAt(i);
          } else {
            // There was an early bailout for the case of setting a
            // register element to itself.
            ASSERT(i != frame_index);
            element.clear_sync();
            elements_[frame_index] = element;
            elements_[i] = CopyElementAt(frame_index);
          }
          // Exit the loop once the appropriate copy is inserted.
          break;
        }
      }
    }
  } else {
    ASSERT(value->is_constant());
    elements_[frame_index] =
        FrameElement::ConstantElement(value->handle(),
                                      FrameElement::NOT_SYNCED);
  }
  value->Unuse();
}


void VirtualFrame::SaveContextRegister() {
  ASSERT(elements_[context_index()].is_memory());
  __ mov(Operand(ebp, fp_relative(context_index())), esi);
}


void VirtualFrame::RestoreContextRegister() {
  ASSERT(elements_[context_index()].is_memory());
  __ mov(esi, Operand(ebp, fp_relative(context_index())));
}


void VirtualFrame::PushReceiverSlotAddress() {
  Result temp = cgen_->allocator()->Allocate();
  ASSERT(temp.is_valid());
  __ lea(temp.reg(), ParameterAt(-1));
  Push(&temp);
}


void VirtualFrame::PushFrameSlotAt(int index) {
  FrameElement new_element = CopyElementAt(index);
  elements_.Add(new_element);
}


// Before changing an element which is copied, adjust so that the
// first copy becomes the new backing store and all the other copies
// are updated.  If the original was in memory, the new backing store
// is allocated to a register.  Return a copy of the new backing store
// or an invalid element if the original was not a copy.
FrameElement VirtualFrame::AdjustCopies(int index) {
  FrameElement original = elements_[index];
  ASSERT(original.is_memory() || original.is_register());

  // Go looking for a first copy above index.
  int i = index + 1;
  while (i < elements_.length()) {
    FrameElement elt = elements_[i];
    if (elt.is_copy() && elt.index() == index) break;
    i++;
  }

  if (i < elements_.length()) {
    // There was a first copy.  Make it the new backing element.
    Register backing_reg;
    if (original.is_memory()) {
      Result fresh = cgen_->allocator()->Allocate();
      ASSERT(fresh.is_valid());
      backing_reg = fresh.reg();
      __ mov(backing_reg, Operand(ebp, fp_relative(index)));
    } else {
      // The original was in a register.
      backing_reg = original.reg();
    }
    FrameElement new_backing_element =
        FrameElement::RegisterElement(backing_reg, FrameElement::NOT_SYNCED);
    if (elements_[i].is_synced()) {
      new_backing_element.set_sync();
    }
    Use(backing_reg);
    elements_[i] = new_backing_element;

    // Update the other copies.
    FrameElement copy = CopyElementAt(i);
    for (int j = i; j < elements_.length(); j++) {
      FrameElement elt = elements_[j];
      if (elt.is_copy() && elt.index() == index) {
        if (elt.is_synced()) {
          copy.set_sync();
        } else {
          copy.clear_sync();
        }
        elements_[j] = copy;
      }
    }

    copy.clear_sync();
    return copy;
  }

  return FrameElement::InvalidElement();
}


void VirtualFrame::TakeFrameSlotAt(int index) {
  ASSERT(index >= 0);
  ASSERT(index <= elements_.length());
  FrameElement original = elements_[index];

  switch (original.type()) {
    case FrameElement::INVALID:
      UNREACHABLE();
      break;

    case FrameElement::MEMORY: {
      // Allocate the element to a register.  If it is not copied,
      // push that register on top of the frame.  If it is copied,
      // make the first copy the backing store and push a fresh copy
      // on top of the frame.
      FrameElement copy = AdjustCopies(index);
      if (copy.is_valid()) {
        // The original element was a copy.  Push the copy of the new
        // backing store.
        elements_.Add(copy);
      } else {
        // The element was not a copy.  Move it to a register and push
        // that.
        Result fresh = cgen_->allocator()->Allocate();
        ASSERT(fresh.is_valid());
        FrameElement new_element =
            FrameElement::RegisterElement(fresh.reg(),
                                          FrameElement::NOT_SYNCED);
        Use(fresh.reg());
        elements_.Add(new_element);
        __ mov(fresh.reg(), Operand(ebp, fp_relative(index)));
      }
      break;
    }

    case FrameElement::REGISTER: {
      // If the element is not copied, push it on top of the frame.
      // If it is copied, make the first copy be the new backing store
      // and push a fresh copy on top of the frame.
      FrameElement copy = AdjustCopies(index);
      if (copy.is_valid()) {
        // The original element was a copy.  Push the copy of the new
        // backing store.
        elements_.Add(copy);
        // This is the only case where we have to unuse the original
        // register.  The original is still counted and so is the new
        // backing store of the copies.
        Unuse(original.reg());
      } else {
        // The element was not a copy.  Push it.
        original.clear_sync();
        elements_.Add(original);
      }
      break;
    }

    case FrameElement::CONSTANT:
      original.clear_sync();
      elements_.Add(original);
      break;

    case FrameElement::COPY:
      original.clear_sync();
      elements_.Add(original);
      break;
  }
  elements_[index] = FrameElement::InvalidElement();
}


void VirtualFrame::StoreToFrameSlotAt(int index) {
  // Store the value on top of the frame to the virtual frame slot at
  // a given index.  The value on top of the frame is left in place.
  // This is a duplicating operation, so it can create copies.
  ASSERT(index >= 0);
  ASSERT(index < elements_.length());

  FrameElement original = elements_[index];
  // If the stored-to slot may be copied, adjust to preserve the
  // copy-on-write semantics of copied elements.
  if (original.is_register() || original.is_memory()) {
    FrameElement ignored = AdjustCopies(index);
  }

  // If the stored-to slot is a register reference, deallocate it.
  if (original.is_register()) {
    Unuse(original.reg());
  }

  int top_index = elements_.length() - 1;
  FrameElement top = elements_[top_index];
  ASSERT(top.is_valid());

  if (top.is_copy()) {
    // There are two cases based on the relative positions of the
    // stored-to slot and the backing slot of the top element.
    int backing_index = top.index();
    ASSERT(backing_index != index);
    if (backing_index < index) {
      // 1. The top element is a copy of a slot below the stored-to
      // slot.  The stored-to slot becomes an unsynced copy of that
      // same backing slot.
      elements_[index] = CopyElementAt(backing_index);
    } else {
      // 2. The top element is a copy of a slot above the stored-to
      // slot.  The stored-to slot becomes the new (unsynced) backing
      // slot and both the top element and the element at the former
      // backing slot become copies of it.  The sync state of the top
      // and former backing elements is preserved.
      FrameElement backing_element = elements_[backing_index];
      ASSERT(backing_element.is_memory() || backing_element.is_register());
      if (backing_element.is_memory()) {
        // Because sets of copies are canonicalized to be backed by
        // their lowest frame element, and because memory frame
        // elements are backed by the corresponding stack address, we
        // have to move the actual value down in the stack.
        //
        // TODO(209): considering allocating the stored-to slot to the
        // temp register.  Alternatively, allow copies to appear in
        // any order in the frame and lazily move the value down to
        // the slot.
        Result temp = cgen_->allocator()->Allocate();
        ASSERT(temp.is_valid());
        __ mov(temp.reg(), Operand(ebp, fp_relative(backing_index)));
        __ mov(Operand(ebp, fp_relative(index)), temp.reg());
      } else if (backing_element.is_synced()) {
        // If the element is a register, we will not actually move
        // anything on the stack but only update the virtual frame
        // element.
        backing_element.clear_sync();
      }
      elements_[index] = backing_element;

      // The old backing element becomes a copy of the new backing
      // element.
      FrameElement new_element = CopyElementAt(index);
      elements_[backing_index] = new_element;
      if (backing_element.is_synced()) {
        elements_[backing_index].set_sync();
      }

      // All the copies of the old backing element (including the top
      // element) become copies of the new backing element.
      for (int i = backing_index + 1; i < elements_.length(); i++) {
        FrameElement current = elements_[i];
        if (current.is_copy() && current.index() == backing_index) {
          elements_[i] = new_element;
          if (current.is_synced()) {
            elements_[i].set_sync();
          }
        }
      }
    }

    return;
  }

  // Move the top element to the stored-to slot and replace it (the
  // top element) with a copy.
  elements_[index] = top;
  if (top.is_memory()) {
    // TODO(209): consider allocating the stored-to slot to the temp
    // register.  Alternatively, allow copies to appear in any order
    // in the frame and lazily move the value down to the slot.
    FrameElement new_top = CopyElementAt(index);
    new_top.set_sync();
    elements_[top_index] = new_top;

    // The sync state of the former top element is correct (synced).
    // Emit code to move the value down in the frame.
    Result temp = cgen_->allocator()->Allocate();
    ASSERT(temp.is_valid());
    __ mov(temp.reg(), Operand(esp, 0));
    __ mov(Operand(ebp, fp_relative(index)), temp.reg());
  } else if (top.is_register()) {
    // The stored-to slot has the (unsynced) register reference and
    // the top element becomes a copy.  The sync state of the top is
    // preserved.
    FrameElement new_top = CopyElementAt(index);
    if (top.is_synced()) {
      new_top.set_sync();
      elements_[index].clear_sync();
    }
    elements_[top_index] = new_top;
  } else {
    // The stored-to slot holds the same value as the top but
    // unsynced.  (We do not have copies of constants yet.)
    ASSERT(top.is_constant());
    elements_[index].clear_sync();
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


Result VirtualFrame::RawCallStub(CodeStub* stub, int frame_arg_count) {
  ASSERT(cgen_->HasValidEntryRegisters());
  __ CallStub(stub);
  Result result = cgen_->allocator()->Allocate(eax);
  ASSERT(result.is_valid());
  return result;
}


Result VirtualFrame::CallStub(CodeStub* stub, int frame_arg_count) {
  PrepareForCall(frame_arg_count, frame_arg_count);
  return RawCallStub(stub, frame_arg_count);
}


Result VirtualFrame::CallStub(CodeStub* stub,
                              Result* arg,
                              int frame_arg_count) {
  PrepareForCall(frame_arg_count, frame_arg_count);
  arg->Unuse();
  return RawCallStub(stub, frame_arg_count);
}


Result VirtualFrame::CallStub(CodeStub* stub,
                              Result* arg0,
                              Result* arg1,
                              int frame_arg_count) {
  PrepareForCall(frame_arg_count, frame_arg_count);
  arg0->Unuse();
  arg1->Unuse();
  return RawCallStub(stub, frame_arg_count);
}


Result VirtualFrame::CallRuntime(Runtime::Function* f,
                                 int frame_arg_count) {
  PrepareForCall(frame_arg_count, frame_arg_count);
  ASSERT(cgen_->HasValidEntryRegisters());
  __ CallRuntime(f, frame_arg_count);
  Result result = cgen_->allocator()->Allocate(eax);
  ASSERT(result.is_valid());
  return result;
}


Result VirtualFrame::CallRuntime(Runtime::FunctionId id,
                                 int frame_arg_count) {
  PrepareForCall(frame_arg_count, frame_arg_count);
  ASSERT(cgen_->HasValidEntryRegisters());
  __ CallRuntime(id, frame_arg_count);
  Result result = cgen_->allocator()->Allocate(eax);
  ASSERT(result.is_valid());
  return result;
}


Result VirtualFrame::InvokeBuiltin(Builtins::JavaScript id,
                                   InvokeFlag flag,
                                   int frame_arg_count) {
  PrepareForCall(frame_arg_count, frame_arg_count);
  ASSERT(cgen_->HasValidEntryRegisters());
  __ InvokeBuiltin(id, flag);
  Result result = cgen_->allocator()->Allocate(eax);
  ASSERT(result.is_valid());
  return result;
}


Result VirtualFrame::RawCallCodeObject(Handle<Code> code,
                                       RelocInfo::Mode rmode) {
  ASSERT(cgen_->HasValidEntryRegisters());
  __ call(code, rmode);
  Result result = cgen_->allocator()->Allocate(eax);
  ASSERT(result.is_valid());
  return result;
}


Result VirtualFrame::CallCodeObject(Handle<Code> code,
                                    RelocInfo::Mode rmode,
                                    int dropped_args) {
  int spilled_args = 0;
  switch (code->kind()) {
    case Code::CALL_IC:
      spilled_args = dropped_args + 1;
      break;
    case Code::FUNCTION:
      spilled_args = dropped_args + 1;
      break;
    case Code::KEYED_LOAD_IC:
      ASSERT(dropped_args == 0);
      spilled_args = 2;
      break;
    default:
      // The other types of code objects are called with values
      // in specific registers, and are handled in functions with
      // a different signature.
      UNREACHABLE();
      break;
  }
  PrepareForCall(spilled_args, dropped_args);
  return RawCallCodeObject(code, rmode);
}


Result VirtualFrame::CallCodeObject(Handle<Code> code,
                                    RelocInfo::Mode rmode,
                                    Result* arg,
                                    int dropped_args) {
  int spilled_args = 0;
  switch (code->kind()) {
    case Code::CALL_IC:
      ASSERT(arg->reg().is(eax));
      spilled_args = dropped_args + 1;
      break;
    case Code::LOAD_IC:
      ASSERT(arg->reg().is(ecx));
      ASSERT(dropped_args == 0);
      spilled_args = 1;
      break;
    case Code::KEYED_STORE_IC:
      ASSERT(arg->reg().is(eax));
      ASSERT(dropped_args == 0);
      spilled_args = 2;
      break;
    default:
      // No other types of code objects are called with values
      // in exactly one register.
      UNREACHABLE();
      break;
  }
  PrepareForCall(spilled_args, dropped_args);
  arg->Unuse();
  return RawCallCodeObject(code, rmode);
}


Result VirtualFrame::CallCodeObject(Handle<Code> code,
                                    RelocInfo::Mode rmode,
                                    Result* arg0,
                                    Result* arg1,
                                    int dropped_args) {
  int spilled_args = 1;
  switch (code->kind()) {
    case Code::STORE_IC:
      ASSERT(arg0->reg().is(eax));
      ASSERT(arg1->reg().is(ecx));
      ASSERT(dropped_args == 0);
      spilled_args = 1;
      break;
    case Code::BUILTIN:
      ASSERT(*code == Builtins::builtin(Builtins::JSConstructCall));
      ASSERT(arg0->reg().is(eax));
      ASSERT(arg1->reg().is(edi));
      spilled_args = dropped_args + 1;
      break;
    default:
      // No other types of code objects are called with values
      // in exactly two registers.
      UNREACHABLE();
      break;
  }
  PrepareForCall(spilled_args, dropped_args);
  arg0->Unuse();
  arg1->Unuse();
  return RawCallCodeObject(code, rmode);
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
  FrameElement element = elements_.RemoveLast();
  int index = elements_.length();
  ASSERT(element.is_valid());

  bool pop_needed = (stack_pointer_ == index);
  if (pop_needed) {
    stack_pointer_--;
    if (element.is_memory()) {
      Result temp = cgen_->allocator()->Allocate();
      ASSERT(temp.is_valid());
      __ pop(temp.reg());
      return temp;
    }

    __ add(Operand(esp), Immediate(kPointerSize));
  }
  ASSERT(!element.is_memory());

  // The top element is a register, constant, or a copy.  Unuse
  // registers and follow copies to their backing store.
  if (element.is_register()) {
    Unuse(element.reg());
  } else if (element.is_copy()) {
    ASSERT(element.index() < index);
    index = element.index();
    element = elements_[index];
  }
  ASSERT(!element.is_copy());

  // The element is memory, a register, or a constant.
  if (element.is_memory()) {
    // Memory elements could only be the backing store of a copy.
    // Allocate the original to a register.
    ASSERT(index <= stack_pointer_);
    Result temp = cgen_->allocator()->Allocate();
    ASSERT(temp.is_valid());
    Use(temp.reg());
    FrameElement new_element =
        FrameElement::RegisterElement(temp.reg(), FrameElement::SYNCED);
    elements_[index] = new_element;
    __ mov(temp.reg(), Operand(ebp, fp_relative(index)));
    return Result(temp.reg(), cgen_);
  } else if (element.is_register()) {
    return Result(element.reg(), cgen_);
  } else {
    ASSERT(element.is_constant());
    return Result(element.handle(), cgen_);
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
  FrameElement new_element;
  if (register_count(reg) == 0) {
    Use(reg);
    new_element =
        FrameElement::RegisterElement(reg, FrameElement::NOT_SYNCED);
  } else {
    for (int i = 0; i < elements_.length(); i++) {
      FrameElement element = elements_[i];
      if (element.is_register() && element.reg().is(reg)) {
        new_element = CopyElementAt(i);
        break;
      }
    }
  }
  elements_.Add(new_element);
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
bool FrameElement::Equals(FrameElement other) {
  if (type() != other.type()) return false;
  if (is_synced() != other.is_synced()) return false;

  if (is_register()) {
    if (!reg().is(other.reg())) return false;
  } else if (is_constant()) {
    if (!handle().is_identical_to(other.handle())) return false;
  } else if (is_copy()) {
    if (index() != other.index()) return false;
  }

  return true;
}


bool VirtualFrame::Equals(VirtualFrame* other) {
  if (cgen_ != other->cgen_) return false;
  if (masm_ != other->masm_) return false;
  if (elements_.length() != other->elements_.length()) return false;

  for (int i = 0; i < elements_.length(); i++) {
    if (!elements_[i].Equals(other->elements_[i])) return false;
  }

  if (parameter_count_ != other->parameter_count_) return false;
  if (local_count_ != other->local_count_) return false;
  if (stack_pointer_ != other->stack_pointer_) return false;
  if (frame_pointer_ != other->frame_pointer_) return false;

  for (int i = 0; i < kNumRegisters; i++) {
    if (frame_registers_.count(i) != other->frame_registers_.count(i)) {
      return false;
    }
  }

  return true;
}
#endif

#undef __

} }  // namespace v8::internal
