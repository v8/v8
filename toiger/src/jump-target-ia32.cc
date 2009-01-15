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

JumpTarget::JumpTarget(CodeGenerator* cgen)
    : cgen_(cgen),
      reaching_frames_(0),
      merge_labels_(0),
      expected_frame_(NULL) {
  ASSERT(cgen_ != NULL);
  masm_ = cgen_->masm();
}


JumpTarget::JumpTarget()
    : cgen_(NULL),
      masm_(NULL),
      reaching_frames_(0),
      merge_labels_(0),
      expected_frame_(NULL) {
}


void JumpTarget::set_code_generator(CodeGenerator* cgen) {
  ASSERT(cgen != NULL);
  ASSERT(cgen_ == NULL);
  cgen_ = cgen;
  masm_ = cgen->masm();
}


void JumpTarget::Jump() {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());
  ASSERT(!cgen_->has_cc());
  // Live non-frame registers are not allowed at unconditional jumps
  // because we have no way of invalidating the corresponding results
  // which are still live in the C++ code.
  ASSERT(cgen_->HasValidEntryRegisters());

  if (is_bound()) {
    // Backward jump.  There is an expected frame to merge to.
    cgen_->frame()->MergeTo(expected_frame_);
    cgen_->DeleteFrame();
    __ jmp(&entry_label_);
  } else {
    // Forward jump.  The current frame is added to the end of the list
    // of frames reaching the target block and a jump to the merge code
    // is emitted.
    AddReachingFrame(cgen_->frame());
    RegisterFile empty;
    cgen_->SetFrame(NULL, &empty);
    __ jmp(&merge_labels_.last());
  }
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


void JumpTarget::Branch(Condition cc, Hint hint) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());
  ASSERT(!cgen_->has_cc());

  if (is_bound()) {
    // Backward branch.  We have an expected frame to merge to on the
    // backward edge.  We negate the condition and emit the merge code
    // here.
    //
    // TODO(): we should try to avoid negating the condition in the case
    // where there is no merge code to emit.  Otherwise, we emit a
    // branch around an unconditional jump.
    Label original_fall_through;
    __ j(NegateCondition(cc), &original_fall_through, NegateHint(hint));
    // Swap the current frame for a copy of it, saving non-frame
    // register reference counts and invalidating all non-frame register
    // references except the reserved ones on the backward edge.
    VirtualFrame* original_frame = cgen_->frame();
    VirtualFrame* working_frame = new VirtualFrame(original_frame);
    RegisterFile non_frame_registers = RegisterAllocator::Reserved();
    cgen_->SetFrame(working_frame, &non_frame_registers);

    working_frame->MergeTo(expected_frame_);
    cgen_->DeleteFrame();
    __ jmp(&entry_label_);

    // Restore the frame and its associated non-frame registers.
    cgen_->SetFrame(original_frame, &non_frame_registers);
    __ bind(&original_fall_through);
  } else {
    // Forward branch.  A copy of the current frame is added to the end
    // of the list of frames reaching the target block and a branch to
    // the merge code is emitted.
    AddReachingFrame(new VirtualFrame(cgen_->frame()));
    __ j(cc, &merge_labels_.last(), hint);
  }
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

#undef DECLARE_ARGCHECK_VARS
#undef ASSERT_ARGCHECK


void JumpTarget::Call() {
  // Call is used to push the address of the catch block on the stack as
  // a return address when compiling try/catch and try/finally.  We
  // fully spill the frame before making the call.  The expected frame
  // at the label (which should be the only one) is the spilled current
  // frame plus an in-memory return address.  The "fall-through" frame
  // at the return site is the spilled current frame.
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());
  ASSERT(!cgen_->has_cc());
  // There are no non-frame references across the call.
  ASSERT(cgen_->HasValidEntryRegisters());
  ASSERT(!is_linked());

  cgen_->frame()->SpillAll();
  VirtualFrame* target_frame = new VirtualFrame(cgen_->frame());
  target_frame->Adjust(1);
  AddReachingFrame(target_frame);
  __ call(&merge_labels_.last());
}


void JumpTarget::Bind() {
  ASSERT(cgen_ != NULL);
  ASSERT(is_linked() || cgen_->has_valid_frame());
  ASSERT(!cgen_->has_cc());
  ASSERT(!is_bound());
  ASSERT(!cgen_->has_cc());

  if (is_linked()) {
    // There were forward jumps.  A mergable frame is created and all
    // the frames reaching the block via forward jumps are merged to it.
    ASSERT(reaching_frames_.length() == merge_labels_.length());

    // Choose a frame as the basis of the expected frame, and make it
    // mergable.  If there is a current frame use it, otherwise use the
    // first in the list (there will be at least one).
    int start_index = 0;
    if (cgen_->has_valid_frame()) {
      // Live non-frame registers are not allowed at the start of a labeled
      // basic block.
      ASSERT(cgen_->HasValidEntryRegisters());
    } else {
      RegisterFile reserved_registers = RegisterAllocator::Reserved();
      cgen_->SetFrame(reaching_frames_[start_index], &reserved_registers);
      __ bind(&merge_labels_[start_index++]);
    }
    cgen_->frame()->MakeMergable();
    expected_frame_ = new VirtualFrame(cgen_->frame());

    for (int i = start_index; i < reaching_frames_.length(); i++) {
      cgen_->DeleteFrame();
      __ jmp(&entry_label_);

      RegisterFile reserved_registers = RegisterAllocator::Reserved();
      cgen_->SetFrame(reaching_frames_[i], &reserved_registers);
      __ bind(&merge_labels_[i]);

      cgen_->frame()->MergeTo(expected_frame_);
    }
    __ bind(&entry_label_);

    // All but the last reaching virtual frame have been deleted, and
    // the last one is the current frame.
    reaching_frames_.Clear();
    merge_labels_.Clear();
  } else {
    // There were no forward jumps.  There must be a current frame,
    // which is made mergable and used as the expected frame.
    ASSERT(cgen_->HasValidEntryRegisters());
    cgen_->frame()->MakeMergable();
    expected_frame_ = new VirtualFrame(cgen_->frame());
    __ bind(&entry_label_);
  }
}


void JumpTarget::Bind(Result* arg) {
  ASSERT(cgen_ != NULL);

  if (cgen_->has_valid_frame()) {
    cgen_->frame()->Push(arg);
  }
  Bind();
  *arg = cgen_->frame()->Pop();
}


void JumpTarget::Bind(Result* arg0, Result* arg1) {
  ASSERT(cgen_ != NULL);

  if (cgen_->has_valid_frame()) {
    cgen_->frame()->Push(arg0);
    cgen_->frame()->Push(arg1);
  }
  Bind();
  *arg1 = cgen_->frame()->Pop();
  *arg0 = cgen_->frame()->Pop();
}


void JumpTarget::CopyTo(JumpTarget* destination) {
  ASSERT(destination != NULL);
  destination->cgen_ = cgen_;
  destination->masm_ = masm_;

  destination->reaching_frames_.Clear();
  destination->merge_labels_.Clear();
  ASSERT(reaching_frames_.length() == merge_labels_.length());
  for (int i = 0; i < reaching_frames_.length(); i++) {
    destination->reaching_frames_.Add(reaching_frames_[i]);
    destination->merge_labels_.Add(merge_labels_[i]);
  }
  destination->expected_frame_ = expected_frame_;
  destination->entry_label_ = entry_label_;
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

  // The states of this target, which was shadowed, and the original
  // target, which was shadowing, are swapped.
  JumpTarget temp;

  other_target_->CopyTo(&temp);
  CopyTo(other_target_);
  temp.CopyTo(this);
  temp.Reset();  // So the destructor does not deallocate virtual frames.

  // The shadowing target does not have a valid code generator yet.
  other_target_->set_code_generator(cgen_);
#ifdef DEBUG
  is_shadowing_ = false;
#endif
}

#undef __


} }  // namespace v8::internal
