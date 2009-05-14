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
// JumpTarget implementation.

bool JumpTarget::compiling_deferred_code_ = false;


JumpTarget::JumpTarget(CodeGenerator* cgen, Directionality direction)
    : cgen_(cgen),
      direction_(direction),
      reaching_frames_(0),
      merge_labels_(0),
      entry_frame_(NULL),
      is_bound_(false),
      is_linked_(false) {
  ASSERT(cgen != NULL);
  masm_ = cgen->masm();
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
  // We should not deallocate jump targets that have unresolved jumps
  // to them.  In the event of a compile-time stack overflow or an
  // uninitialized jump target, we don't care.
  ASSERT(!is_linked() || cgen_ == NULL || cgen_->HasStackOverflow());
  for (int i = 0; i < reaching_frames_.length(); i++) {
    delete reaching_frames_[i];
  }
  delete entry_frame_;
  Reset();
}


void JumpTarget::Reset() {
  reaching_frames_.Clear();
  merge_labels_.Clear();
  entry_frame_ = NULL;
  entry_label_.Unuse();
  is_bound_ = false;
  is_linked_ = false;
}


void JumpTarget::ComputeEntryFrame(int mergable_elements) {
  // Given: a collection of frames reaching by forward CFG edges and
  // the directionality of the block.  Compute: an entry frame for the
  // block.

  Counters::compute_entry_frame.Increment();
#ifdef DEBUG
  if (compiling_deferred_code_) {
    ASSERT(reaching_frames_.length() > 1);
    VirtualFrame* frame = reaching_frames_[0];
    bool all_identical = true;
    for (int i = 1; i < reaching_frames_.length(); i++) {
      if (!frame->Equals(reaching_frames_[i])) {
        all_identical = false;
        break;
      }
    }
    ASSERT(!all_identical || all_identical);
  }
#endif

  // Choose an initial frame.
  VirtualFrame* initial_frame = reaching_frames_[0];

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
    // We do not allow copies or constants in bidirectional frames.  All
    // elements above the water mark on bidirectional frames have
    // unknown static types.
    if (direction_ == BIDIRECTIONAL && i > high_water_mark) {
      if (element.is_constant() || element.is_copy()) {
        elements.Add(NULL);
        continue;
      }
      // It's safe to change the static type on the initial frame
      // element, see comment in JumpTarget::Combine.
      initial_frame->elements_[i].set_static_type(StaticType::unknown());
    }
    elements.Add(&initial_frame->elements_[i]);
  }

  // Compute elements based on the other reaching frames.
  if (reaching_frames_.length() > 1) {
    for (int i = 0; i < length; i++) {
      FrameElement* element = elements[i];
      for (int j = 1; j < reaching_frames_.length(); j++) {
        // Element computation is monotonic: new information will not
        // change our decision about undetermined or invalid elements.
        if (element == NULL || !element->is_valid()) break;

        element = element->Combine(&reaching_frames_[j]->elements_[i]);
      }
      elements[i] = element;
    }
  }

  // Build the new frame.  A freshly allocated frame has memory elements
  // for the parameters and some platform-dependent elements (e.g.,
  // return address).  Replace those first.
  entry_frame_ = new VirtualFrame(cgen_);
  int index = 0;
  for (; index < entry_frame_->elements_.length(); index++) {
    FrameElement* target = elements[index];
    // If the element is determined, set it now.  Count registers.  Mark
    // elements as copied exactly when they have a copy.  Undetermined
    // elements are initially recorded as if in memory.
    if (target != NULL) {
      entry_frame_->elements_[index] = *target;
      entry_frame_->InitializeEntryElement(index, target);
    }
  }
  // Then fill in the rest of the frame with new elements.
  for (; index < length; index++) {
    FrameElement* target = elements[index];
    if (target == NULL) {
      entry_frame_->elements_.Add(FrameElement::MemoryElement());
    } else {
      entry_frame_->elements_.Add(*target);
      entry_frame_->InitializeEntryElement(index, target);
    }
  }

  // Allocate any still-undetermined frame elements to registers or
  // memory, from the top down.
  for (int i = length - 1; i >= 0; i--) {
    if (elements[i] == NULL) {
      // Loop over all the reaching frames to check whether the element
      // is synced on all frames, to count the registers it occupies,
      // and to compute a merged static type.
      bool is_synced = true;
      RegisterFile candidate_registers;
      int best_count = kMinInt;
      int best_reg_code = no_reg.code_;

      StaticType type;  // Initially invalid.
      if (direction_ != BIDIRECTIONAL || i < high_water_mark) {
        type = reaching_frames_[0]->elements_[i].static_type();
      }

      for (int j = 0; j < reaching_frames_.length(); j++) {
        FrameElement element = reaching_frames_[j]->elements_[i];
        is_synced = is_synced && element.is_synced();
        if (element.is_register() && !entry_frame_->is_used(element.reg())) {
          // Count the register occurrence and remember it if better
          // than the previous best.
          candidate_registers.Use(element.reg());
          if (candidate_registers.count(element.reg()) > best_count) {
            best_count = candidate_registers.count(element.reg());
            best_reg_code = element.reg().code();
          }
        }
        type = type.merge(element.static_type());
      }

      // If the value is synced on all frames, put it in memory.  This
      // costs nothing at the merge code but will incur a
      // memory-to-register move when the value is needed later.
      if (is_synced) {
        // Already recorded as a memory element.
        entry_frame_->elements_[i].set_static_type(type);
        continue;
      }

      // Try to put it in a register.  If there was no best choice
      // consider any free register.
      if (best_reg_code == no_reg.code_) {
        for (int j = 0; j < kNumRegisters; j++) {
          if (!entry_frame_->is_used(j) && !RegisterAllocator::IsReserved(j)) {
            best_reg_code = j;
            break;
          }
        }
      }

      if (best_reg_code == no_reg.code_) {
        // If there was no register found, the element is already
        // recorded as in memory.
        entry_frame_->elements_[i].set_static_type(type);
      } else {
        // If there was a register choice, use it.  Preserve the copied
        // flag on the element.  Set the static type as computed.
        bool is_copied = entry_frame_->elements_[i].is_copied();
        Register reg = { best_reg_code };
        entry_frame_->elements_[i] =
            FrameElement::RegisterElement(reg,
                                          FrameElement::NOT_SYNCED);
        if (is_copied) entry_frame_->elements_[i].set_copied();
        entry_frame_->elements_[i].set_static_type(type);
        entry_frame_->register_locations_[best_reg_code] = i;
      }
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
}


void JumpTarget::Jump() {
  DoJump();
}


void JumpTarget::Jump(Result* arg) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  cgen_->frame()->Push(arg);
  DoJump();
}


void JumpTarget::Jump(Result* arg0, Result* arg1) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  cgen_->frame()->Push(arg0);
  cgen_->frame()->Push(arg1);
  DoJump();
}


void JumpTarget::Jump(Result* arg0, Result* arg1, Result* arg2) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  cgen_->frame()->Push(arg0);
  cgen_->frame()->Push(arg1);
  cgen_->frame()->Push(arg2);
  DoJump();
}


void JumpTarget::Branch(Condition cc, Hint hint) {
  DoBranch(cc, hint);
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
  DoBranch(cc, hint);
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
  DoBranch(cc, hint);
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
  DoBranch(cc, hint);
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
  DoBranch(cc, hint);
  *arg3 = cgen_->frame()->Pop();
  *arg2 = cgen_->frame()->Pop();
  *arg1 = cgen_->frame()->Pop();
  *arg0 = cgen_->frame()->Pop();

  ASSERT_ARGCHECK(arg0);
  ASSERT_ARGCHECK(arg1);
  ASSERT_ARGCHECK(arg2);
  ASSERT_ARGCHECK(arg3);
}


void BreakTarget::Branch(Condition cc, Result* arg, Hint hint) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  int count = cgen_->frame()->height() - expected_height_;
  if (count > 0) {
    // We negate and branch here rather than using DoBranch's negate
    // and branch.  This gives us a hook to remove statement state
    // from the frame.
    JumpTarget fall_through(cgen_);
    // Branch to fall through will not negate, because it is a
    // forward-only target.
    fall_through.Branch(NegateCondition(cc), NegateHint(hint));
    Jump(arg);  // May emit merge code here.
    fall_through.Bind();
  } else {
    DECLARE_ARGCHECK_VARS(arg);
    cgen_->frame()->Push(arg);
    DoBranch(cc, hint);
    *arg = cgen_->frame()->Pop();
    ASSERT_ARGCHECK(arg);
  }
}

#undef DECLARE_ARGCHECK_VARS
#undef ASSERT_ARGCHECK


void JumpTarget::Bind(int mergable_elements) {
  DoBind(mergable_elements);
}


void JumpTarget::Bind(Result* arg, int mergable_elements) {
  ASSERT(cgen_ != NULL);

  if (cgen_->has_valid_frame()) {
    cgen_->frame()->Push(arg);
  }
  DoBind(mergable_elements);
  *arg = cgen_->frame()->Pop();
}


void JumpTarget::Bind(Result* arg0, Result* arg1, int mergable_elements) {
  ASSERT(cgen_ != NULL);

  if (cgen_->has_valid_frame()) {
    cgen_->frame()->Push(arg0);
    cgen_->frame()->Push(arg1);
  }
  DoBind(mergable_elements);
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
  DoBind(mergable_elements);
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
  DoBind(mergable_elements);
  *arg3 = cgen_->frame()->Pop();
  *arg2 = cgen_->frame()->Pop();
  *arg1 = cgen_->frame()->Pop();
  *arg0 = cgen_->frame()->Pop();
}


void JumpTarget::AddReachingFrame(VirtualFrame* frame) {
  ASSERT(reaching_frames_.length() == merge_labels_.length());
  ASSERT(entry_frame_ == NULL);
  Label fresh;
  merge_labels_.Add(fresh);
  reaching_frames_.Add(frame);
}


// -------------------------------------------------------------------------
// BreakTarget implementation.

void BreakTarget::Initialize(CodeGenerator* cgen, Directionality direction) {
  JumpTarget::Initialize(cgen, direction);
  ASSERT(cgen_->has_valid_frame());
  expected_height_ = cgen_->frame()->height();
}


void BreakTarget::CopyTo(BreakTarget* destination) {
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
  destination->expected_height_ = expected_height_;
}


void BreakTarget::Jump() {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  // Drop leftover statement state from the frame before merging.
  cgen_->frame()->ForgetElements(cgen_->frame()->height() - expected_height_);
  DoJump();
}


void BreakTarget::Jump(Result* arg) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  // Drop leftover statement state from the frame before merging.
  cgen_->frame()->ForgetElements(cgen_->frame()->height() - expected_height_);
  cgen_->frame()->Push(arg);
  DoJump();
}


void BreakTarget::Branch(Condition cc, Hint hint) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  int count = cgen_->frame()->height() - expected_height_;
  if (count > 0) {
    // We negate and branch here rather than using DoBranch's negate
    // and branch.  This gives us a hook to remove statement state
    // from the frame.
    JumpTarget fall_through(cgen_);
    // Branch to fall through will not negate, because it is a
    // forward-only target.
    fall_through.Branch(NegateCondition(cc), NegateHint(hint));
    Jump();  // May emit merge code here.
    fall_through.Bind();
  } else {
    DoBranch(cc, hint);
  }
}


void BreakTarget::Bind(int mergable_elements) {
#ifdef DEBUG
  ASSERT(cgen_ != NULL);
  // All the forward-reaching frames should have been adjusted at the
  // jumps to this target.
  for (int i = 0; i < reaching_frames_.length(); i++) {
    ASSERT(reaching_frames_[i] == NULL ||
           reaching_frames_[i]->height() == expected_height_);
  }
#endif
  // Drop leftover statement state from the frame before merging, even
  // on the fall through.  This is so we can bind the return target
  // with state on the frame.
  if (cgen_->has_valid_frame()) {
    cgen_->frame()->ForgetElements(cgen_->frame()->height() - expected_height_);
  }
  DoBind(mergable_elements);
}


void BreakTarget::Bind(Result* arg, int mergable_elements) {
#ifdef DEBUG
  ASSERT(cgen_ != NULL);
  // All the forward-reaching frames should have been adjusted at the
  // jumps to this target.
  for (int i = 0; i < reaching_frames_.length(); i++) {
    ASSERT(reaching_frames_[i] == NULL ||
           reaching_frames_[i]->height() == expected_height_ + 1);
  }
#endif
  // Drop leftover statement state from the frame before merging, even
  // on the fall through.  This is so we can bind the return target
  // with state on the frame.
  if (cgen_->has_valid_frame()) {
    cgen_->frame()->ForgetElements(cgen_->frame()->height() - expected_height_);
    cgen_->frame()->Push(arg);
  }
  DoBind(mergable_elements);
  *arg = cgen_->frame()->Pop();
}


// -------------------------------------------------------------------------
// ShadowTarget implementation.

ShadowTarget::ShadowTarget(BreakTarget* shadowed) {
  ASSERT(shadowed != NULL);
  other_target_ = shadowed;

#ifdef DEBUG
  is_shadowing_ = true;
#endif
  // While shadowing this shadow target saves the state of the original.
  shadowed->CopyTo(this);

  // The original's state is reset.  We do not Unuse it because that
  // would delete the expected frame and assert that the target is not
  // linked.
  shadowed->Reset();
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());
  shadowed->set_expected_height(cgen_->frame()->height());

  // Setting the code generator to null prevents the shadow target from
  // being used until shadowing stops.
  cgen_ = NULL;
  masm_ = NULL;
}


void ShadowTarget::StopShadowing() {
  ASSERT(is_shadowing_);

  // This target does not have a valid code generator yet.
  cgen_ = other_target_->code_generator();
  ASSERT(cgen_ != NULL);
  masm_ = cgen_->masm();

  // The states of this target, which was shadowed, and the original
  // target, which was shadowing, are swapped.
  BreakTarget temp;
  other_target_->CopyTo(&temp);
  CopyTo(other_target_);
  temp.CopyTo(this);
  temp.Reset();  // So the destructor does not deallocate virtual frames.

#ifdef DEBUG
  is_shadowing_ = false;
#endif
}


} }  // namespace v8::internal
