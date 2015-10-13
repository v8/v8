// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODES_H_
#define V8_INTERPRETER_BYTECODES_H_

#include <iosfwd>

// Clients of this interface shouldn't depend on lots of interpreter internals.
// Do not include anything from src/interpreter here!
#include "src/utils.h"

namespace v8 {
namespace internal {
namespace interpreter {

// The list of operand types used by bytecodes.
#define OPERAND_TYPE_LIST(V)    \
                                \
  /* None operand. */           \
  V(None, OperandSize::kNone)   \
                                \
  /* Byte operands. */          \
  V(Count8, OperandSize::kByte) \
  V(Imm8, OperandSize::kByte)   \
  V(Idx8, OperandSize::kByte)   \
  V(Reg8, OperandSize::kByte)   \
                                \
  /* Short operands. */         \
  V(Idx16, OperandSize::kShort)

// The list of bytecodes which are interpreted by the interpreter.
#define BYTECODE_LIST(V)                                                       \
                                                                               \
  /* Loading the accumulator */                                                \
  V(LdaZero, OperandType::kNone)                                               \
  V(LdaSmi8, OperandType::kImm8)                                               \
  V(LdaConstant, OperandType::kIdx8)                                           \
  V(LdaUndefined, OperandType::kNone)                                          \
  V(LdaNull, OperandType::kNone)                                               \
  V(LdaTheHole, OperandType::kNone)                                            \
  V(LdaTrue, OperandType::kNone)                                               \
  V(LdaFalse, OperandType::kNone)                                              \
                                                                               \
  /* Globals */                                                                \
  V(LdaGlobal, OperandType::kIdx8)                                             \
  V(StaGlobal, OperandType::kIdx8)                                             \
                                                                               \
  /* Context operations */                                                     \
  V(LdaContextSlot, OperandType::kReg8, OperandType::kIdx8)                    \
                                                                               \
  /* Register-accumulator transfers */                                         \
  V(Ldar, OperandType::kReg8)                                                  \
  V(Star, OperandType::kReg8)                                                  \
                                                                               \
  /* LoadIC operations */                                                      \
  V(LoadICSloppy, OperandType::kReg8, OperandType::kIdx8)                      \
  V(LoadICStrict, OperandType::kReg8, OperandType::kIdx8)                      \
  V(KeyedLoadICSloppy, OperandType::kReg8, OperandType::kIdx8)                 \
  V(KeyedLoadICStrict, OperandType::kReg8, OperandType::kIdx8)                 \
                                                                               \
  /* StoreIC operations */                                                     \
  V(StoreICSloppy, OperandType::kReg8, OperandType::kReg8, OperandType::kIdx8) \
  V(StoreICStrict, OperandType::kReg8, OperandType::kReg8, OperandType::kIdx8) \
  V(KeyedStoreICSloppy, OperandType::kReg8, OperandType::kReg8,                \
    OperandType::kIdx8)                                                        \
  V(KeyedStoreICStrict, OperandType::kReg8, OperandType::kReg8,                \
    OperandType::kIdx8)                                                        \
                                                                               \
  /* Binary Operators */                                                       \
  V(Add, OperandType::kReg8)                                                   \
  V(Sub, OperandType::kReg8)                                                   \
  V(Mul, OperandType::kReg8)                                                   \
  V(Div, OperandType::kReg8)                                                   \
  V(Mod, OperandType::kReg8)                                                   \
  V(BitwiseOr, OperandType::kReg8)                                             \
  V(BitwiseXor, OperandType::kReg8)                                            \
  V(BitwiseAnd, OperandType::kReg8)                                            \
  V(ShiftLeft, OperandType::kReg8)                                             \
  V(ShiftRight, OperandType::kReg8)                                            \
  V(ShiftRightLogical, OperandType::kReg8)                                     \
                                                                               \
  /* Unary Operators */                                                        \
  V(LogicalNot, OperandType::kNone)                                            \
  V(TypeOf, OperandType::kNone)                                                \
                                                                               \
  /* Call operations. */                                                       \
  V(Call, OperandType::kReg8, OperandType::kReg8, OperandType::kCount8)        \
  V(CallRuntime, OperandType::kIdx16, OperandType::kReg8,                      \
    OperandType::kCount8)                                                      \
                                                                               \
  /* Test Operators */                                                         \
  V(TestEqual, OperandType::kReg8)                                             \
  V(TestNotEqual, OperandType::kReg8)                                          \
  V(TestEqualStrict, OperandType::kReg8)                                       \
  V(TestNotEqualStrict, OperandType::kReg8)                                    \
  V(TestLessThan, OperandType::kReg8)                                          \
  V(TestGreaterThan, OperandType::kReg8)                                       \
  V(TestLessThanOrEqual, OperandType::kReg8)                                   \
  V(TestGreaterThanOrEqual, OperandType::kReg8)                                \
  V(TestInstanceOf, OperandType::kReg8)                                        \
  V(TestIn, OperandType::kReg8)                                                \
                                                                               \
  /* Cast operators */                                                         \
  V(ToBoolean, OperandType::kNone)                                             \
                                                                               \
  /* Closure allocation */                                                     \
  V(CreateClosure, OperandType::kImm8)                                         \
                                                                               \
  /* Control Flow */                                                           \
  V(Jump, OperandType::kImm8)                                                  \
  V(JumpConstant, OperandType::kIdx8)                                          \
  V(JumpIfTrue, OperandType::kImm8)                                            \
  V(JumpIfTrueConstant, OperandType::kIdx8)                                    \
  V(JumpIfFalse, OperandType::kImm8)                                           \
  V(JumpIfFalseConstant, OperandType::kIdx8)                                   \
  V(Return, OperandType::kNone)


// Enumeration of the size classes of operand types used by bytecodes.
enum class OperandSize : uint8_t {
  kNone = 0,
  kByte = 1,
  kShort = 2,
};


// Enumeration of operand types used by bytecodes.
enum class OperandType : uint8_t {
#define DECLARE_OPERAND_TYPE(Name, _) k##Name,
  OPERAND_TYPE_LIST(DECLARE_OPERAND_TYPE)
#undef DECLARE_OPERAND_TYPE
#define COUNT_OPERAND_TYPES(x, _) +1
  // The COUNT_OPERAND macro will turn this into kLast = -1 +1 +1... which will
  // evaluate to the same value as the last operand.
  kLast = -1 OPERAND_TYPE_LIST(COUNT_OPERAND_TYPES)
#undef COUNT_OPERAND_TYPES
};


// Enumeration of interpreter bytecodes.
enum class Bytecode : uint8_t {
#define DECLARE_BYTECODE(Name, ...) k##Name,
  BYTECODE_LIST(DECLARE_BYTECODE)
#undef DECLARE_BYTECODE
#define COUNT_BYTECODE(x, ...) +1
  // The COUNT_BYTECODE macro will turn this into kLast = -1 +1 +1... which will
  // evaluate to the same value as the last real bytecode.
  kLast = -1 BYTECODE_LIST(COUNT_BYTECODE)
#undef COUNT_BYTECODE
};


// An interpreter Register which is located in the function's Register file
// in its stack-frame. Register hold parameters, this, and expression values.
class Register {
 public:
  static const int kMaxRegisterIndex = 127;
  static const int kMinRegisterIndex = -128;

  Register() : index_(kIllegalIndex) {}

  explicit Register(int index) : index_(index) {
    DCHECK_LE(index_, kMaxRegisterIndex);
    DCHECK_GE(index_, kMinRegisterIndex);
  }

  int index() const {
    DCHECK(index_ != kIllegalIndex);
    return index_;
  }
  bool is_parameter() const { return index() < 0; }

  static Register FromParameterIndex(int index, int parameter_count);
  int ToParameterIndex(int parameter_count) const;
  static int MaxParameterIndex();

  // Returns the register for the function's outer context.
  static Register function_context();
  bool is_function_context() const;

  static Register FromOperand(uint8_t operand);
  uint8_t ToOperand() const;

 private:
  static const int kIllegalIndex = kMaxInt;

  void* operator new(size_t size);
  void operator delete(void* p);

  int index_;
};


class Bytecodes {
 public:
  // Returns string representation of |bytecode|.
  static const char* ToString(Bytecode bytecode);

  // Returns string representation of |operand_type|.
  static const char* OperandTypeToString(OperandType operand_type);

  // Returns string representation of |operand_size|.
  static const char* OperandSizeToString(OperandSize operand_size);

  // Returns byte value of bytecode.
  static uint8_t ToByte(Bytecode bytecode);

  // Returns bytecode for |value|.
  static Bytecode FromByte(uint8_t value);

  // Returns the number of operands expected by |bytecode|.
  static int NumberOfOperands(Bytecode bytecode);

  // Return the i-th operand of |bytecode|.
  static OperandType GetOperandType(Bytecode bytecode, int i);

  // Return the size of the i-th operand of |bytecode|.
  static OperandSize GetOperandSize(Bytecode bytecode, int i);

  // Returns the offset of the i-th operand of |bytecode| relative to the start
  // of the bytecode.
  static int GetOperandOffset(Bytecode bytecode, int i);

  // Returns the size of the bytecode including its operands.
  static int Size(Bytecode bytecode);

  // Returns the size of |operand|.
  static OperandSize SizeOfOperand(OperandType operand);

  // Return true if the bytecode is a jump or a conditional jump taking
  // an immediate byte operand (OperandType::kImm8).
  static bool IsJump(Bytecode bytecode);

  // Return true if the bytecode is a jump or conditional jump taking a
  // constant pool entry (OperandType::kIdx).
  static bool IsJumpConstant(Bytecode bytecode);

  // Converts bytes[0] and bytes[1] to a 16 bit 'short' operand value.
  static uint16_t ShortOperandFromBytes(const uint8_t* bytes);

  // Converts 16 bit 'short' |operand| into bytes_out[0] and bytes_out[1].
  static void ShortOperandToBytes(uint16_t operand, uint8_t* bytes_out);

  // Decode a single bytecode and operands to |os|.
  static std::ostream& Decode(std::ostream& os, const uint8_t* bytecode_start,
                              int number_of_parameters);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Bytecodes);
};

std::ostream& operator<<(std::ostream& os, const Bytecode& bytecode);
std::ostream& operator<<(std::ostream& os, const OperandType& operand_type);
std::ostream& operator<<(std::ostream& os, const OperandSize& operand_type);

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODES_H_
