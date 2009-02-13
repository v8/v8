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
    cgen_->frame()->MergeTo(entry_frame_);
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

  is_linked_ = !is_bound_;
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
    RegisterFile non_frame_registers = RegisterAllocator::Reserved();
    cgen_->SetFrame(working_frame, &non_frame_registers);

    working_frame->MergeTo(entry_frame_);
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
    __ b(cc, &merge_labels_.last());
  }

  is_linked_ = !is_bound_;
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

  cgen_->frame()->SpillAll();
  VirtualFrame* target_frame = new VirtualFrame(cgen_->frame());
  target_frame->Adjust(1);
  AddReachingFrame(target_frame);
  __ bl(&merge_labels_.last());

  is_linked_ = !is_bound_;
}


void JumpTarget::Bind() {
  ASSERT(cgen_ != NULL);
  ASSERT(!is_bound());

  // Live non-frame registers are not allowed at the start of a basic
  // block.
  ASSERT(!cgen_->has_valid_frame() || cgen_->HasValidEntryRegisters());

  // Compute the frame to use for entry to the block.
  ComputeEntryFrame();

  if (is_linked()) {
    // There were forward jumps.  All the reaching frames, beginning
    // with the current frame if any, are merged to the expected one.
    int start_index = 0;
    if (!cgen_->has_valid_frame()) {
      // Pick up the first reaching frame as the code generator's
      // current frame.
      RegisterFile reserved_registers = RegisterAllocator::Reserved();
      cgen_->SetFrame(reaching_frames_[0], &reserved_registers);
      __ bind(&merge_labels_[0]);
      start_index = 1;
    }

    cgen_->frame()->MergeTo(entry_frame_);

    for (int i = start_index; i < reaching_frames_.length(); i++) {
      // Delete the current frame and jump to the block entry.
      cgen_->DeleteFrame();
      __ jmp(&entry_label_);

      // Pick up the next reaching frame as the code generator's
      // current frame.
      RegisterFile reserved_registers = RegisterAllocator::Reserved();
      cgen_->SetFrame(reaching_frames_[i], &reserved_registers);
      __ bind(&merge_labels_[i]);

      cgen_->frame()->MergeTo(entry_frame_);
    }

    __ bind(&entry_label_);

    // All but the last reaching virtual frame have been deleted, and
    // the last one is the current frame.
    reaching_frames_.Clear();
    merge_labels_.Clear();
  } else {
    // There were no forward jumps.  The current frame is merged to
    // the entry frame.
    cgen_->frame()->MergeTo(entry_frame_);
    __ bind(&entry_label_);
  }

  is_linked_ = false;
  is_bound_ = true;
}

#undef __


} }  // namespace v8::internal
