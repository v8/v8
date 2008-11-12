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
  assembler_->SetRegisterToCurrentPosition(register_index);
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


static void TwoWayCharacterClass(
    Re2kAssembler* assembler,
    RegExpCharacterClass* char_class,
    Label* on_match,
    Label* on_mismatch) {
  ZoneList<CharacterRange>* ranges = char_class->ranges();
  int range_count = ranges->length();
  if (!char_class->is_negated()) {
    for (int i = 0; i < range_count; i++) {
      CharacterRange& range = ranges->at(i);
      assembler->CheckRange(range.from(), range.to(), on_match);
    }
    if (on_mismatch == NULL) {
      assembler->PopBacktrack();
    } else {
      assembler->GoTo(on_mismatch);
    }
  } else {  // range is negated.
    if (range_count == 0) {
      assembler->GoTo(on_match);
    } else {
      CharacterRange& previous = ranges->at(0);
      if (previous.from() > 0) {
        assembler->CheckRange(0, previous.from() - 1, on_match);
      }
      for (int i = 1; i < range_count; i++) {
        CharacterRange& range = ranges->at(i);
        if (previous.to() < range.from() - 1) {
          assembler->CheckRange(previous.to() + 1, range.from() - 1, on_match);
        }
        previous = range;
      }
      if (previous.to() < 65535) {
        assembler->CheckRange(previous.to() + 1, 65535, on_match);
      }
    }
  }
}


void RegExpMacroAssemblerRe2k::CheckCurrentPosition(
  int register_index,
  Label* on_equal) {
  // TODO(erikcorry): Implement.
  UNREACHABLE();
}


void RegExpMacroAssemblerRe2k::CheckCharacterClass(
    RegExpCharacterClass* char_class,
    int cp_offset,
    Label* on_failure) {
  assembler_->LoadCurrentChar(cp_offset, on_failure);
  if (!char_class->is_negated() &&
      char_class->ranges()->length() == 1 &&
      on_failure != NULL) {
    // This is the simple case where the char class has one range and we want to
    // fall through if it matches.
    CharacterRange& range = char_class->ranges()->at(0);
    assembler_->CheckNotRange(range.from(), range.to(), on_failure);
  } else {
    Label on_success;
    TwoWayCharacterClass(assembler_, char_class, &on_success, on_failure);
    assembler_->Bind(&on_success);
  }
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
    assembler_->CheckChar(str[i], on_failure);
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
