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

#ifndef V8_VIRTUAL_FRAME_ARM_H_
#define V8_VIRTUAL_FRAME_ARM_H_

#include "macro-assembler.h"

namespace v8 { namespace internal {

// -------------------------------------------------------------------------
// Virtual frames
//
// The virtual frame is an abstraction of the physical stack frame.  It
// encapsulates the parameters, frame-allocated locals, and the expression
// stack.  It supports push/pop operations on the expression stack, as well
// as random access to the expression stack elements, locals, and
// parameters.

class VirtualFrame {
 public:
  // Construct a virtual frame with the given code generator used to
  // generate code.
  explicit VirtualFrame(CodeGenerator* cgen);

  // Construct a virtual frame that is a clone of an existing one, initially
  // with an identical state.
  explicit VirtualFrame(VirtualFrame* original);

  // Make this virtual frame have a state identical to an expected virtual
  // frame.  As a side effect, code may be emitted to make this frame match
  // the expected one.
  void MergeTo(VirtualFrame* expected);

  // The current top of the expression stack as an assembly operand.
  MemOperand Top() const { return MemOperand(sp, 0); }

  // An element of the expression stack as an assembly operand.
  MemOperand Element(int index) const {
    return MemOperand(sp, index * kPointerSize);
  }

  // A frame-allocated local as an assembly operand.
  MemOperand Local(int index) const {
    ASSERT(0 <= index && index < frame_local_count_);
    return MemOperand(fp, kLocal0Offset - index * kPointerSize);
  }

  // The function frame slot.
  MemOperand Function() const { return MemOperand(fp, kFunctionOffset); }

  // The context frame slot.
  MemOperand Context() const { return MemOperand(fp, kContextOffset); }

  // A parameter as an assembly operand.
  MemOperand Parameter(int index) const {
    // Index -1 corresponds to the receiver.
    ASSERT(-1 <= index && index <= parameter_count_);
    return MemOperand(fp, (1 + parameter_count_ - index) * kPointerSize);
  }

 private:
  static const int kLocal0Offset = JavaScriptFrameConstants::kLocal0Offset;
  static const int kFunctionOffset = JavaScriptFrameConstants::kFunctionOffset;
  static const int kContextOffset = StandardFrameConstants::kContextOffset;

  MacroAssembler* masm_;

  // The number of frame-allocated locals and parameters respectively.
  int frame_local_count_;
  int parameter_count_;
};


} }  // namespace v8::internal

#endif  // V8_VIRTUAL_FRAME_ARM_H_
