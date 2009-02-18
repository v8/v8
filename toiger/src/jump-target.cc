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

#include "codegen.h"
#include "jump-target.h"

namespace v8 { namespace internal {

// -------------------------------------------------------------------------
// JumpTarget implementation.

JumpTarget::JumpTarget(CodeGenerator* cgen, Directionality direction)
    : cgen_(cgen),
      direction_(direction),
      reaching_frames_(0),
      merge_labels_(0),
      entry_frame_(NULL),
      is_bound_(false),
      is_linked_(false) {
  ASSERT(cgen_ != NULL);
  masm_ = cgen_->masm();
}


JumpTarget::JumpTarget()
    : cgen_(NULL),
      masm_(NULL),
      direction_(FORWARD_ONLY),
      reaching_frames_(0),
      merge_labels_(0),
      entry_frame_(NULL),
      is_bound_(false),
      is_linked_(false) {
}


void JumpTarget::Initialize(CodeGenerator* cgen, Directionality direction) {
  ASSERT(cgen != NULL);
  ASSERT(cgen_ == NULL);
  cgen_ = cgen;
  masm_ = cgen->masm();
  direction_ = direction;
}


void JumpTarget::Unuse() {
  ASSERT(!is_linked());
  entry_label_.Unuse();
  delete entry_frame_;
  entry_frame_ = NULL;
  is_bound_ = false;
  is_linked_ = false;
}


void JumpTarget::Reset() {
  reaching_frames_.Clear();
  merge_labels_.Clear();
  entry_frame_ = NULL;
  entry_label_.Unuse();
  is_bound_ = false;
  is_linked_ = false;
}


FrameElement* JumpTarget::Combine(FrameElement* left, FrameElement* right) {
  // Given a pair of non-null frame element pointers, return one of
  // them as an entry frame candidate or null if they are
  // incompatible.

  // If either is invalid, the result is.
  if (!left->is_valid()) return left;
  if (!right->is_valid()) return right;

  // If they have the same value, the result is the same.  (Exception:
  // bidirectional frames cannot have constants or copies.)  If either
  // is unsynced, the result is.
  if (left->is_memory() && right->is_memory()) return left;

  if (left->is_register() && right->is_register() &&
      left->reg().is(right->reg())) {
    if (!left->is_synced()) {
      return left;
    } else {
      return right;
    }
  }

  if (direction_ == FORWARD_ONLY &&
      left->is_constant() &&
      right->is_constant() &&
      left->handle().is_identical_to(right->handle())) {
    if (!left->is_synced()) {
      return left;
    } else {
      return right;
    }
  }

  if (direction_ == FORWARD_ONLY &&
      left->is_copy() &&
      right->is_copy() &&
      left->index() == right->index()) {
    if (!left->is_synced()) {
      return left;
    } else {
      return right;
    }
  }

  // Otherwise they are incompatible and we will reallocate them.
  return NULL;
}


void JumpTarget::ComputeEntryFrame(int mergable_elements) {
  // Given: a collection of frames reaching by forward CFG edges
  // (including the code generator's current frame) and the
  // directionality of the block.  Compute: an entry frame for the
  // block.

  // Choose an initial frame, either the code generator's current
  // frame if there is one, or the first reaching frame if not.
  VirtualFrame* initial_frame = cgen_->frame();
  int start_index = 0;  // Begin iteration with the 1st reaching frame.
  if (initial_frame == NULL) {
    initial_frame = reaching_frames_[0];
    start_index = 1;  // Begin iteration with the 2nd reaching frame.
  }

  // A list of pointers to frame elements in the entry frame.  NULL
  // indicates that the element has not yet been determined.
  int length = initial_frame->elements_.length();
  List<FrameElement*> elements(length);

  // Convert the number of mergable elements (counted from the top
  // down) to a frame high-water mark (counted from the bottom up).
  // Elements strictly above the high-water index will be mergable in
  // entry frames for bidirectional jump targets.
  int high_water_mark = (mergable_elements == kAllElements)
      ? VirtualFrame::kIllegalIndex  // All frame indices are above this.
      : length - mergable_elements - 1;  // Top index if m_e == 0.

  // Initially populate the list of elements based on the initial
  // frame.
  for (int i = 0; i < length; i++) {
    FrameElement element = initial_frame->elements_[i];
    // We do not allow copies or constants in bidirectional frames.
    if (direction_ == BIDIRECTIONAL &&
        i > high_water_mark &&
        (element.is_constant() || element.is_copy())) {
      elements.Add(NULL);
    } else {
      elements.Add(&initial_frame->elements_[i]);
    }
  }

  // Compute elements based on the other reaching frames.
  if (start_index < reaching_frames_.length()) {
    for (int i = 0; i < length; i++) {
      for (int j = start_index; j < reaching_frames_.length(); j++) {
        FrameElement* element = elements[i];

        // Element computation is monotonic: new information will not
        // change our decision about undetermined or invalid elements.
        if (element == NULL || !element->is_valid()) break;

        elements[i] = Combine(element, &reaching_frames_[j]->elements_[i]);
      }
    }
  }

  // Compute the registers already reserved by values in the frame.
  // Count the reserved registers to avoid using them.
  RegisterFile frame_registers = RegisterAllocator::Reserved();
  for (int i = 0; i < length; i++) {
    FrameElement* element = elements[i];
    if (element != NULL && element->is_register()) {
      frame_registers.Use(element->reg());
    }
  }

  // Build the new frame.  The frame already has memory elements for
  // the parameters (including the receiver) and the return address.
  // We will fill it up with memory elements.
  entry_frame_ = new VirtualFrame(cgen_);
  while (entry_frame_->elements_.length() < length) {
    entry_frame_->elements_.Add(FrameElement::MemoryElement());
  }


  // Copy the already-determined frame elements to the entry frame,
  // and allocate any still-undetermined frame elements to registers
  // or memory, from the top down.
  for (int i = length - 1; i >= 0; i--) {
    if (elements[i] == NULL) {
      // If the value is synced on all frames, put it in memory.  This
      // costs nothing at the merge code but will incur a
      // memory-to-register move when the value is needed later.
      bool is_synced = initial_frame->elements_[i].is_synced();
      int j = start_index;
      while (is_synced && j < reaching_frames_.length()) {
        is_synced = reaching_frames_[j]->elements_[i].is_synced();
        j++;
      }
      // There is nothing to be done if the elements are all synced.
      // It is already recorded as a memory element.
      if (is_synced) continue;

      // Choose an available register.  Prefer ones that the element
      // is already occupying on some reaching frame.
      RegisterFile candidate_registers;
      int max_count = kMinInt;
      int best_reg_code = no_reg.code_;

      // Consider the initial frame.
      FrameElement element = initial_frame->elements_[i];
      if (element.is_register() &&
          !frame_registers.is_used(element.reg())) {
        candidate_registers.Use(element.reg());
        max_count = 1;
        best_reg_code = element.reg().code();
      }
      // Consider the other frames.
      for (int j = start_index; j < reaching_frames_.length(); j++) {
        element = reaching_frames_[j]->elements_[i];
        if (element.is_register() &&
            !frame_registers.is_used(element.reg())) {
          candidate_registers.Use(element.reg());
          if (candidate_registers.count(element.reg()) > max_count) {
            max_count = candidate_registers.count(element.reg());
            best_reg_code = element.reg().code();
          }
        }
      }
      // If there was no preferred choice consider any free register.
      if (best_reg_code == no_reg.code_) {
        for (int j = 0; j < kNumRegisters; j++) {
          if (!frame_registers.is_used(j)) {
            best_reg_code = j;
            break;
          }
        }
      }

      // If there was a register choice, use it.  If not do nothing
      // (the element is already recorded as in memory)
      if (best_reg_code != no_reg.code_) {
        Register reg = { best_reg_code };
        frame_registers.Use(reg);
        entry_frame_->elements_[i] =
            FrameElement::RegisterElement(reg,
                                          FrameElement::NOT_SYNCED);
      }
    } else {
      // The element is already determined.
      entry_frame_->elements_[i] = *elements[i];
    }
  }

  // Fill in the other fields of the entry frame.
  entry_frame_->local_count_ = initial_frame->local_count_;
  entry_frame_->frame_pointer_ = initial_frame->frame_pointer_;

  // The stack pointer is at the highest synced element or the base of
  // the expression stack.
  int stack_pointer = length - 1;
  while (stack_pointer >= entry_frame_->expression_base_index() &&
         !entry_frame_->elements_[stack_pointer].is_synced()) {
    stack_pointer--;
  }
  entry_frame_->stack_pointer_ = stack_pointer;

  // Unuse the reserved registers---they do not actually appear in
  // the entry frame.
  RegisterAllocator::UnuseReserved(&frame_registers);
  entry_frame_->frame_registers_ = frame_registers;
}


void JumpTarget::Jump(Result* arg) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  cgen_->frame()->Push(arg);
  Jump();
}


void JumpTarget::Jump(Result* arg0, Result* arg1) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  cgen_->frame()->Push(arg0);
  cgen_->frame()->Push(arg1);
  Jump();
}


void JumpTarget::Jump(Result* arg0, Result* arg1, Result* arg2) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  cgen_->frame()->Push(arg0);
  cgen_->frame()->Push(arg1);
  cgen_->frame()->Push(arg2);
  Jump();
}


#ifdef DEBUG
#define DECLARE_ARGCHECK_VARS(name)                                \
  Result::Type name##_type = name->type();                         \
  Register name##_reg = name->is_register() ? name->reg() : no_reg

#define ASSERT_ARGCHECK(name)                                \
  ASSERT(name->type() == name##_type);                       \
  ASSERT(!name->is_register() || name->reg().is(name##_reg))

#else
#define DECLARE_ARGCHECK_VARS(name) do {} while (false)

#define ASSERT_ARGCHECK(name) do {} while (false)
#endif

void JumpTarget::Branch(Condition cc, Result* arg, Hint hint) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  // We want to check that non-frame registers at the call site stay in
  // the same registers on the fall-through branch.
  DECLARE_ARGCHECK_VARS(arg);

  cgen_->frame()->Push(arg);
  Branch(cc, hint);
  *arg = cgen_->frame()->Pop();

  ASSERT_ARGCHECK(arg);
}


void JumpTarget::Branch(Condition cc, Result* arg0, Result* arg1, Hint hint) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->frame() != NULL);

  // We want to check that non-frame registers at the call site stay in
  // the same registers on the fall-through branch.
  DECLARE_ARGCHECK_VARS(arg0);
  DECLARE_ARGCHECK_VARS(arg1);

  cgen_->frame()->Push(arg0);
  cgen_->frame()->Push(arg1);
  Branch(cc, hint);
  *arg1 = cgen_->frame()->Pop();
  *arg0 = cgen_->frame()->Pop();

  ASSERT_ARGCHECK(arg0);
  ASSERT_ARGCHECK(arg1);
}


void JumpTarget::Branch(Condition cc,
                        Result* arg0,
                        Result* arg1,
                        Result* arg2,
                        Hint hint) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->frame() != NULL);

  // We want to check that non-frame registers at the call site stay in
  // the same registers on the fall-through branch.
  DECLARE_ARGCHECK_VARS(arg0);
  DECLARE_ARGCHECK_VARS(arg1);
  DECLARE_ARGCHECK_VARS(arg2);

  cgen_->frame()->Push(arg0);
  cgen_->frame()->Push(arg1);
  cgen_->frame()->Push(arg2);
  Branch(cc, hint);
  *arg2 = cgen_->frame()->Pop();
  *arg1 = cgen_->frame()->Pop();
  *arg0 = cgen_->frame()->Pop();

  ASSERT_ARGCHECK(arg0);
  ASSERT_ARGCHECK(arg1);
  ASSERT_ARGCHECK(arg2);
}


void JumpTarget::Branch(Condition cc,
                        Result* arg0,
                        Result* arg1,
                        Result* arg2,
                        Result* arg3,
                        Hint hint) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->frame() != NULL);

  // We want to check that non-frame registers at the call site stay in
  // the same registers on the fall-through branch.
  DECLARE_ARGCHECK_VARS(arg0);
  DECLARE_ARGCHECK_VARS(arg1);
  DECLARE_ARGCHECK_VARS(arg2);
  DECLARE_ARGCHECK_VARS(arg3);

  cgen_->frame()->Push(arg0);
  cgen_->frame()->Push(arg1);
  cgen_->frame()->Push(arg2);
  cgen_->frame()->Push(arg3);
  Branch(cc, hint);
  *arg3 = cgen_->frame()->Pop();
  *arg2 = cgen_->frame()->Pop();
  *arg1 = cgen_->frame()->Pop();
  *arg0 = cgen_->frame()->Pop();

  ASSERT_ARGCHECK(arg0);
  ASSERT_ARGCHECK(arg1);
  ASSERT_ARGCHECK(arg2);
  ASSERT_ARGCHECK(arg3);
}

#undef DECLARE_ARGCHECK_VARS
#undef ASSERT_ARGCHECK


void JumpTarget::Bind(Result* arg, int mergable_elements) {
  ASSERT(cgen_ != NULL);

  if (cgen_->has_valid_frame()) {
    cgen_->frame()->Push(arg);
  }
  Bind(mergable_elements);
  *arg = cgen_->frame()->Pop();
}


void JumpTarget::Bind(Result* arg0, Result* arg1, int mergable_elements) {
  ASSERT(cgen_ != NULL);

  if (cgen_->has_valid_frame()) {
    cgen_->frame()->Push(arg0);
    cgen_->frame()->Push(arg1);
  }
  Bind(mergable_elements);
  *arg1 = cgen_->frame()->Pop();
  *arg0 = cgen_->frame()->Pop();
}


void JumpTarget::Bind(Result* arg0,
                      Result* arg1,
                      Result* arg2,
                      int mergable_elements) {
  ASSERT(cgen_ != NULL);

  if (cgen_->has_valid_frame()) {
    cgen_->frame()->Push(arg0);
    cgen_->frame()->Push(arg1);
    cgen_->frame()->Push(arg2);
  }
  Bind(mergable_elements);
  *arg2 = cgen_->frame()->Pop();
  *arg1 = cgen_->frame()->Pop();
  *arg0 = cgen_->frame()->Pop();
}


void JumpTarget::Bind(Result* arg0,
                      Result* arg1,
                      Result* arg2,
                      Result* arg3,
                      int mergable_elements) {
  ASSERT(cgen_ != NULL);

  if (cgen_->has_valid_frame()) {
    cgen_->frame()->Push(arg0);
    cgen_->frame()->Push(arg1);
    cgen_->frame()->Push(arg2);
    cgen_->frame()->Push(arg3);
  }
  Bind(mergable_elements);
  *arg3 = cgen_->frame()->Pop();
  *arg2 = cgen_->frame()->Pop();
  *arg1 = cgen_->frame()->Pop();
  *arg0 = cgen_->frame()->Pop();
}


void JumpTarget::CopyTo(JumpTarget* destination) {
  ASSERT(destination != NULL);
  destination->cgen_ = cgen_;
  destination->masm_ = masm_;
  destination->direction_ = direction_;
  destination->reaching_frames_.Clear();
  destination->merge_labels_.Clear();
  ASSERT(reaching_frames_.length() == merge_labels_.length());
  for (int i = 0; i < reaching_frames_.length(); i++) {
    destination->reaching_frames_.Add(reaching_frames_[i]);
    destination->merge_labels_.Add(merge_labels_[i]);
  }
  destination->entry_frame_ = entry_frame_;
  destination->entry_label_ = entry_label_;
  destination->is_bound_ = is_bound_;
  destination->is_linked_ = is_linked_;
}


void JumpTarget::AddReachingFrame(VirtualFrame* frame) {
  ASSERT(reaching_frames_.length() == merge_labels_.length());
  Label fresh;
  merge_labels_.Add(fresh);
  reaching_frames_.Add(frame);
}


// -------------------------------------------------------------------------
// ShadowTarget implementation.

ShadowTarget::ShadowTarget(JumpTarget* shadowed) {
  ASSERT(shadowed != NULL);
  other_target_ = shadowed;

#ifdef DEBUG
  is_shadowing_ = true;
#endif
  // While shadowing this shadow target saves the state of the original.
  shadowed->CopyTo(this);

  // Setting the code generator to null prevents the shadow target from
  // being used until shadowing stops.
  cgen_ = NULL;
  masm_ = NULL;

  // The original's state is reset.  We do not Unuse it because that
  // would delete the expected frame and assert that the target is not
  // linked.
  shadowed->Reset();
}


void ShadowTarget::StopShadowing() {
  ASSERT(is_shadowing_);

  // This target does not have a valid code generator yet.
  cgen_ = other_target_->code_generator();
  ASSERT(cgen_ != NULL);
  masm_ = cgen_->masm();

  // The states of this target, which was shadowed, and the original
  // target, which was shadowing, are swapped.
  JumpTarget temp;
  other_target_->CopyTo(&temp);
  CopyTo(other_target_);
  temp.CopyTo(this);
  temp.Reset();  // So the destructor does not deallocate virtual frames.

#ifdef DEBUG
  is_shadowing_ = false;
#endif
}


} }  // namespace v8::internal
