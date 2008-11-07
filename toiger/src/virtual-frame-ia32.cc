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

// -------------------------------------------------------------------------
// VirtualFrame implementation.

#define __ masm_->

VirtualFrame::VirtualFrame(CodeGenerator* cgen)
    : masm_(cgen->masm()),
      parameter_count_(cgen->scope()->num_parameters()),
      frame_local_count_(cgen->scope()->num_stack_slots()),
      height_(0) {
}


VirtualFrame::VirtualFrame(VirtualFrame* original)
    : masm_(original->masm_),
      parameter_count_(original->parameter_count_),
      frame_local_count_(original->frame_local_count_),
      height_(original->height_) {
}


void VirtualFrame::Forget(int count) {
  ASSERT(count >= 0);
  ASSERT(height_ >= count);
  height_ -= count;
}


void VirtualFrame::MergeTo(VirtualFrame* expected) {
  ASSERT(masm_ == expected->masm_);
  ASSERT(frame_local_count_ == expected->frame_local_count_);
  ASSERT(parameter_count_ == expected->parameter_count_);
  ASSERT(height_ == expected->height_);
}


void VirtualFrame::Enter() {
  Comment cmnt(masm_, "[ Enter JS frame");
  __ push(ebp);
  __ mov(ebp, Operand(esp));

  // Store the context and the function in the frame.
  __ push(esi);
  __ push(edi);

  // Clear the function slot when generating debug code.
  if (FLAG_debug_code) {
    __ Set(edi, Immediate(reinterpret_cast<int>(kZapValue)));
  }
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
  __ pop(ebp);
}


void VirtualFrame::AllocateLocals() {
  if (frame_local_count_ > 0) {
    Comment cmnt(masm_, "[ Allocate space for locals");
    __ Set(eax, Immediate(Factory::undefined_value()));
    for (int i = 0; i < frame_local_count_; i++) {
      __ push(eax);
    }
  }
}


#undef __

} }  // namespace v8::internal
