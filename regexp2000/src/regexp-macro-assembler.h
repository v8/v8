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

#ifndef V8_REGEXP_MACRO_ASSEMBLER_H_
#define V8_REGEXP_MACRO_ASSEMBLER_H_

namespace v8 { namespace internal {


struct DisjunctDecisionRow {
  RegExpCharacterClass cc;
  Label* on_match;
};


class RegExpMacroAssembler {
 public:
  RegExpMacroAssembler();
  virtual ~RegExpMacroAssembler();
  virtual void Bind(Label* label) = 0;
  virtual void EmitOrLink(Label* label) = 0;
  virtual void AdvanceCurrentPosition(int by) = 0;  // Signed cp change.
  virtual void PopCurrentPosition() = 0;
  virtual void PushCurrentPosition() = 0;
  virtual void Backtrack() = 0;
  virtual void GoTo(Label* label) = 0;
  virtual void PushBacktrack(Label* label) = 0;
  virtual void Succeed() = 0;
  virtual void Fail() = 0;
  virtual void PopRegister(int register_index) = 0;
  virtual void PushRegister(int register_index) = 0;
  virtual void AdvanceRegister(int reg, int by) = 0;  // r[reg] += by.
  virtual void SetRegister(int register_index, int to) = 0;
  virtual void WriteCurrentPositionToRegister(int reg) = 0;
  virtual void ReadCurrentPositionFromRegister(int reg) = 0;
  virtual void WriteStackPointerToRegister(int reg) = 0;
  virtual void ReadStackPointerFromRegister(int reg) = 0;
  virtual void LoadCurrentCharacter(int cp_offset, Label* on_end_of_input) = 0;
  virtual void CheckCharacterLT(uc16 limit, Label* on_less) = 0;
  virtual void CheckCharacterGT(uc16 limit, Label* on_greater) = 0;
  virtual void CheckCharacter(uc16 c, Label* on_equal) = 0;
  virtual void CheckNotCharacter(uc16 c, Label* on_not_equal) = 0;
  // Check the current character for a match with a literal string.  If we
  // fail to match then goto the on_failure label.  End of input always
  // matches.  If the label is NULL then we should pop a backtrack address off
  // the stack abnd go to that.
  virtual void CheckCharacters(
      Vector<const uc16> str,
      int cp_offset,
      Label* on_failure) = 0;
  // Check the current input position against a register.  If the register is
  // equal to the current position then go to the label.  If the label is NULL
  // then backtrack instead.
  virtual void CheckCurrentPosition(
      int register_index,
      Label* on_equal) = 0;
  // Check the current character against a bitmap.  The range of the current
  // character must be from start to start + length_of_bitmap_in_bits.
  virtual void CheckBitmap(
      uc16 start,           // The bitmap is indexed from this character.
      Label* bitmap,        // Where the bitmap is emitted.
      Label* on_zero) = 0;  // Where to go if the bit is 0.  Fall through on 1.
  // Dispatch after looking the current character up in a 2-bits-per-entry
  // map.  The destinations vector has up to 4 labels.
  virtual void DispatchHalfNibbleMap(
      uc16 start,
      Label* half_nibble_map,
      const Vector<Label*>& destinations) = 0;
  // Dispatch after looking the current character up in a byte map.  The
  // destinations vector has up to 256 labels.
  virtual void DispatchByteMap(
      uc16 start,
      Label* byte_map,
      const Vector<Label*>& destinations) = 0;
  // Dispatch after looking the high byte of the current character up in a byte
  // map.  The destinations vector has up to 256 labels.
  virtual void DispatchHighByteMap(
      byte start,
      Label* byte_map,
      const Vector<Label*>& destinations) = 0;
  // Check whether a register is < a given constant and go to a label if it is.
  // Backtracks instead if the label is NULL.
  virtual void IfRegisterLT(int reg, int comparand, Label* if_lt) = 0;
  // Check whether a register is >= a given constant and go to a label if it
  // is.  Backtracks instead if the label is NULL.
  virtual void IfRegisterGE(int reg, int comparand, Label* if_ge) = 0;

  enum Re2kImplementation {
    kIA32Implementation,
    kARMImplementation,
    kBytecodeImplementation};

  virtual Re2kImplementation Implementation() = 0;
  virtual Handle<Object> GetCode() = 0;

 private:
};


template <typename T>
struct ArraySlice {
 public:
  ArraySlice(Handle<ByteArray> array, size_t offset)
    : array_(array), offset_(offset) {}
  Handle<ByteArray> array() { return array_; }
  // Offset in the byte array data.
  size_t offset() { return offset_; }
  // Offset from the ByteArray pointer.
  size_t base_offset() {
    return ByteArray::kHeaderSize - kHeapObjectTag + offset;
  }
  T* operator*() {
    return reinterpret_cast<T*>(array_->GetDataStartAddress() + offset);
  }
  T& operator[](int idx) {
    return reinterpret_cast<T*>(array_->GetDataStartAddress() + offset)[idx];
  }
 private:
  Handle<ByteArray> array_;
  size_t offset_;
};


class ByteArrayProvider {
 public:
  explicit ByteArrayProvider(int initial_size);
  // Provides a place to put "size" elements of type T. The information
  // can be stored in the provided ByteArray at the "offset". The offset is
  // aligned to an "align"-boundary
  template <typename T>
  ArraySlice<T> GetBuffer(int size, int align);
 private:
  const int byte_array_size_;
  Handle<ByteArray> current_byte_array_;
  int current_byte_array_free_offset_;
};

} }  // namespace v8::internal

#endif  // V8_REGEXP_MACRO_ASSEMBLER_H_
