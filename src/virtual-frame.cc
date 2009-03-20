// Copyright 2009 the V8 project authors. All rights reserved.
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

#include "codegen-inl.h"
#include "register-allocator-inl.h"

namespace v8 { namespace internal {

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
          FrameElement::TypeField::encode(FrameElement::COPY)
          | FrameElement::IsCopiedField::encode(false)
          | FrameElement::SyncField::encode(FrameElement::NOT_SYNCED);
      result.data_.index_ = index;
      elements_[index].set_copied();
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

  stack_pointer_ -= count;
  ForgetElements(count);
}


void VirtualFrame::ForgetElements(int count) {
  ASSERT(count >= 0);
  ASSERT(elements_.length() >= count);

  for (int i = 0; i < count; i++) {
    FrameElement last = elements_.RemoveLast();
    if (last.is_register()) {
      // A hack to properly count register references for the code
      // generator's current frame and also for other frames.  The
      // same code appears in PrepareMergeTo.
      if (cgen_->frame() == this) {
        Unuse(last.reg());
      } else {
        frame_registers_.Unuse(last.reg());
      }
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

  SyncElementAt(index);
  // The element is now in memory.  Its copied flag is preserved.
  FrameElement new_element = FrameElement::MemoryElement();
  if (elements_[index].is_copied()) {
    new_element.set_copied();
  }
  if (elements_[index].is_register()) {
    Unuse(elements_[index].reg());
  }
  elements_[index] = new_element;
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


void VirtualFrame::PrepareMergeTo(VirtualFrame* expected) {
  // Perform state changes on this frame that will make merge to the
  // expected frame simpler or else increase the likelihood that his
  // frame will match another.
  for (int i = 0; i < elements_.length(); i++) {
    FrameElement source = elements_[i];
    FrameElement target = expected->elements_[i];

    if (!target.is_valid() ||
        (target.is_memory() && !source.is_memory() && source.is_synced())) {
      // No code needs to be generated to invalidate valid elements.
      // No code needs to be generated to move values to memory if
      // they are already synced.  We perform those moves here, before
      // merging.
      if (source.is_register()) {
        // If the frame is the code generator's current frame, we have
        // to decrement both the frame-internal and global register
        // counts.
        if (cgen_->frame() == this) {
          Unuse(source.reg());
        } else {
          frame_registers_.Unuse(source.reg());
        }
      }
      elements_[i] = target;
    } else if (target.is_register() && !target.is_synced() &&
               !source.is_memory()) {
      // If an element's target is a register that doesn't need to be
      // synced, and the element is not in memory, then the sync state
      // of the element is irrelevant.  We clear the sync bit.
      ASSERT(source.is_valid());
      elements_[i].clear_sync();
    }

    elements_[i].clear_copied();
    if (elements_[i].is_copy()) {
      elements_[elements_[i].index()].set_copied();
    }
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


void VirtualFrame::PrepareForReturn() {
  // Spill all locals. This is necessary to make sure all locals have
  // the right value when breaking at the return site in the debugger.
  //
  // TODO(203): It is also necessary to ensure that merging at the
  // return site does not generate code to overwrite eax, where the
  // return value is kept in a non-refcounted register reference.
  for (int i = 0; i < expression_base_index(); i++) SpillElementAt(i);
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
  if (original.is_copied() &&
      (original.is_register() || original.is_memory())) {
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


void VirtualFrame::PushFrameSlotAt(int index) {
  FrameElement new_element = CopyElementAt(index);
  elements_.Add(new_element);
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


bool FrameElement::Equals(FrameElement other) {
  if (type_ != other.type_) return false;

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
#ifdef DEBUG
  // These are sanity checks in debug builds, but we do not need to
  // use them to distinguish frames at merge points.
  if (cgen_ != other->cgen_) return false;
  if (masm_ != other->masm_) return false;
  if (parameter_count_ != other->parameter_count_) return false;
  if (local_count_ != other->local_count_) return false;
  if (frame_pointer_ != other->frame_pointer_) return false;

  for (int i = 0; i < kNumRegisters; i++) {
    if (frame_registers_.count(i) != other->frame_registers_.count(i)) {
      return false;
    }
  }
  if (elements_.length() != other->elements_.length()) return false;
#endif
  if (stack_pointer_ != other->stack_pointer_) return false;
  for (int i = 0; i < elements_.length(); i++) {
    if (!elements_[i].Equals(other->elements_[i])) return false;
  }

  return true;
}

} }  // namespace v8::internal
