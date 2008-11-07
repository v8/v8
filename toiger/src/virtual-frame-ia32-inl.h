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

#ifndef V8_VIRTUAL_FRAME_INL_H_
#define V8_VIRTUAL_FRAME_INL_H_

namespace v8 { namespace internal {


// -------------------------------------------------------------------------
// VirtualFrame inline functions.

#define __ masm_->

void VirtualFrame::PushTryHandler(HandlerType type) {
  // Grow the expression stack by handler size less two (the return address
  // is already pushed by a call instruction, and PushTryHandler from the
  // macro assembler will leave the top of stack in the eax register to be
  // pushed separately).
  height_ += (kHandlerSize - 2);
  __ PushTryHandler(IN_JAVASCRIPT, type);
  // TODO(1222589): remove the reliance of PushTryHandler on a cached TOS
  Push(eax);
}


void VirtualFrame::CallStub(CodeStub* stub, int frame_arg_count) {
  ASSERT(frame_arg_count >= 0);
  ASSERT(height_ >= frame_arg_count);
  height_ -= frame_arg_count;
  __ CallStub(stub);
}


void VirtualFrame::CallRuntime(Runtime::Function* f, int frame_arg_count) {
  ASSERT(frame_arg_count >= 0);
  ASSERT(height_ >= frame_arg_count);
  height_ -= frame_arg_count;
  __ CallRuntime(f, frame_arg_count);
}


void VirtualFrame::CallRuntime(Runtime::FunctionId id, int frame_arg_count) {
  ASSERT(frame_arg_count >= 0);
  ASSERT(height_ >= frame_arg_count);
  height_ -= frame_arg_count;
  __ CallRuntime(id, frame_arg_count);
}


void VirtualFrame::InvokeBuiltin(Builtins::JavaScript id,
                                 InvokeFlag flag,
                                 int frame_arg_count) {
  ASSERT(frame_arg_count >= 0);
  ASSERT(height_ >= frame_arg_count);
  height_ -= frame_arg_count;
  __ InvokeBuiltin(id, flag);
}


void VirtualFrame::CallCode(Handle<Code> code,
                            RelocInfo::Mode rmode,
                            int frame_arg_count) {
  ASSERT(frame_arg_count >= 0);
  ASSERT(height_ >= frame_arg_count);
  height_ -= frame_arg_count;
  __ call(code, rmode);
}


void VirtualFrame::Drop(int count) {
  ASSERT(count >= 0);
  ASSERT(height_ >= count);
  if (count > 0) {
    __ add(Operand(esp), Immediate(count * kPointerSize));
    height_ -= count;
  }
}


void VirtualFrame::Pop() {
  ASSERT(height_ > 0);
  __ add(Operand(esp), Immediate(kPointerSize));
  height_--;
}


void VirtualFrame::Pop(Register reg) {
  ASSERT(height_ > 0);
  __ pop(reg);
  height_--;
}


void VirtualFrame::Pop(Operand operand) {
  ASSERT(height_ > 0);
  __ pop(operand);
  height_--;
}


void VirtualFrame::Push(Register reg) {
  height_++;
  __ push(reg);
}


void VirtualFrame::Push(Operand operand) {
  height_++;
  __ push(operand);
}


void VirtualFrame::Push(Immediate immediate) {
  height_++;
  __ push(immediate);
}

#undef __

} }  // namespace v8::internal

#endif  // V8_VIRTUAL_FRAME_INL_H_
