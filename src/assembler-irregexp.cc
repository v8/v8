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

// A light-weight assembler for the Irregexp byte code.


#include "v8.h"
#include "ast.h"
#include "bytecodes-irregexp.h"
#include "assembler-irregexp.h"

#include "assembler-irregexp-inl.h"


namespace v8 { namespace internal {


IrregexpAssembler::IrregexpAssembler(Vector<byte> buffer)
    : buffer_(buffer),
      pc_(0),
      own_buffer_(false) {
}


IrregexpAssembler::~IrregexpAssembler() {
  if (own_buffer_) {
    buffer_.Dispose();
  }
}


void IrregexpAssembler::PushCurrentPosition(int cp_offset) {
  ASSERT(cp_offset >= 0);
  Emit(BC_PUSH_CP);
  Emit32(cp_offset);
}


void IrregexpAssembler::PushBacktrack(Label* l) {
  Emit(BC_PUSH_BT);
  EmitOrLink(l);
}


void IrregexpAssembler::PushRegister(int index) {
  ASSERT(index >= 0);
  Emit(BC_PUSH_REGISTER);
  Emit(index);
}


void IrregexpAssembler::WriteCurrentPositionToRegister(int index,
                                                       int cp_offset) {
  ASSERT(cp_offset >= 0);
  ASSERT(index >= 0);
  Emit(BC_SET_REGISTER_TO_CP);
  Emit(index);
  Emit32(cp_offset);
}


void IrregexpAssembler::ReadCurrentPositionFromRegister(int index) {
  ASSERT(index >= 0);
  Emit(BC_SET_CP_TO_REGISTER);
  Emit(index);
}


void IrregexpAssembler::WriteStackPointerToRegister(int index) {
  ASSERT(index >= 0);
  Emit(BC_SET_REGISTER_TO_SP);
  Emit(index);
}


void IrregexpAssembler::ReadStackPointerFromRegister(int index) {
  ASSERT(index >= 0);
  Emit(BC_SET_SP_TO_REGISTER);
  Emit(index);
}


void IrregexpAssembler::SetRegister(int index, int value) {
  ASSERT(index >= 0);
  Emit(BC_SET_REGISTER);
  Emit(index);
  Emit32(value);
}


void IrregexpAssembler::AdvanceRegister(int index, int by) {
  ASSERT(index >= 0);
  Emit(BC_ADVANCE_REGISTER);
  Emit(index);
  Emit32(by);
}


void IrregexpAssembler::PopCurrentPosition() {
  Emit(BC_POP_CP);
}


void IrregexpAssembler::PopBacktrack() {
  Emit(BC_POP_BT);
}


void IrregexpAssembler::PopRegister(int index) {
  Emit(BC_POP_REGISTER);
  Emit(index);
}


void IrregexpAssembler::Fail() {
  Emit(BC_FAIL);
}


void IrregexpAssembler::Break() {
  Emit(BC_BREAK);
}


void IrregexpAssembler::Succeed() {
  Emit(BC_SUCCEED);
}


void IrregexpAssembler::Bind(Label* l) {
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


void IrregexpAssembler::AdvanceCP(int cp_offset) {
  Emit(BC_ADVANCE_CP);
  Emit32(cp_offset);
}


void IrregexpAssembler::GoTo(Label* l) {
  Emit(BC_GOTO);
  EmitOrLink(l);
}


void IrregexpAssembler::LoadCurrentChar(int cp_offset, Label* on_end) {
  Emit(BC_LOAD_CURRENT_CHAR);
  Emit32(cp_offset);
  EmitOrLink(on_end);
}


void IrregexpAssembler::CheckCharacter(uc16 c, Label* on_match) {
  Emit(BC_CHECK_CHAR);
  Emit16(c);
  EmitOrLink(on_match);
}


void IrregexpAssembler::CheckNotCharacter(uc16 c, Label* on_mismatch) {
  Emit(BC_CHECK_NOT_CHAR);
  Emit16(c);
  EmitOrLink(on_mismatch);
}

void IrregexpAssembler::OrThenCheckNotCharacter(uc16 c,
                                                uc16 mask,
                                                Label* on_mismatch) {
  Emit(BC_OR_CHECK_NOT_CHAR);
  Emit16(c);
  Emit16(mask);
  EmitOrLink(on_mismatch);
}


void IrregexpAssembler::MinusOrThenCheckNotCharacter(uc16 c,
                                                     uc16 mask,
                                                     Label* on_mismatch) {
  Emit(BC_MINUS_OR_CHECK_NOT_CHAR);
  Emit16(c);
  Emit16(mask);
  EmitOrLink(on_mismatch);
}


void IrregexpAssembler::CheckCharacterLT(uc16 limit, Label* on_less) {
  Emit(BC_CHECK_LT);
  Emit16(limit);
  EmitOrLink(on_less);
}


void IrregexpAssembler::CheckCharacterGT(uc16 limit, Label* on_greater) {
  Emit(BC_CHECK_GT);
  Emit16(limit);
  EmitOrLink(on_greater);
}


void IrregexpAssembler::CheckNotBackReference(int capture_index,
                                              Label* on_mismatch) {
  Emit(BC_CHECK_NOT_BACK_REF);
  Emit(capture_index);
  EmitOrLink(on_mismatch);
}


void IrregexpAssembler::CheckNotBackReferenceNoCase(int capture_index,
                                                    Label* on_mismatch) {
  Emit(BC_CHECK_NOT_BACK_REF_NO_CASE);
  Emit(capture_index);
  EmitOrLink(on_mismatch);
}


void IrregexpAssembler::CheckRegister(int byte_code,
                                      int reg_index,
                                      uint16_t vs,
                                      Label* on_true) {
  Emit(byte_code);
  Emit(reg_index);
  Emit16(vs);
  EmitOrLink(on_true);
}


void IrregexpAssembler::CheckRegisterLT(int reg_index,
                                        uint16_t vs,
                                        Label* on_less_than) {
  CheckRegister(BC_CHECK_REGISTER_LT, reg_index, vs, on_less_than);
}


void IrregexpAssembler::CheckRegisterGE(int reg_index,
                                        uint16_t vs,
                                        Label* on_greater_than_equal) {
  CheckRegister(BC_CHECK_REGISTER_GE, reg_index, vs, on_greater_than_equal);
}


void IrregexpAssembler::LookupMap1(uc16 start, Label* bit_map, Label* on_zero) {
  Emit(BC_LOOKUP_MAP1);
  Emit16(start);
  EmitOrLink(bit_map);
  EmitOrLink(on_zero);
}


void IrregexpAssembler::LookupMap2(uc16 start,
                                   Label* half_nibble_map,
                                   const Vector<Label*>& table) {
  Emit(BC_LOOKUP_MAP2);
  Emit16(start);
  EmitOrLink(half_nibble_map);
  ASSERT(table.length() > 0);
  ASSERT(table.length() <= 4);
  for (int i = 0; i < table.length(); i++) {
    EmitOrLink(table[i]);
  }
}


void IrregexpAssembler::LookupMap8(uc16 start,
                                   Label* byte_map,
                                   const Vector<Label*>& table) {
  Emit(BC_LOOKUP_MAP8);
  Emit16(start);
  EmitOrLink(byte_map);
  ASSERT(table.length() > 0);
  ASSERT(table.length() <= 256);
  for (int i = 0; i < table.length(); i++) {
    EmitOrLink(table[i]);
  }
}


void IrregexpAssembler::LookupHighMap8(byte start,
                                       Label* byte_map,
                                       const Vector<Label*>& table) {
  Emit(BC_LOOKUP_HI_MAP8);
  Emit(start);
  EmitOrLink(byte_map);
  ASSERT(table.length() > 0);
  ASSERT(table.length() <= 256);
  for (int i = 0; i < table.length(); i++) {
    EmitOrLink(table[i]);
  }
}


int IrregexpAssembler::length() {
  return pc_;
}


void IrregexpAssembler::Copy(Address a) {
  memcpy(a, buffer_.start(), length());
}


void IrregexpAssembler::Expand() {
  bool old_buffer_was_our_own = own_buffer_;
  Vector<byte> old_buffer = buffer_;
  buffer_ = Vector<byte>::New(old_buffer.length() * 2);
  own_buffer_ = true;
  memcpy(buffer_.start(), old_buffer.start(), old_buffer.length());
  if (old_buffer_was_our_own) {
    old_buffer.Dispose();
  }
}


} }  // namespace v8::internal
