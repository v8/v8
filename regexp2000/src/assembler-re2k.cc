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

// A light-weight assembler for the Regexp2000 byte code.


#include "v8.h"
#include "ast.h"
#include "bytecodes-re2k.h"
#include "assembler-re2k.h"

#include "assembler-re2k-inl.h"


namespace v8 { namespace internal {


Re2kAssembler::Re2kAssembler(Vector<byte> buffer)
  : buffer_(buffer),
    pc_(0),
    own_buffer_(false) {
}


Re2kAssembler::~Re2kAssembler() {
}


void Re2kAssembler::PushCurrentPosition(int cp_offset) {
  ASSERT(cp_offset >= 0);
  Emit(BC_PUSH_CP);
  Emit32(cp_offset);
}


void Re2kAssembler::PushBacktrack(Label* l) {
  Emit(BC_PUSH_BT);
  EmitOrLink(l);
}


void Re2kAssembler::PushRegister(int index) {
  ASSERT(index >= 0);
  Emit(BC_PUSH_REGISTER);
  Emit(index);
}


void Re2kAssembler::SetRegisterToCurrentPosition(int index, int cp_offset) {
  ASSERT(cp_offset >= 0);
  ASSERT(index >= 0);
  Emit(BC_SET_REGISTER_TO_CP);
  Emit(index);
  Emit32(cp_offset);
}


void Re2kAssembler::SetRegister(int index, int value) {
  ASSERT(index >= 0);
  Emit(BC_SET_REGISTER);
  Emit(index);
  Emit32(value);
}


void Re2kAssembler::PopCurrentPosition() {
  Emit(BC_POP_CP);
}


void Re2kAssembler::PopBacktrack() {
  Emit(BC_POP_BT);
}


void Re2kAssembler::PopRegister(int index) {
  Emit(BC_POP_REGISTER);
  Emit(index);
}


void Re2kAssembler::Fail() {
  Emit(BC_FAIL);
}


void Re2kAssembler::Break() {
  Emit(BC_BREAK);
}


void Re2kAssembler::FailIfWithin(int distance_from_end) {
  Emit(BC_FAIL_IF_WITHIN);
  Emit32(distance_from_end);
}


void Re2kAssembler::Succeed() {
  Emit(BC_SUCCEED);
}


void Re2kAssembler::Bind(Label* l) {
  ASSERT(!l->is_bound());
  if (l->is_linked()) {
    int pos = l->pos();
    while (pos != 0) {
      int fixup = pos;
      pos = Load32(buffer_.start() + fixup);
      Store32(buffer_.start() + fixup, pc_);
    }
  }
  l->bind_to(pc_);
}


void Re2kAssembler::AdvanceCP(int cp_offset) {
  Emit(BC_ADVANCE_CP);
  Emit32(cp_offset);
}


void Re2kAssembler::GoTo(Label* l) {
  Emit(BC_GOTO);
  EmitOrLink(l);
}


void Re2kAssembler::LoadCurrentChar(int cp_offset) {
  Emit(BC_LOAD_CURRENT_CHAR);
  Emit32(cp_offset);
}


void Re2kAssembler::CheckChar(uc16 c, Label* on_mismatch) {
  Emit(BC_CHECK_CHAR);
  Emit16(c);
  EmitOrLink(on_mismatch);
}


void Re2kAssembler::CheckNotChar(uc16 c, Label* on_match) {
  Emit(BC_CHECK_NOT_CHAR);
  Emit16(c);
  EmitOrLink(on_match);
}


void Re2kAssembler::CheckEnd(Label* on_not_end) {
  Emit(BC_CHECK_END);
  EmitOrLink(on_not_end);
}


void Re2kAssembler::CheckNotEnd(Label* on_end) {
  Emit(BC_CHECK_NOT_END);
  EmitOrLink(on_end);
}


void Re2kAssembler::CheckRange(uc16 start, uc16 end, Label* on_mismatch) {
  Emit(BC_CHECK_RANGE);
  Emit16(start);
  Emit16(end);
  EmitOrLink(on_mismatch);
}


void Re2kAssembler::CheckNotRange(uc16 start, uc16 end, Label* on_match) {
  Emit(BC_CHECK_NOT_RANGE);
  Emit16(start);
  Emit16(end);
  EmitOrLink(on_match);
}


void Re2kAssembler::CheckBackref(int capture_index,
                                 Label* on_mismatch,
                                 int cp_offset) {
  Emit(BC_CHECK_BACKREF);
  Emit32(cp_offset);
  Emit(capture_index);
  EmitOrLink(on_mismatch);
}


void Re2kAssembler::CheckNotBackref(int capture_index,
                                    Label* on_match,
                                    int cp_offset) {
  Emit(BC_CHECK_NOT_BACKREF);
  Emit32(cp_offset);
  Emit(capture_index);
  EmitOrLink(on_match);
}


void Re2kAssembler::CheckRegister(int byte_code,
                                  int reg_index,
                                  uint16_t vs,
                                  Label* on_true) {
  Emit(byte_code);
  Emit(reg_index);
  Emit16(vs);
  EmitOrLink(on_true);
}


void Re2kAssembler::CheckRegisterLt(int reg_index,
                                    uint16_t vs,
                                    Label* on_less_than) {
  CheckRegister(BC_CHECK_REGISTER_LT, reg_index, vs, on_less_than);
}


void Re2kAssembler::CheckRegisterGe(int reg_index,
                                    uint16_t vs,
                                    Label* on_greater_than_equal) {
  CheckRegister(BC_CHECK_REGISTER_GE, reg_index, vs, on_greater_than_equal);
}


int Re2kAssembler::length() {
  return pc_;
}


void Re2kAssembler::Copy(Address a) {
  memcpy(a, buffer_.start(), length());
}

} }  // namespace v8::internal
