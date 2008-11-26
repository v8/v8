// Copyright 2006-2008 the V8 project authors. All rights reserved.
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

#ifndef V8_ASSEMBLER_IRREGEXP_H_
#define V8_ASSEMBLER_IRREGEXP_H_

namespace v8 { namespace internal {


class IrregexpAssembler {
 public:
  // Create an assembler. Instructions and relocation information are emitted
  // into a buffer, with the instructions starting from the beginning and the
  // relocation information starting from the end of the buffer. See CodeDesc
  // for a detailed comment on the layout (globals.h).
  //
  // If the provided buffer is NULL, the assembler allocates and grows its own
  // buffer, and buffer_size determines the initial buffer size. The buffer is
  // owned by the assembler and deallocated upon destruction of the assembler.
  //
  // If the provided buffer is not NULL, the assembler uses the provided buffer
  // for code generation and assumes its size to be buffer_size. If the buffer
  // is too small, a fatal error occurs. No deallocation of the buffer is done
  // upon destruction of the assembler.
  explicit IrregexpAssembler(Vector<byte>);
  ~IrregexpAssembler();

  // CP = current position in source.
  // BT = backtrack label.

  // Stack.
  void PushCurrentPosition(int cp_offset = 0);
  void PushBacktrack(Label* l);
  void PushRegister(int index);
  void WriteCurrentPositionToRegister(int index, int cp_offset = 0);
  void ReadCurrentPositionFromRegister(int index);
  void WriteStackPointerToRegister(int index);
  void ReadStackPointerFromRegister(int index);
  void SetRegister(int index, int value);
  void AdvanceRegister(int index, int by);

  void PopCurrentPosition();
  void PopBacktrack();
  void PopRegister(int index);

  void Fail();
  void Succeed();

  // This instruction will cause a fatal VM error if hit.
  void Break();

  // Binds an unbound label L to the current code posn.
  void Bind(Label* l);

  void AdvanceCP(int by);

  void GoTo(Label* l);

  // Loads current char into a machine register.  Jumps to the label if we
  // reached the end of the subject string.  Fall through otherwise.
  void LoadCurrentChar(int cp_offset, Label* on_end);

  // Checks current char register against a singleton.
  void CheckCharacter(uc16 c, Label* on_match);
  void CheckNotCharacter(uc16 c, Label* on_mismatch);
  void OrThenCheckNotCharacter(uc16 c, uc16 mask, Label* on_mismatch);
  void MinusOrThenCheckNotCharacter(uc16 c, uc16 mask, Label* on_mismatch);

  // Used to check current char register against a range.
  void CheckCharacterLT(uc16 limit, Label* on_less);
  void CheckCharacterGT(uc16 limit, Label* on_greater);

  // Checks current position for a match against a previous capture.  Advances
  // current position by the length of the capture iff it matches.  The capture
  // is stored in a given register and the register after.  If a register
  // contains -1 then the other register must always contain -1 and the
  // on_mismatch label will never be called.
  void CheckNotBackReference(int capture_index, Label* on_mismatch);
  void CheckNotBackReferenceNoCase(int capture_index, Label* on_mismatch);

  // Checks a register for strictly-less-than or greater-than-or-equal.
  void CheckRegisterLT(int reg_index, uint16_t vs, Label* on_less_than);
  void CheckRegisterGE(int reg_index, uint16_t vs, Label* on_greater_equal);

  // Subtracts a 16 bit value from the current character, uses the result to
  // look up in a bit array, uses the result of that to decide whether to fall
  // though (on 1) or jump to the on_zero label (on 0).
  void LookupMap1(uc16 start, Label* bit_map, Label* on_zero);

  // Subtracts a 16 bit value from the current character, uses the result to
  // look up in a 2-bit array, uses the result of that to look up in a label
  // table and jumps to the label.
  void LookupMap2(uc16 start,
                  Label* half_nibble_map,
                  const Vector<Label*>& table);

  // Subtracts a 16 bit value from the current character, uses the result to
  // look up in a byte array, uses the result of that to look up in a label
  // array and jumps to the label.
  void LookupMap8(uc16 start, Label* byte_map, const Vector<Label*>& table);

  // Takes the high byte of the current character, uses the result to
  // look up in a byte array, uses the result of that to look up in a label
  // array and jumps to the label.
  void LookupHighMap8(byte start, Label* byte_map, const Vector<Label*>& table);

  // Code and bitmap emission.
  inline void Emit32(uint32_t x);
  inline void Emit16(uint32_t x);
  inline void Emit(uint32_t x);

  // Bytecode buffer.
  int length();
  void Copy(Address a);

  inline void EmitOrLink(Label* l);
 private:
  inline void CheckRegister(int byte_code,
                            int reg_index,
                            uint16_t vs,
                            Label* on_true);
  void Expand();

  // The buffer into which code and relocation info are generated.
  Vector<byte> buffer_;
  // The program counter.
  int pc_;
  // True if the assembler owns the buffer, false if buffer is external.
  bool own_buffer_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(IrregexpAssembler);
};


} }  // namespace v8::internal

#endif  // V8_ASSEMBLER_IRREGEXP_H_
