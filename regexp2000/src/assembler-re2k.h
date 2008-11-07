// Copyright 2006-2008 the V8 project authors. All rights reserved.

// A light-weight assembler for the Regexp2000 byte code.

#ifndef V8_ASSEMBLER_RE2K_H_
#define V8_ASSEMBLER_RE2K_H_

namespace v8 { namespace internal {


class Re2kAssembler {
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
  explicit Re2kAssembler(Vector<byte>);
  ~Re2kAssembler();

  // CP = current position in source.
  // BT = backtrack label.

  // Stack.
  void PushCurrentPosition(int cp_offset = 0);
  void PushBacktrack(Label* l);
  void PushRegister(int index);
  void SetRegisterToCurrentPosition(int index, int cp_offset = 0);
  void SetRegister(int index, int value);

  void PopCurrentPosition();
  void PopBacktrack();
  void PopRegister(int index);

  void Fail();
  void FailIfWithin(int distance_from_end);
  void Succeed();

  void Break();  // This instruction will cause a fatal VM error if hit.

  void Bind(Label* l);  // binds an unbound label L to the current code position

  void AdvanceCP(int cp_offset = 1);

  void GoTo(Label* l);

  // Loads current char into a register.
  void LoadCurrentChar(int cp_offset = 0);

  // Checks current char register against a singleton.
  void CheckChar(uc16 c, Label* on_mismatch);
  void CheckNotChar(uc16 c, Label* on_match);

  // Checks current char register against the magic end-of-input symbol.
  void CheckEnd(Label* on_not_end);
  void CheckNotEnd(Label* on_end);

  // Checks current char register against a range.
  void CheckRange(uc16 start, uc16 end, Label* on_mismatch);
  void CheckNotRange(uc16 start, uc16 end, Label* on_match);

  // Checks that the current char is in the range and that the corresponding bit
  // is set in the bitmap.
  void CheckBitmap(uc16 start, uc16 end, const byte* bits, Label* on_mismatch);
  void CheckNotBitmap(uc16 start, uc16 end, const byte* bits, Label* on_match);

  // Checks current position (plus optional offset) for a match against a
  // previous capture.  Advances current position by the length of the capture
  // iff it matches.  The capture is stored in a given register and the
  // the register after.
  void CheckBackref(int capture_index, Label* on_mismatch, int cp_offset = 0);
  void CheckNotBackref(int capture_index, Label* on_match, int cp_offset = 0);

  // Checks a register for equal, less than or equal, less than, greater than
  // or equal, greater than, not equal.
  void CheckRegisterLt(int reg_index, uint16_t vs, Label* on_less_than);
  void CheckRegisterGe(int reg_index, uint16_t vs, Label* on_greater_equal);

  // Code and bitmap emission.
  inline void Emit32(uint32_t x);
  inline void Emit16(uint32_t x);
  inline void Emit(uint32_t x);

  // Bytecode buffer.
  int length();
  void Copy(Address a);

 private:
  // Don't use this.
  Re2kAssembler() { UNREACHABLE(); }
  // The buffer into which code and relocation info are generated.
  Vector<byte> buffer_;

  inline void CheckRegister(int byte_code,
                            int reg_index,
                            uint16_t vs,
                            Label* on_true);
  // Code generation.
  int pc_;  // The program counter; moves forward.

  // True if the assembler owns the buffer, false if buffer is external.
  bool own_buffer_;

  inline void EmitOrLink(Label* l);
};


} }  // namespace v8::internal

#endif  // V8_ASSEMBLER_RE2K_H_
