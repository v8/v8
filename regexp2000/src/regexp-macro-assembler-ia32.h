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

#ifndef REGEXP_MACRO_ASSEMBLER_IA32_H_
#define REGEXP_MACRO_ASSEMBLER_IA32_H_

#include "regexp-macro-assembler.h"
#include "macro-assembler-ia32.h"

namespace v8 { namespace internal {

template <typename SubjectChar>
class RegExpMacroAssemblerIA32: public RegExpMacroAssembler<SubjectChar> {
 public:
  RegExpMacroAssemblerIA32() { }
  virtual ~RegExpMacroAssembler();
  void Initialize(int num_registers, bool ignore_case);
  virtual void AdvanceCurrentPosition(int by);  // Signed cp change.
  virtual void AdvanceRegister(int reg, int by);  // r[reg] += by.
  virtual void Backtrack();
  virtual void Bind(Label* label);

  // Check the current character against a bitmap.  The range of the current
  // character must be from start to start + length_of_bitmap_in_bits.
  // Where to go if the bit is 0.  Fall through on 1.
  virtual void CheckBitmap(
      uc16 start,           // The bitmap is indexed from this character.
      Label* bitmap,        // Where the bitmap is emitted.
      Label* on_zero);


  // Looks at the next character from the subject and if it doesn't match
  // then goto the on_failure label.  End of input never matches.  If the
  // label is NULL then we should pop a backtrack address off the stack and
  // go to that.
  virtual void CheckCharacterClass(
      RegExpCharacterClass* cclass,
      Label* on_failure);

  // Check the current character for a match with a literal string.  If we
  // fail to match then goto the on_failure label.  End of input always
  // matches.  If the label is NULL then we should pop a backtrack address off
  // the stack and go to that.
  virtual void CheckCharacters(
      Vector<uc16> str,
      Label* on_failure);

  // Check the current input position against a register.  If the register is
  // equal to the current position then go to the label.  If the label is NULL
  // then backtrack instead.
  virtual void CheckCurrentPosition(
      int register_index,
      Label* on_equal);

  // Dispatch after looking the current character up in a byte map.  The
  // destinations vector has up to 256 labels.
  virtual void DispatchByteMap(
      uc16 start,
      Label* byte_map,
      Vector<Label*>& destinations);

  // Dispatch after looking the current character up in a 2-bits-per-entry
  // map.  The destinations vector has up to 4 labels.
  virtual void DispatchHalfNibbleMap(
      uc16 start,
      Label* half_nibble_map,
      Vector<Label*>& destinations);

  // Dispatch after looking the high byte of the current character up in a byte
  // map.  The destinations vector has up to 256 labels.
  virtual void DispatchHighByteMap(
      byte start,
      Label* byte_map,
      Vector<Label*>& destinations);

  virtual void EmitOrLink(Label* label);

  virtual void Fail();

  virtual Handle<Object> GetCode();

  virtual void GoTo(Label* label);

  // Check whether a register is >= a given constant and go to a label if it
  // is.  Backtracks instead if the label is NULL.
  virtual void IfRegisterGE(int reg, int comparand, Label* if_ge);

  // Check whether a register is < a given constant and go to a label if it is.
  // Backtracks instead if the label is NULL.
  virtual void IfRegisterLT(int reg, int comparand, Label* if_lt);

  virtual Re2kImplementation Implementation();

  virtual void PopCurrentPosition();
  virtual void PopRegister(int register_index);
  virtual void PushBacktrack(Label* label);
  virtual void PushCurrentPosition();
  virtual void PushRegister(int register_index);
  virtual void SetRegister(int register_index, int to);
  virtual void Succeed();
  virtual void WriteCurrentPositionToRegister(int reg);
 private:
  Operand register_location(int register_index);
  bool ignore_case();
  // Generate code to perform case-canonicalization on the register.
  void BranchOrBacktrack(Condition condition, Label* to);
  void Canonicalize(Register register);
  void Exit(bool success);
  // Read a character from input at the given offset from the current
  // position.
  void ReadChar(Register destination, int offset);

  template <typename T>
  void LoadConstantBufferAddress(Register reg, ArraySlice<T>* buffer);

  // Read the current character into the destination register.
  void ReadCurrentChar(Register destination);

  static const int kRegExpConstantsSize = 256;
  static const int kMaxInlineStringTests = 8;
  static const uint32_t kEndOfInput = ~0;

  MacroAssembler* masm_;
  ByteArrayProvider constants_;
  int num_registers_;
  bool ignore_case_;
};
}}

#endif /* REGEXP_MACRO_ASSEMBLER_IA32_H_ */
