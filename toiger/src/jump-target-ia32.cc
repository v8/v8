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

JumpTarget::JumpTarget(CodeGenerator* cgen) {
  ASSERT(cgen != NULL);
  expected_frame_ = NULL;
  code_generator_ = cgen;
  masm_ = cgen->masm();
}


JumpTarget::JumpTarget()
    : expected_frame_(NULL),
      code_generator_(NULL),
      masm_(NULL) {
}


void JumpTarget::set_code_generator(CodeGenerator* cgen) {
  ASSERT(cgen != NULL);
  ASSERT(code_generator_ == NULL);
  code_generator_ = cgen;
  masm_ = cgen->masm();
}


void JumpTarget::Jump() {
  // Precondition: there is a current frame.  There may or may not be an
  // expected frame at the label.
  ASSERT(code_generator_ != NULL);
  ASSERT(masm_ != NULL);

  VirtualFrame* current_frame = code_generator_->frame();
  ASSERT(current_frame != NULL);

  if (expected_frame_ == NULL) {
    expected_frame_ = current_frame;
    code_generator_->set_frame(NULL);
    // The frame at the actual function return will always have height
    // zero.
    if (code_generator_->IsActualFunctionReturn(this)) {
      expected_frame_->height_ = 0;
    }
  } else {
    // No code needs to be emitted to merge to the expected frame at the
    // actual function return.
    if (!code_generator_->IsActualFunctionReturn(this)) {
      current_frame->MergeTo(expected_frame_);
    }
    code_generator_->delete_frame();
  }

  __ jmp(&label_);
  // Postcondition: there is no current frame but there is an expected frame
  // at the label.
}


void JumpTarget::Branch(Condition cc, Hint hint) {
  // Precondition: there is a current frame.  There may or may not be an
  // expected frame at the label.
  ASSERT(code_generator_ != NULL);
  ASSERT(masm_ != NULL);

  VirtualFrame* current_frame = code_generator_->frame();
  ASSERT(current_frame != NULL);

  if (expected_frame_ == NULL) {
    expected_frame_ = new VirtualFrame(current_frame);
    // The frame at the actual function return will always have height
    // zero.
    if (code_generator_->IsActualFunctionReturn(this)) {
      expected_frame_->height_ = 0;
    }
  } else {
    // No code needs to be emitted to merge to the expected frame at the
    // actual function return.
    if (!code_generator_->IsActualFunctionReturn(this)) {
      current_frame->MergeTo(expected_frame_);
    }
  }

  __ j(cc, &label_, hint);
  // Postcondition: there is both a current frame and an expected frame at
  // the label and they match.
}


void JumpTarget::Call() {
  // Precondition: there is a current frame, and there is no expected frame
  // at the label.
  ASSERT(code_generator_ != NULL);
  ASSERT(masm_ != NULL);
  ASSERT(!code_generator_->IsActualFunctionReturn(this));

  VirtualFrame* current_frame = code_generator_->frame();
  ASSERT(current_frame != NULL);
  ASSERT(expected_frame_ == NULL);

  expected_frame_ = new VirtualFrame(current_frame);
  // Adjust the expected frame's height to account for the return address
  // pushed by the call instruction.
  expected_frame_->height_++;

  __ call(&label_);

  // Postcondition: there is both a current frame and an expected frame at
  // the label.  The current frame is one shorter than the one at the label
  // (which contains the 'return address', ie, the eip register and possibly
  // cs register).
}


void JumpTarget::Bind() {
  ASSERT(code_generator_ != NULL);
  ASSERT(masm_ != NULL);

  // Precondition: there is either a current frame or an expected frame at
  // the label (and possibly both).  The label is unbound.
  VirtualFrame* current_frame = code_generator_->frame();
  ASSERT(current_frame != NULL || expected_frame_ != NULL);
  ASSERT(!label_.is_bound());

  if (expected_frame_ == NULL) {
    expected_frame_ = new VirtualFrame(current_frame);
    // The frame at the actual function return will always have height
    // zero.
    if (code_generator_->IsActualFunctionReturn(this)) {
      expected_frame_->height_ = 0;
    }
  } else if (current_frame == NULL) {
    code_generator_->set_frame(new VirtualFrame(expected_frame_));
  } else {
    // No code needs to be emitted to merge to the expected frame at the
    // actual function return.
    if (!code_generator_->IsActualFunctionReturn(this)) {
      current_frame->MergeTo(expected_frame_);
    }
  }

  __ bind(&label_);
  // Postcondition: there is both a current frame and an expected frame at
  // the label and they match.  The label is bound.
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
