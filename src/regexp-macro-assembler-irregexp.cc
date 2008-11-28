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
#include "bytecodes-irregexp.h"
#include "assembler-irregexp.h"
#include "assembler-irregexp-inl.h"
#include "regexp-macro-assembler.h"
#include "regexp-macro-assembler-irregexp.h"


namespace v8 { namespace internal {


RegExpMacroAssemblerIrregexp::~RegExpMacroAssemblerIrregexp() {
}


RegExpMacroAssemblerIrregexp::IrregexpImplementation
RegExpMacroAssemblerIrregexp::Implementation() {
  return kBytecodeImplementation;
}


void RegExpMacroAssemblerIrregexp::Bind(Label* l) {
  assembler_->Bind(l);
}


void RegExpMacroAssemblerIrregexp::EmitOrLink(Label* l) {
  assembler_->EmitOrLink(l);
}


void RegExpMacroAssemblerIrregexp::PopRegister(int register_index) {
  assembler_->PopRegister(register_index);
}


void RegExpMacroAssemblerIrregexp::PushRegister(int register_index) {
  assembler_->PushRegister(register_index);
}


void RegExpMacroAssemblerIrregexp::WriteCurrentPositionToRegister(
    int register_index) {
  assembler_->WriteCurrentPositionToRegister(register_index);
}


void RegExpMacroAssemblerIrregexp::ReadCurrentPositionFromRegister(
    int register_index) {
  assembler_->ReadCurrentPositionFromRegister(register_index);
}


void RegExpMacroAssemblerIrregexp::WriteStackPointerToRegister(
    int register_index) {
  assembler_->WriteStackPointerToRegister(register_index);
}


void RegExpMacroAssemblerIrregexp::ReadStackPointerFromRegister(
    int register_index) {
  assembler_->ReadStackPointerFromRegister(register_index);
}


void RegExpMacroAssemblerIrregexp::SetRegister(int register_index, int to) {
  assembler_->SetRegister(register_index, to);
}


void RegExpMacroAssemblerIrregexp::AdvanceRegister(int register_index, int by) {
  assembler_->AdvanceRegister(register_index, by);
}


void RegExpMacroAssemblerIrregexp::PopCurrentPosition() {
  assembler_->PopCurrentPosition();
}


void RegExpMacroAssemblerIrregexp::PushCurrentPosition() {
  assembler_->PushCurrentPosition();
}


void RegExpMacroAssemblerIrregexp::Backtrack() {
  assembler_->PopBacktrack();
}


void RegExpMacroAssemblerIrregexp::GoTo(Label* l) {
  assembler_->GoTo(l);
}


void RegExpMacroAssemblerIrregexp::PushBacktrack(Label* l) {
  assembler_->PushBacktrack(l);
}


void RegExpMacroAssemblerIrregexp::Succeed() {
  assembler_->Succeed();
}


void RegExpMacroAssemblerIrregexp::Fail() {
  assembler_->Fail();
}


void RegExpMacroAssemblerIrregexp::AdvanceCurrentPosition(int by) {
  assembler_->AdvanceCP(by);
}


void RegExpMacroAssemblerIrregexp::CheckCurrentPosition(
  int register_index,
  Label* on_equal) {
  // TODO(erikcorry): Implement.
  UNREACHABLE();
}


void RegExpMacroAssemblerIrregexp::LoadCurrentCharacter(int cp_offset,
                                                        Label* on_failure) {
  assembler_->LoadCurrentChar(cp_offset, on_failure);
}


void RegExpMacroAssemblerIrregexp::CheckCharacterLT(uc16 limit,
                                                    Label* on_less) {
  assembler_->CheckCharacterLT(limit, on_less);
}


void RegExpMacroAssemblerIrregexp::CheckCharacterGT(uc16 limit,
                                                    Label* on_greater) {
  assembler_->CheckCharacterGT(limit, on_greater);
}


void RegExpMacroAssemblerIrregexp::CheckCharacter(uc16 c, Label* on_equal) {
  assembler_->CheckCharacter(c, on_equal);
}


void RegExpMacroAssemblerIrregexp::CheckNotCharacter(uc16 c,
                                                     Label* on_not_equal) {
  assembler_->CheckNotCharacter(c, on_not_equal);
}


void RegExpMacroAssemblerIrregexp::CheckNotCharacterAfterOr(uc16 c,
                                                        uc16 mask,
                                                        Label* on_not_equal) {
  assembler_->OrThenCheckNotCharacter(c, mask, on_not_equal);
}


void RegExpMacroAssemblerIrregexp::CheckNotCharacterAfterMinusOr(
    uc16 c,
    uc16 mask,
    Label* on_not_equal) {
  assembler_->MinusOrThenCheckNotCharacter(c, mask, on_not_equal);
}


void RegExpMacroAssemblerIrregexp::CheckNotBackReference(int start_reg,
                                                     Label* on_not_equal) {
  assembler_->CheckNotBackReference(start_reg, on_not_equal);
}


void RegExpMacroAssemblerIrregexp::CheckNotBackReferenceIgnoreCase(
    int start_reg,
    Label* on_not_equal) {
  assembler_->CheckNotBackReferenceNoCase(start_reg, on_not_equal);
}


void RegExpMacroAssemblerIrregexp::CheckNotRegistersEqual(int reg1,
                                                          int reg2,
                                                          Label* on_not_equal) {
  assembler_->CheckNotRegistersEqual(reg1, reg2, on_not_equal);
}


void RegExpMacroAssemblerIrregexp::CheckBitmap(uc16 start,
                                           Label* bitmap,
                                           Label* on_zero) {
  assembler_->LookupMap1(start, bitmap, on_zero);
}


void RegExpMacroAssemblerIrregexp::DispatchHalfNibbleMap(
    uc16 start,
    Label* half_nibble_map,
    const Vector<Label*>& table) {
  assembler_->LookupMap2(start, half_nibble_map, table);
}


void RegExpMacroAssemblerIrregexp::DispatchByteMap(
    uc16 start,
    Label* byte_map,
    const Vector<Label*>& table) {
  assembler_->LookupMap8(start, byte_map, table);
}


void RegExpMacroAssemblerIrregexp::DispatchHighByteMap(
    byte start,
    Label* byte_map,
    const Vector<Label*>& table) {
  assembler_->LookupHighMap8(start, byte_map, table);
}


void RegExpMacroAssemblerIrregexp::CheckCharacters(
  Vector<const uc16> str,
  int cp_offset,
  Label* on_failure) {
  for (int i = str.length() - 1; i >= 0; i--) {
    assembler_->LoadCurrentChar(cp_offset + i, on_failure);
    assembler_->CheckNotCharacter(str[i], on_failure);
  }
}


void RegExpMacroAssemblerIrregexp::IfRegisterLT(int register_index,
                                                int comparand,
                                                Label* if_less_than) {
  ASSERT(comparand >= 0 && comparand <= 65535);
  assembler_->CheckRegisterLT(register_index, comparand, if_less_than);
}


void RegExpMacroAssemblerIrregexp::IfRegisterGE(int register_index,
                                                int comparand,
                                                Label* if_greater_or_equal) {
  ASSERT(comparand >= 0 && comparand <= 65535);
  assembler_->CheckRegisterGE(register_index, comparand, if_greater_or_equal);
}


Handle<Object> RegExpMacroAssemblerIrregexp::GetCode() {
  Handle<ByteArray> array = Factory::NewByteArray(assembler_->length());
  assembler_->Copy(array->GetDataStartAddress());
  return array;
}

} }  // namespace v8::internal
