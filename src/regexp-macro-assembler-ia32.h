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
  enum Mode { ASCII = 1, UC16 = 2 };
  enum Result { EXCEPTION = -1, FAILURE = 0, SUCCESS = 1 };

  RegExpMacroAssemblerIA32(Mode mode, int registers_to_save);
  virtual ~RegExpMacroAssemblerIA32();
  virtual void AdvanceCurrentPosition(int by);
  virtual void AdvanceRegister(int reg, int by);
  virtual void Backtrack();
  virtual void Bind(Label* label);
  virtual void CheckBitmap(uc16 start, Label* bitmap, Label* on_zero);
  virtual void CheckCharacter(uint32_t c, Label* on_equal);
  virtual void CheckCharacterAfterAnd(uint32_t c,
                                      uint32_t mask,
                                      Label* on_equal);
  virtual void CheckCharacterGT(uc16 limit, Label* on_greater);
  virtual void CheckCharacterLT(uc16 limit, Label* on_less);
  virtual void CheckCharacters(Vector<const uc16> str,
                               int cp_offset,
                               Label* on_failure,
                               bool check_end_of_string);
  virtual void CheckGreedyLoop(Label* on_tos_equals_current_position);
  virtual void CheckNotAtStart(Label* on_not_at_start);
  virtual void CheckNotBackReference(int start_reg, Label* on_no_match);
  virtual void CheckNotBackReferenceIgnoreCase(int start_reg,
                                               Label* on_no_match);
  virtual void CheckNotRegistersEqual(int reg1, int reg2, Label* on_not_equal);
  virtual void CheckNotCharacter(uint32_t c, Label* on_not_equal);
  virtual void CheckNotCharacterAfterAnd(uint32_t c,
                                         uint32_t mask,
                                         Label* on_not_equal);
  virtual void CheckNotCharacterAfterMinusAnd(uc16 c,
                                              uc16 minus,
                                              uc16 mask,
                                              Label* on_not_equal);
  virtual bool CheckSpecialCharacterClass(uc16 type,
                                          int cp_offset,
                                          bool check_offset,
                                          Label* on_no_match);
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
  virtual Handle<Object> GetCode(Handle<String> source);
  virtual void GoTo(Label* label);
  virtual void IfRegisterGE(int reg, int comparand, Label* if_ge);
  virtual void IfRegisterLT(int reg, int comparand, Label* if_lt);
  virtual IrregexpImplementation Implementation();
  virtual void LoadCurrentCharacter(int cp_offset,
                                    Label* on_end_of_input,
                                    bool check_bounds = true,
                                    int characters = 1);
  virtual void PopCurrentPosition();
  virtual void PopRegister(int register_index);
  virtual void PushBacktrack(Label* label);
  virtual void PushCurrentPosition();
  virtual void PushRegister(int register_index);
  virtual void ReadCurrentPositionFromRegister(int reg);
  virtual void ReadStackPointerFromRegister(int reg);
  virtual void SetRegister(int register_index, int to);
  virtual void Succeed();
  virtual void WriteCurrentPositionToRegister(int reg, int cp_offset);
  virtual void WriteStackPointerToRegister(int reg);

  template <typename T>
  static inline Result Execute(Code* code,
                               T** input,
                               int start_offset,
                               int end_offset,
                               int* output,
                               bool at_start) {
    typedef int (*matcher)(T**, int, int, int*, int);
    matcher matcher_func = FUNCTION_CAST<matcher>(code->entry());
    int at_start_val = at_start ? 1 : 0;
    int result = matcher_func(input,
                              start_offset,
                              end_offset,
                              output,
                              at_start_val);
    return (result < 0) ? EXCEPTION : (result ? SUCCESS : FAILURE);
  }

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
  static const int kAtStart = kRegisterOutput + sizeof(uint32_t);

  // Initial size of code buffer.
  static const size_t kRegExpCodeSize = 1024;
  // Initial size of constant buffers allocated during compilation.
  static const int kRegExpConstantsSize = 256;
  // Only unroll loops up to this length. TODO(lrn): Actually use this.
  static const int kMaxInlineStringTests = 32;

  // Compares two-byte strings case insensitively.
  static int CaseInsensitiveCompareUC16(uc16** buffer,
                                        int byte_offset1,
                                        int byte_offset2,
                                        size_t byte_length);

  void LoadCurrentCharacterUnchecked(int cp_offset, int characters);

  // Adds code that checks whether preemption has been requested
  // (and checks if we have hit the stack limit too).
  void CheckStackLimit();

  // Called from RegExp if the stack-guard is triggered.
  // If the code object is relocated, the return address is fixed before
  // returning.
  static int CheckStackGuardState(Address return_address, Code* re_code);

  // Checks whether the given offset from the current position is before
  // the end of the string.
  void CheckPosition(int cp_offset, Label* on_outside_input);

  // The ebp-relative location of a regexp register.
  Operand register_location(int register_index);

  // The register containing the current character after LoadCurrentCharacter.
  Register current_character();

  // Byte size of chars in the string to match (decided by the Mode argument)
  size_t char_size();

  // Equivalent to a conditional branch to the label, unless the label
  // is NULL, in which case it is a conditional Backtrack.
  void BranchOrBacktrack(Condition condition, Label* to);

  // Load the address of a "constant buffer" (a slice of a byte array)
  // into a register. The address is computed from the ByteArray* address
  // and an offset. Uses no extra registers.
  void LoadConstantBufferAddress(Register reg, ArraySlice* buffer);

  // Call and return internally in the generated code in a way that
  // is GC-safe (i.e., doesn't leave absolute code addresses on the stack)
  void SafeCall(Label* to);
  void SafeReturn();

  // Before calling a C-function from generated code, align arguments on stack.
  // After aligning the frame, arguments must be stored in esp[0], esp[4],
  // etc., not pushed. The argument count assumes all arguments are word sized.
  void FrameAlign(int num_arguments);
  // Calls a C function and cleans up the space for arguments allocated
  // by FrameAlign. The called function is not allowed to trigger a garbage
  // collection, since that might move the code and invalidate the return
  // address
  void CallCFunction(Address function_address, int num_arguments);

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
  Label backtrack_label_;
  Label exit_label_;
  Label check_preempt_label_;
  // Handle used to represent the generated code object itself.
  Handle<Object> self_;
};

}}  // namespace v8::internal

#endif /* REGEXP_MACRO_ASSEMBLER_IA32_H_ */
