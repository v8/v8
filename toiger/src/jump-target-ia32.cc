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
    : expected_frame_(NULL),
      cgen_(cgen),
      masm_(cgen->masm()) {
}


JumpTarget::JumpTarget()
    : expected_frame_(NULL),
      cgen_(NULL),
      masm_(NULL) {
}


void JumpTarget::set_code_generator(CodeGenerator* cgen) {
  ASSERT(cgen != NULL);
  ASSERT(cgen_ == NULL);
  cgen_ = cgen;
  masm_ = cgen->masm();
}


void JumpTarget::Jump() {
  // Precondition: there is a current frame.  There may or may not be an
  // expected frame at the label.
  ASSERT(cgen_ != NULL);

  VirtualFrame* current_frame = cgen_->frame();
  ASSERT(current_frame != NULL);
  ASSERT(cgen_->HasValidEntryRegisters());

  if (expected_frame_ == NULL) {
    current_frame->MakeMergable();
    expected_frame_ = current_frame;
    ASSERT(cgen_->HasValidEntryRegisters());
    cgen_->SetFrame(NULL);
  } else {
    current_frame->MergeTo(expected_frame_);
    ASSERT(cgen_->HasValidEntryRegisters());
    cgen_->DeleteFrame();
  }

  __ jmp(&label_);
  // Postcondition: there is no current frame but there is an expected frame
  // at the label.
}


void JumpTarget::Jump(Result* arg) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

  cgen_->frame()->Push(arg);
  Jump();
}


void JumpTarget::Branch(Condition cc, Hint hint) {
  // Precondition: there is a current frame.  There may or may not be an
  // expected frame at the label.
  ASSERT(cgen_ != NULL);
  ASSERT(masm_ != NULL);

  VirtualFrame* current_frame = cgen_->frame();
  ASSERT(current_frame != NULL);
  ASSERT(cgen_->HasValidEntryRegisters());

  if (expected_frame_ == NULL) {
    expected_frame_ = new VirtualFrame(current_frame);
    // For a branch, the frame at the fall-through basic block (not labeled)
    // does not need to be mergable, but only the other (labeled) one.  That
    // is achieved by reversing the condition and emitting the make mergable
    // code as the actual fall-through block.  This is necessary only when
    // MakeMergable will generate code.
    if (expected_frame_->RequiresMergeCode()) {
      Label original_fall_through;
      __ j(NegateCondition(cc), &original_fall_through, NegateHint(hint));
      expected_frame_->MakeMergable();
      __ jmp(&label_);
      __ bind(&original_fall_through);
    } else {
      expected_frame_->MakeMergable();
      ASSERT(cgen_->HasValidEntryRegisters());
      __ j(cc, &label_, hint);
    }
  } else {
    // We negate the condition and emit the code to merge to the expected
    // frame immediately.
    //
    // TODO(): This should be replaced with a solution that emits the
    // merge code for forward CFG edges at the appropriate entry to the
    // target block.
    Label original_fall_through;
    __ j(NegateCondition(cc), &original_fall_through, NegateHint(hint));
    VirtualFrame* working_frame = new VirtualFrame(current_frame);
    cgen_->SetFrame(working_frame);
    working_frame->MergeTo(expected_frame_);
    ASSERT(cgen_->HasValidEntryRegisters());
    __ jmp(&label_);
    cgen_->SetFrame(current_frame);
    delete working_frame;
    __ bind(&original_fall_through);
  }
  // Postcondition: there is both a current frame and an expected frame at
  // the label and they match.
}


void JumpTarget::Branch(Condition cc, Result* arg, Hint hint) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->has_valid_frame());

#ifdef DEBUG
  // We want register results at the call site to stay in the same registers
  // on the fall-through branch.
  Result::Type arg_type = arg->type();
  Register arg_reg = arg->is_register() ? arg->reg() : no_reg;
#endif

  cgen_->frame()->Push(arg);
  Branch(cc, hint);
  *arg = cgen_->frame()->Pop();

  ASSERT(arg->type() == arg_type);
  ASSERT(!arg->is_register() || arg->reg().is(arg_reg));
}


void JumpTarget::Branch(Condition cc, Result* arg0, Result* arg1, Hint hint) {
  ASSERT(cgen_ != NULL);
  ASSERT(cgen_->frame() != NULL);

#ifdef DEBUG
  // We want register results at the call site to stay in the same registers
  // on the fall-through branch.
  Result::Type arg0_type = arg0->type();
  Register arg0_reg = arg0->is_register() ? arg0->reg() : no_reg;
  Result::Type arg1_type = arg1->type();
  Register arg1_reg = arg1->is_register() ? arg1->reg() : no_reg;
#endif

  cgen_->frame()->Push(arg0);
  cgen_->frame()->Push(arg1);
  Branch(cc, hint);
  *arg1 = cgen_->frame()->Pop();
  *arg0 = cgen_->frame()->Pop();

  ASSERT(arg0->type() == arg0_type);
  ASSERT(!arg0->is_register() || arg0->reg().is(arg0_reg));
  ASSERT(arg1->type() == arg1_type);
  ASSERT(!arg1->is_register() || arg1->reg().is(arg1_reg));
}


void JumpTarget::Call() {
  // Precondition: there is a current frame, and there is no expected frame
  // at the label.
  ASSERT(cgen_ != NULL);
  ASSERT(masm_ != NULL);

  VirtualFrame* current_frame = cgen_->frame();
  ASSERT(current_frame != NULL);
  ASSERT(expected_frame_ == NULL);
  ASSERT(cgen_->HasValidEntryRegisters());

  expected_frame_ = new VirtualFrame(current_frame);
  expected_frame_->MakeMergable();
  // Adjust the expected frame's height to account for the return address
  // pushed by the call instruction.
  expected_frame_->Adjust(1);
  ASSERT(cgen_->HasValidEntryRegisters());

  __ call(&label_);
  // Postcondition: there is both a current frame and an expected frame at
  // the label.  The current frame is one shorter than the one at the label
  // (which contains the return address in memory).
}


void JumpTarget::Bind() {
  // Precondition: there is either a current frame or an expected frame at
  // the label (and possibly both).  The label is unbound.
  ASSERT(cgen_ != NULL);
  ASSERT(masm_ != NULL);

  VirtualFrame* current_frame = cgen_->frame();
  ASSERT(current_frame != NULL || expected_frame_ != NULL);
  ASSERT(!label_.is_bound());

  if (expected_frame_ == NULL) {
    ASSERT(cgen_->HasValidEntryRegisters());
    // When a label is bound the current frame becomes the expected frame at
    // the label.  This requires the current frame to be mergable.
    current_frame->MakeMergable();
    ASSERT(cgen_->HasValidEntryRegisters());
    expected_frame_ = new VirtualFrame(current_frame);
  } else if (current_frame == NULL) {
    cgen_->SetFrame(new VirtualFrame(expected_frame_));
    ASSERT(cgen_->HasValidEntryRegisters());
  } else {
    ASSERT(cgen_->HasValidEntryRegisters());
    current_frame->MergeTo(expected_frame_);
    ASSERT(cgen_->HasValidEntryRegisters());
  }

  __ bind(&label_);
  // Postcondition: there is both a current frame and an expected frame at
  // the label and they match.  The label is bound.
}


void JumpTarget::Bind(Result* arg) {
  ASSERT(cgen_ != NULL);

#ifdef DEBUG
  // We want register results at the call site to stay in the same
  // registers.
  bool had_entry_frame = false;
  Result::Type arg_type;
  Register arg_reg;
#endif

  if (cgen_->has_valid_frame()) {
#ifdef DEBUG
    had_entry_frame = true;
    arg_type = arg->type();
    arg_reg = arg->is_register() ? arg->reg() : no_reg;
#endif
    cgen_->frame()->Push(arg);
  }
  Bind();
  *arg = cgen_->frame()->Pop();

  ASSERT(!had_entry_frame || arg->type() == arg_type);
  ASSERT(!had_entry_frame || !arg->is_register() || arg->reg().is(arg_reg));
}


void JumpTarget::Bind(Result* arg0, Result* arg1) {
  ASSERT(cgen_ != NULL);

#ifdef DEBUG
  // We want register results at the call site to stay in the same
  // registers.
  bool had_entry_frame = false;
  Result::Type arg0_type;
  Register arg0_reg;
  Result::Type arg1_type;
  Register arg1_reg;
#endif

  if (cgen_->frame() != NULL) {
#ifdef DEBUG
    had_entry_frame = true;
    arg0_type = arg0->type();
    arg0_reg = arg0->is_register() ? arg0->reg() : no_reg;
    arg1_type = arg1->type();
    arg1_reg = arg1->is_register() ? arg1->reg() : no_reg;
#endif
    cgen_->frame()->Push(arg0);
    cgen_->frame()->Push(arg1);
  }
  Bind();
  *arg1 = cgen_->frame()->Pop();
  *arg0 = cgen_->frame()->Pop();

  ASSERT(!had_entry_frame || arg0->type() == arg0_type);
  ASSERT(!had_entry_frame ||
         !arg0->is_register() ||
         arg0->reg().is(arg0_reg));
  ASSERT(!had_entry_frame || arg1->type() == arg1_type);
  ASSERT(!had_entry_frame ||
         !arg1->is_register() ||
         arg1->reg().is(arg1_reg));
}


// -------------------------------------------------------------------------
// ShadowTarget implementation.

ShadowTarget::ShadowTarget(JumpTarget* original) {
  ASSERT(original != NULL);
  original_target_ = original;
  original_pos_ = original->label()->pos_;
  original_expected_frame_ = original->expected_frame();

  // We do not call Unuse() on the orginal jump target, because we do not
  // want to delete the expected frame.
  original->label()->pos_ = 0;
  original->set_expected_frame(NULL);
#ifdef DEBUG
  is_shadowing_ = true;
#endif
}


void ShadowTarget::StopShadowing() {
  ASSERT(is_shadowing_);
  ASSERT(is_unused());

  set_code_generator(original_target_->code_generator());
  label_.pos_ = original_target_->label()->pos_;
  expected_frame_ = original_target_->expected_frame();

  original_target_->label()->pos_ = original_pos_;
  original_target_->set_expected_frame(original_expected_frame_);

#ifdef DEBUG
  is_shadowing_ = false;
#endif
}

#undef __


} }  // namespace v8::internal
