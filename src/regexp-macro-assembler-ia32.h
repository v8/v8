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

namespace v8 { namespace internal {

class RegExpMacroAssemblerIA32: public RegExpMacroAssembler {
 public:
  // Type of input string to generate code for.
  enum Mode {ASCII = 1, UC16 = 2};

  RegExpMacroAssemblerIA32(Mode mode, int registers_to_save);
  virtual ~RegExpMacroAssemblerIA32();
  virtual void AdvanceCurrentPosition(int by);
  virtual void AdvanceRegister(int reg, int by);
  virtual void Backtrack();
  virtual void Bind(Label* label);
  virtual void CheckBitmap(uc16 start, Label* bitmap, Label* on_zero);
  virtual void CheckCharacter(uc16 c, Label* on_equal);
  virtual void CheckCharacterGT(uc16 limit, Label* on_greater);
  virtual void CheckCharacterLT(uc16 limit, Label* on_less);
  virtual void CheckCharacters(Vector<const uc16> str,
                               int cp_offset,
                               Label* on_failure);
  virtual void CheckCurrentPosition(int register_index, Label* on_equal);
  virtual void CheckNotBackReference(int start_reg, Label* on_no_match);
  virtual void CheckNotBackReferenceIgnoreCase(int start_reg,
                                               Label* on_no_match);
  virtual void CheckNotCharacter(uc16 c, Label* on_not_equal);
  virtual void CheckNotCharacterAfterOr(uc16 c, uc16 mask, Label* on_not_equal);
  virtual void CheckNotCharacterAfterMinusOr(uc16 c,
                                             uc16 mask,
                                             Label* on_not_equal);
  virtual void DispatchByteMap(uc16 start,
                               Label* byte_map,
                               const Vector<Label*>& destinations);
  virtual void DispatchHalfNibbleMap(uc16 start,
                                     Label* half_nibble_map,
                                     const Vector<Label*>& destinations);
  virtual void DispatchHighByteMap(byte start,
                                   Label* byte_map,
                                   const Vector<Label*>& destinations);
  virtual void EmitOrLink(Label* label);
  virtual void Fail();
  virtual Handle<Object> GetCode();
  virtual void GoTo(Label* label);
  virtual void IfRegisterGE(int reg, int comparand, Label* if_ge);
  virtual void IfRegisterLT(int reg, int comparand, Label* if_lt);
  virtual IrregexpImplementation Implementation();
  virtual void LoadCurrentCharacter(int cp_offset, Label* on_end_of_input);
  virtual void PopCurrentPosition();
  virtual void PopRegister(int register_index);
  virtual void PushBacktrack(Label* label);
  virtual void PushCurrentPosition();
  virtual void PushRegister(int register_index);
  virtual void ReadCurrentPositionFromRegister(int reg);
  virtual void ReadStackPointerFromRegister(int reg);
  virtual void SetRegister(int register_index, int to);
  virtual void Succeed();
  virtual void WriteCurrentPositionToRegister(int reg);
  virtual void WriteStackPointerToRegister(int reg);

 private:
  // Offsets from ebp of arguments to function.
  static const int kBackup_ebx = sizeof(uint32_t);
  static const int kBackup_edi = kBackup_ebx + sizeof(uint32_t);
  static const int kBackup_esi = kBackup_edi + sizeof(uint32_t);
  static const int kReturn_eip = kBackup_esi + sizeof(uint32_t);
  static const int kInputBuffer = kReturn_eip + sizeof(uint32_t);
  static const int kInputStartOffset = kInputBuffer + sizeof(uint32_t);
  static const int kInputEndOffset = kInputStartOffset + sizeof(uint32_t);
  static const int kRegisterOutput = kInputEndOffset + sizeof(uint32_t);

  // Initial size of code buffer.
  static const size_t kRegExpCodeSize = 1024;
  // Initial size of constant buffers allocated during compilation.
  static const int kRegExpConstantsSize = 256;
  // Only unroll loops up to this length.
  static const int kMaxInlineStringTests = 8;
  // Special "character" marking end of input.
  static const uint32_t kEndOfInput = ~0;

  // The ebp-relative location of a regexp register.
  Operand register_location(int register_index);

  // Byte size of chars in the string to match (decided by the Mode argument)
  size_t char_size();

  // Records that a register is used. At the end, we need the number of
  // registers used.
  void RecordRegister(int register_index);

  // Equivalent to a conditional branch to the label, unless the label
  // is NULL, in which case it is a conditional Backtrack.
  void BranchOrBacktrack(Condition condition, Label* to);

  // Generate code to perform case-canonicalization on the register.
  void Canonicalize(Register register);

  // Read a character from input at the given offset from the current
  // position.
  void ReadChar(Register destination, int offset);

  // Load the address of a "constant buffer" (a slice of a byte array)
  // into a register. The address is computed from the ByteArray* address
  // and an offset. Uses no extra registers.
  void LoadConstantBufferAddress(Register reg, ArraySlice* buffer);

  // Read the current character into the destination register.
  void ReadCurrentChar(Register destination);

  // Adds code that checks whether preemption has been requested
  // (and checks if we have hit the stack limit too).
  void CheckStackLimit();

  MacroAssembler* masm_;
  // Constant buffer provider. Allocates external storage for storing
  // constants.
  ByteArrayProvider constants_;
  // Which mode to generate code for (ASCII or UTF16).
  Mode mode_;
  // One greater than maximal register index actually used.
  int num_registers_;
  // Number of registers to output at the end (the saved registers
  // are always 0..num_saved_registers_-1)
  int num_saved_registers_;
  // Labels used internally.
  Label entry_label_;
  Label start_label_;
  Label success_label_;
  Label exit_label_;
  // Handle used to represent the generated code object itself.
  Handle<Object> self_;
};

}}  // namespace v8::internal

#endif /* REGEXP_MACRO_ASSEMBLER_IA32_H_ */
