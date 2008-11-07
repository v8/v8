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

VirtualFrame::VirtualFrame(CodeGenerator* cgen) {
  ASSERT(cgen->scope() != NULL);

  masm_ = cgen->masm();
  frame_local_count_ = cgen->scope()->num_stack_slots();
  parameter_count_ = cgen->scope()->num_parameters();
}


VirtualFrame::VirtualFrame(VirtualFrame* original) {
  ASSERT(original == NULL);
}


void VirtualFrame::MergeTo(VirtualFrame* expected) {
  ASSERT(expected == NULL);
}


void VirtualFrame::Enter() {
  Comment cmnt(masm_, "[ Enter JS frame");
#ifdef DEBUG
  { Label done, fail;
    __ tst(r1, Operand(kSmiTagMask));
    __ b(eq, &fail);
    __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));
    __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
    __ cmp(r2, Operand(JS_FUNCTION_TYPE));
    __ b(eq, &done);
    __ bind(&fail);
    __ stop("CodeGenerator::EnterJSFrame - r1 not a function");
    __ bind(&done);
  }
#endif  // DEBUG

  __ stm(db_w, sp, r1.bit() | cp.bit() | fp.bit() | lr.bit());
  // Adjust FP to point to saved FP.
  __ add(fp, sp, Operand(2 * kPointerSize));
}


void VirtualFrame::Exit() {
  Comment cmnt(masm_, "[ Exit JS frame");
  // Drop the execution stack down to the frame pointer and restore the caller
  // frame pointer and return address.
  __ mov(sp, fp);
  __ ldm(ia_w, sp, fp.bit() | lr.bit());
}


void VirtualFrame::AllocateLocals() {
  if (frame_local_count_ > 0) {
    Comment cmnt(masm_, "[ Allocate space for locals");
      // Initialize stack slots with 'undefined' value.
    __ mov(ip, Operand(Factory::undefined_value()));
    for (int i = 0; i < frame_local_count_; i++) {
      __ push(ip);
    }
  }
}

#undef __

} }  // namespace v8::internal
