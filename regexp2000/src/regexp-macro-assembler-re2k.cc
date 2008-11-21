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
#include "ast.h"
#include "bytecodes-re2k.h"
#include "assembler-re2k.h"
#include "assembler-re2k-inl.h"
#include "regexp-macro-assembler.h"
#include "regexp-macro-assembler-re2k.h"


namespace v8 { namespace internal {


RegExpMacroAssemblerRe2k::~RegExpMacroAssemblerRe2k() {
}


RegExpMacroAssemblerRe2k::Re2kImplementation
RegExpMacroAssemblerRe2k::Implementation() {
  return kBytecodeImplementation;
}


void RegExpMacroAssemblerRe2k::Bind(Label* l) {
  assembler_->Bind(l);
}


void RegExpMacroAssemblerRe2k::EmitOrLink(Label* l) {
  assembler_->EmitOrLink(l);
}


void RegExpMacroAssemblerRe2k::PopRegister(int register_index) {
  assembler_->PopRegister(register_index);
}


void RegExpMacroAssemblerRe2k::PushRegister(int register_index) {
  assembler_->PushRegister(register_index);
}


void RegExpMacroAssemblerRe2k::WriteCurrentPositionToRegister(
    int register_index) {
  assembler_->WriteCurrentPositionToRegister(register_index);
}


void RegExpMacroAssemblerRe2k::ReadCurrentPositionFromRegister(
    int register_index) {
  assembler_->ReadCurrentPositionFromRegister(register_index);
}


void RegExpMacroAssemblerRe2k::WriteStackPointerToRegister(int register_index) {
  assembler_->WriteStackPointerToRegister(register_index);
}


void RegExpMacroAssemblerRe2k::ReadStackPointerFromRegister(
    int register_index) {
  assembler_->ReadStackPointerFromRegister(register_index);
}


void RegExpMacroAssemblerRe2k::SetRegister(int register_index, int to) {
  assembler_->SetRegister(register_index, to);
}


void RegExpMacroAssemblerRe2k::AdvanceRegister(int register_index, int by) {
  assembler_->AdvanceRegister(register_index, by);
}


void RegExpMacroAssemblerRe2k::PopCurrentPosition() {
  assembler_->PopCurrentPosition();
}


void RegExpMacroAssemblerRe2k::PushCurrentPosition() {
  assembler_->PushCurrentPosition();
}


void RegExpMacroAssemblerRe2k::Backtrack() {
  assembler_->PopBacktrack();
}


void RegExpMacroAssemblerRe2k::GoTo(Label* l) {
  assembler_->GoTo(l);
}


void RegExpMacroAssemblerRe2k::PushBacktrack(Label* l) {
  assembler_->PushBacktrack(l);
}


void RegExpMacroAssemblerRe2k::Succeed() {
  assembler_->Succeed();
}


void RegExpMacroAssemblerRe2k::Fail() {
  assembler_->Fail();
}


void RegExpMacroAssemblerRe2k::AdvanceCurrentPosition(int by) {
  assembler_->AdvanceCP(by);
}


void RegExpMacroAssemblerRe2k::CheckCurrentPosition(
  int register_index,
  Label* on_equal) {
  // TODO(erikcorry): Implement.
  UNREACHABLE();
}


void RegExpMacroAssemblerRe2k::LoadCurrentCharacter(int cp_offset,
                                                    Label* on_failure) {
  assembler_->LoadCurrentChar(cp_offset, on_failure);
}


void RegExpMacroAssemblerRe2k::CheckCharacterLT(uc16 limit, Label* on_less) {
  assembler_->CheckCharacterLT(limit, on_less);
}


void RegExpMacroAssemblerRe2k::CheckCharacterGT(uc16 limit, Label* on_greater) {
  assembler_->CheckCharacterGT(limit, on_greater);
}


void RegExpMacroAssemblerRe2k::CheckCharacter(uc16 c, Label* on_equal) {
  assembler_->CheckCharacter(c, on_equal);
}


void RegExpMacroAssemblerRe2k::CheckNotCharacter(uc16 c, Label* on_not_equal) {
  assembler_->CheckNotCharacter(c, on_not_equal);
}


void RegExpMacroAssemblerRe2k::CheckNotBackReference(int start_reg,
                                                     Label* on_not_equal) {
  assembler_->CheckNotBackReference(start_reg, on_not_equal);
}


void RegExpMacroAssemblerRe2k::CheckBitmap(uc16 start,
                                           Label* bitmap,
                                           Label* on_zero) {
  assembler_->LookupMap1(start, bitmap, on_zero);
}


void RegExpMacroAssemblerRe2k::DispatchHalfNibbleMap(
    uc16 start,
    Label* half_nibble_map,
    const Vector<Label*>& table) {
  assembler_->LookupMap2(start, half_nibble_map, table);
}


void RegExpMacroAssemblerRe2k::DispatchByteMap(uc16 start,
                                               Label* byte_map,
                                               const Vector<Label*>& table) {
  assembler_->LookupMap8(start, byte_map, table);
}


void RegExpMacroAssemblerRe2k::DispatchHighByteMap(
    byte start,
    Label* byte_map,
    const Vector<Label*>& table) {
  assembler_->LookupHighMap8(start, byte_map, table);
}


void RegExpMacroAssemblerRe2k::CheckCharacters(
  Vector<const uc16> str,
  int cp_offset,
  Label* on_failure) {
  for (int i = str.length() - 1; i >= 0; i--) {
    assembler_->LoadCurrentChar(cp_offset + i, on_failure);
    assembler_->CheckNotCharacter(str[i], on_failure);
  }
}


void RegExpMacroAssemblerRe2k::IfRegisterLT(int register_index,
                                            int comparand,
                                            Label* if_less_than) {
  ASSERT(comparand >= 0 && comparand <= 65535);
  assembler_->CheckRegisterLT(register_index, comparand, if_less_than);
}


void RegExpMacroAssemblerRe2k::IfRegisterGE(int register_index,
                                            int comparand,
                                            Label* if_greater_or_equal) {
  ASSERT(comparand >= 0 && comparand <= 65535);
  assembler_->CheckRegisterGE(register_index, comparand, if_greater_or_equal);
}


Handle<Object> RegExpMacroAssemblerRe2k::GetCode() {
  Handle<ByteArray> array = Factory::NewByteArray(assembler_->length());
  assembler_->Copy(array->GetDataStartAddress());
  return array;
}

} }  // namespace v8::internal
