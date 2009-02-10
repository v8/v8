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
#include "jump-target.h"

namespace v8 { namespace internal {

// -------------------------------------------------------------------------
// JumpTarget implementation.

#define __ masm_->

JumpTarget::JumpTarget(CodeGenerator* cgen, Directionality direction)
    : cgen_(cgen),
      direction_(direction),
      reaching_frames_(0),
      merge_labels_(0),
      expected_frame_(NULL),
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
      expected_frame_(NULL),
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
  delete expected_frame_;
  expected_frame_ = NULL;
  is_bound_ = false;
  is_linked_ = false;
}


void JumpTarget::Reset() {
  reaching_frames_.Clear();
  merge_labels_.Clear();
  expected_frame_ = NULL;
  entry_label_.Unuse();
  is_bound_ = false;
  is_linked_ = false;
}


void JumpTarget::Jump() {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());
  // Live non-frame registers are not allowed at unconditional jumps
  // because we have no way of invalidating the corresponding results
  // which are still live in the C++ code.
  ASSERT(cgen_->HasValidEntryRegisters());

  if (is_bound()) {
    // Backward jump.  There is an expected frame to merge to.
    ASSERT(direction_ == BIDIRECTIONAL);
    cgen_->frame()->MergeTo(expected_frame_);
    cgen_->DeleteFrame();
    __ jmp(&entry_label_);
  } else {
    // Forward jump.  The current frame is added to the end of the list
    // of frames reaching the target block and a jump to the merge code
    // is emitted.
    AddReachingFrame(cgen_->frame());
    cgen_->SetFrame(NULL);
    __ jmp(&merge_labels_.last());
  }

  is_linked_ = !is_bound_;
}


void JumpTarget::Jump(Result* arg) {
  UNIMPLEMENTED();
}


void JumpTarget::Jump(Result* arg0, Result* arg1) {
  UNIMPLEMENTED();
}


void JumpTarget::Jump(Result* arg0, Result* arg1, Result* arg2) {
  UNIMPLEMENTED();
}


void JumpTarget::Branch(Condition cc, Hint ignored) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  if (is_bound()) {
    // Backward branch.  We have an expected frame to merge to on the
    // backward edge.  We negate the condition and emit the merge code
    // here.
    //
    // TODO(210): we should try to avoid negating the condition in the
    // case where there is no merge code to emit.  Otherwise, we emit
    // a branch around an unconditional jump.
    ASSERT(direction_ == BIDIRECTIONAL);
    Label original_fall_through;
    __ b(NegateCondition(cc), &original_fall_through);
    // Swap the current frame for a copy of it, saving non-frame
    // register reference counts and invalidating all non-frame register
    // references except the reserved ones on the backward edge.
    VirtualFrame* original_frame = cgen_->frame();
    VirtualFrame* working_frame = new VirtualFrame(original_frame);
    cgen_->SetFrame(working_frame);

    working_frame->MergeTo(expected_frame_);
    cgen_->DeleteFrame();
    __ jmp(&entry_label_);

    // Restore the frame and its associated non-frame registers.
    cgen_->SetFrame(original_frame);
    __ bind(&original_fall_through);
  } else {
    // Forward branch.  A copy of the current frame is added to the end
    // of the list of frames reaching the target block and a branch to
    // the merge code is emitted.
    AddReachingFrame(new VirtualFrame(cgen_->frame()));
    __ b(cc, &merge_labels_.last());
  }

  is_linked_ = !is_bound_;
}


void JumpTarget::Branch(Condition cc, Result* arg, Hint ignored) {
  UNIMPLEMENTED();
}


void JumpTarget::Branch(Condition cc,
                        Result* arg0,
                        Result* arg1,
                        Hint ignored) {
  UNIMPLEMENTED();
}


void JumpTarget::Branch(Condition cc,
                        Result* arg0,
                        Result* arg1,
                        Result* arg2,
                        Hint ignored) {
  UNIMPLEMENTED();
}


void JumpTarget::Branch(Condition cc,
                        Result* arg0,
                        Result* arg1,
                        Result* arg2,
                        Result* arg3,
                        Hint ignored) {
  UNIMPLEMENTED();
}


void JumpTarget::Call() {
  // Call is used to push the address of the catch block on the stack as
  // a return address when compiling try/catch and try/finally.  We
  // fully spill the frame before making the call.  The expected frame
  // at the label (which should be the only one) is the spilled current
  // frame plus an in-memory return address.  The "fall-through" frame
  // at the return site is the spilled current frame.
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());
  // There are no non-frame references across the call.
  ASSERT(cgen_->HasValidEntryRegisters());
  ASSERT(!is_linked());

  VirtualFrame* target_frame = new VirtualFrame(cgen_->frame());
  target_frame->Adjust(1);
  AddReachingFrame(target_frame);
  __ bl(&merge_labels_.last());

  is_linked_ = !is_bound_;
}


void JumpTarget::Bind() {
  ASSERT(cgen_ != NULL);
  ASSERT(!is_bound());

  if (is_linked()) {
    // There were forward jumps.  A mergable frame is created and all
    // the frames reaching the block via forward jumps are merged to it.
    ASSERT(reaching_frames_.length() == merge_labels_.length());

    // A special case is that there was only one jump to the block so
    // far, no fall-through, and there cannot be another entry because
    // the block is forward only.  In that case, simply use the single
    // frame.
    bool single_entry = (direction_ == FORWARD_ONLY) &&
                        !cgen_->has_valid_frame() &&
                        (reaching_frames_.length() == 1);
    if (single_entry) {
      // Pick up the only forward reaching frame and bind its merge
      // label.  No merge code is emitted.
      cgen_->SetFrame(reaching_frames_[0]);
      __ bind(&merge_labels_[0]);
    } else {
      // Otherwise, choose a frame as the basis of the expected frame,
      // and make it mergable.  If there is a current frame use it,
      // otherwise use the first in the list (there will be at least
      // one).
      int start_index = 0;
      if (cgen_->has_valid_frame()) {
        // Live non-frame registers are not allowed at the start of a
        // labeled basic block.
        ASSERT(cgen_->HasValidEntryRegisters());
      } else {
        cgen_->SetFrame(reaching_frames_[start_index]);
        __ bind(&merge_labels_[start_index++]);
      }
      cgen_->frame()->MakeMergable();
      expected_frame_ = new VirtualFrame(cgen_->frame());

      for (int i = start_index; i < reaching_frames_.length(); i++) {
        cgen_->DeleteFrame();
        __ jmp(&entry_label_);

        cgen_->SetFrame(reaching_frames_[i]);
        __ bind(&merge_labels_[i]);

        cgen_->frame()->MergeTo(expected_frame_);
      }

      __ bind(&entry_label_);
    }

    // All but the last reaching virtual frame have been deleted, and
    // the last one is the current frame.
    reaching_frames_.Clear();
    merge_labels_.Clear();

  } else {
    // There were no forward jumps.  If this jump target is not
    // bidirectional, there is no need to do anything.  For
    // bidirectional jump targets, the current frame is made mergable
    // and used for the expected frame.
    if (direction_ == BIDIRECTIONAL) {
      ASSERT(cgen_->HasValidEntryRegisters());
      cgen_->frame()->MakeMergable();
      expected_frame_ = new VirtualFrame(cgen_->frame());
      __ bind(&entry_label_);
    }
  }

  is_linked_ = false;
  is_bound_ = true;
}


void JumpTarget::Bind(Result* arg) {
  UNIMPLEMENTED();
}


void JumpTarget::Bind(Result* arg0, Result* arg1) {
  UNIMPLEMENTED();
}


void JumpTarget::Bind(Result* arg0, Result* arg1, Result* arg2) {
  UNIMPLEMENTED();
}


void JumpTarget::Bind(Result* arg0, Result* arg1, Result* arg2, Result* arg3) {
  UNIMPLEMENTED();
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
  destination->expected_frame_ = expected_frame_;
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

#undef __


} }  // namespace v8::internal
