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

#define INVALID_OPERAND_TYPE_LIST(V) V(None, OperandTypeInfo::kNone)

#define REGISTER_INPUT_OPERAND_TYPE_LIST(V)         \
  V(MaybeReg, OperandTypeInfo::kScalableSignedByte) \
  V(Reg, OperandTypeInfo::kScalableSignedByte)      \
  V(RegPair, OperandTypeInfo::kScalableSignedByte)

#define REGISTER_OUTPUT_OPERAND_TYPE_LIST(V)          \
  V(RegOut, OperandTypeInfo::kScalableSignedByte)     \
  V(RegOutPair, OperandTypeInfo::kScalableSignedByte) \
  V(RegOutTriple, OperandTypeInfo::kScalableSignedByte)

#define SCALAR_OPERAND_TYPE_LIST(V)                   \
  V(Flag8, OperandTypeInfo::kFixedUnsignedByte)       \
  V(Idx, OperandTypeInfo::kScalableUnsignedByte)      \
  V(Imm, OperandTypeInfo::kScalableSignedByte)        \
  V(RegCount, OperandTypeInfo::kScalableUnsignedByte) \
  V(RuntimeId, OperandTypeInfo::kFixedUnsignedShort)

#define REGISTER_OPERAND_TYPE_LIST(V) \
  REGISTER_INPUT_OPERAND_TYPE_LIST(V) \
  REGISTER_OUTPUT_OPERAND_TYPE_LIST(V)

#define NON_REGISTER_OPERAND_TYPE_LIST(V) \
  INVALID_OPERAND_TYPE_LIST(V)            \
  SCALAR_OPERAND_TYPE_LIST(V)

// The list of operand types used by bytecodes.
#define OPERAND_TYPE_LIST(V)        \
  NON_REGISTER_OPERAND_TYPE_LIST(V) \
  REGISTER_OPERAND_TYPE_LIST(V)

// Define one debug break bytecode for each possible size of unscaled
// bytecodes.
#define DEBUG_BREAK_PLAIN_BYTECODE_LIST(V)                                \
  V(DebugBreak0, OperandType::kNone)                                      \
  V(DebugBreak1, OperandType::kReg)                                       \
  V(DebugBreak2, OperandType::kReg, OperandType::kReg)                    \
  V(DebugBreak3, OperandType::kReg, OperandType::kReg, OperandType::kReg) \
  V(DebugBreak4, OperandType::kReg, OperandType::kReg, OperandType::kReg, \
    OperandType::kReg)                                                    \
  V(DebugBreak5, OperandType::kRuntimeId, OperandType::kReg,              \
    OperandType::kReg)                                                    \
  V(DebugBreak6, OperandType::kRuntimeId, OperandType::kReg,              \
    OperandType::kReg, OperandType::kReg)

// Define one debug break for each widening prefix.
#define DEBUG_BREAK_PREFIX_BYTECODE_LIST(V) \
  V(DebugBreakWide, OperandType::kNone)     \
  V(DebugBreakExtraWide, OperandType::kNone)

#define DEBUG_BREAK_BYTECODE_LIST(V) \
  DEBUG_BREAK_PLAIN_BYTECODE_LIST(V) \
  DEBUG_BREAK_PREFIX_BYTECODE_LIST(V)

// The list of bytecodes which are interpreted by the interpreter.
#define BYTECODE_LIST(V)                                                    \
  /* Extended width operands */                                             \
  V(Wide, OperandType::kNone)                                               \
  V(ExtraWide, OperandType::kNone)                                          \
                                                                            \
  /* Loading the accumulator */                                             \
  V(LdaZero, OperandType::kNone)                                            \
  V(LdaSmi, OperandType::kImm)                                              \
  V(LdaUndefined, OperandType::kNone)                                       \
  V(LdaNull, OperandType::kNone)                                            \
  V(LdaTheHole, OperandType::kNone)                                         \
  V(LdaTrue, OperandType::kNone)                                            \
  V(LdaFalse, OperandType::kNone)                                           \
  V(LdaConstant, OperandType::kIdx)                                         \
                                                                            \
  /* Globals */                                                             \
  V(LdaGlobal, OperandType::kIdx, OperandType::kIdx)                        \
  V(LdaGlobalInsideTypeof, OperandType::kIdx, OperandType::kIdx)            \
  V(StaGlobalSloppy, OperandType::kIdx, OperandType::kIdx)                  \
  V(StaGlobalStrict, OperandType::kIdx, OperandType::kIdx)                  \
                                                                            \
  /* Context operations */                                                  \
  V(PushContext, OperandType::kReg)                                         \
  V(PopContext, OperandType::kReg)                                          \
  V(LdaContextSlot, OperandType::kReg, OperandType::kIdx)                   \
  V(StaContextSlot, OperandType::kReg, OperandType::kIdx)                   \
                                                                            \
  /* Load-Store lookup slots */                                             \
  V(LdaLookupSlot, OperandType::kIdx)                                       \
  V(LdaLookupSlotInsideTypeof, OperandType::kIdx)                           \
  V(StaLookupSlotSloppy, OperandType::kIdx)                                 \
  V(StaLookupSlotStrict, OperandType::kIdx)                                 \
                                                                            \
  /* Register-accumulator transfers */                                      \
  V(Ldar, OperandType::kReg)                                                \
  V(Star, OperandType::kRegOut)                                             \
                                                                            \
  /* Register-register transfers */                                         \
  V(Mov, OperandType::kReg, OperandType::kRegOut)                           \
                                                                            \
  /* LoadIC operations */                                                   \
  V(LoadIC, OperandType::kReg, OperandType::kIdx, OperandType::kIdx)        \
  V(KeyedLoadIC, OperandType::kReg, OperandType::kIdx)                      \
                                                                            \
  /* StoreIC operations */                                                  \
  V(StoreICSloppy, OperandType::kReg, OperandType::kIdx, OperandType::kIdx) \
  V(StoreICStrict, OperandType::kReg, OperandType::kIdx, OperandType::kIdx) \
  V(KeyedStoreICSloppy, OperandType::kReg, OperandType::kReg,               \
    OperandType::kIdx)                                                      \
  V(KeyedStoreICStrict, OperandType::kReg, OperandType::kReg,               \
    OperandType::kIdx)                                                      \
                                                                            \
  /* Binary Operators */                                                    \
  V(Add, OperandType::kReg)                                                 \
  V(Sub, OperandType::kReg)                                                 \
  V(Mul, OperandType::kReg)                                                 \
  V(Div, OperandType::kReg)                                                 \
  V(Mod, OperandType::kReg)                                                 \
  V(BitwiseOr, OperandType::kReg)                                           \
  V(BitwiseXor, OperandType::kReg)                                          \
  V(BitwiseAnd, OperandType::kReg)                                          \
  V(ShiftLeft, OperandType::kReg)                                           \
  V(ShiftRight, OperandType::kReg)                                          \
  V(ShiftRightLogical, OperandType::kReg)                                   \
                                                                            \
  /* Unary Operators */                                                     \
  V(Inc, OperandType::kNone)                                                \
  V(Dec, OperandType::kNone)                                                \
  V(LogicalNot, OperandType::kNone)                                         \
  V(TypeOf, OperandType::kNone)                                             \
  V(DeletePropertyStrict, OperandType::kReg)                                \
  V(DeletePropertySloppy, OperandType::kReg)                                \
                                                                            \
  /* Call operations */                                                     \
  V(Call, OperandType::kReg, OperandType::kReg, OperandType::kRegCount,     \
    OperandType::kIdx)                                                      \
  V(TailCall, OperandType::kReg, OperandType::kReg, OperandType::kRegCount, \
    OperandType::kIdx)                                                      \
  V(CallRuntime, OperandType::kRuntimeId, OperandType::kMaybeReg,           \
    OperandType::kRegCount)                                                 \
  V(CallRuntimeForPair, OperandType::kRuntimeId, OperandType::kMaybeReg,    \
    OperandType::kRegCount, OperandType::kRegOutPair)                       \
  V(CallJSRuntime, OperandType::kIdx, OperandType::kReg,                    \
    OperandType::kRegCount)                                                 \
                                                                            \
  /* Intrinsics */                                                          \
  V(InvokeIntrinsic, OperandType::kRuntimeId, OperandType::kMaybeReg,       \
    OperandType::kRegCount)                                                 \
                                                                            \
  /* New operator */                                                        \
  V(New, OperandType::kReg, OperandType::kMaybeReg, OperandType::kRegCount) \
                                                                            \
  /* Test Operators */                                                      \
  V(TestEqual, OperandType::kReg)                                           \
  V(TestNotEqual, OperandType::kReg)                                        \
  V(TestEqualStrict, OperandType::kReg)                                     \
  V(TestLessThan, OperandType::kReg)                                        \
  V(TestGreaterThan, OperandType::kReg)                                     \
  V(TestLessThanOrEqual, OperandType::kReg)                                 \
  V(TestGreaterThanOrEqual, OperandType::kReg)                              \
  V(TestInstanceOf, OperandType::kReg)                                      \
  V(TestIn, OperandType::kReg)                                              \
                                                                            \
  /* Cast operators */                                                      \
  V(ToName, OperandType::kNone)                                             \
  V(ToNumber, OperandType::kNone)                                           \
  V(ToObject, OperandType::kNone)                                           \
                                                                            \
  /* Literals */                                                            \
  V(CreateRegExpLiteral, OperandType::kIdx, OperandType::kIdx,              \
    OperandType::kFlag8)                                                    \
  V(CreateArrayLiteral, OperandType::kIdx, OperandType::kIdx,               \
    OperandType::kFlag8)                                                    \
  V(CreateObjectLiteral, OperandType::kIdx, OperandType::kIdx,              \
    OperandType::kFlag8)                                                    \
                                                                            \
  /* Closure allocation */                                                  \
  V(CreateClosure, OperandType::kIdx, OperandType::kFlag8)                  \
                                                                            \
  /* Arguments allocation */                                                \
  V(CreateMappedArguments, OperandType::kNone)                              \
  V(CreateUnmappedArguments, OperandType::kNone)                            \
  V(CreateRestParameter, OperandType::kNone)                                \
                                                                            \
  /* Control Flow */                                                        \
  V(Jump, OperandType::kImm)                                                \
  V(JumpConstant, OperandType::kIdx)                                        \
  V(JumpIfTrue, OperandType::kImm)                                          \
  V(JumpIfTrueConstant, OperandType::kIdx)                                  \
  V(JumpIfFalse, OperandType::kImm)                                         \
  V(JumpIfFalseConstant, OperandType::kIdx)                                 \
  V(JumpIfToBooleanTrue, OperandType::kImm)                                 \
  V(JumpIfToBooleanTrueConstant, OperandType::kIdx)                         \
  V(JumpIfToBooleanFalse, OperandType::kImm)                                \
  V(JumpIfToBooleanFalseConstant, OperandType::kIdx)                        \
  V(JumpIfNull, OperandType::kImm)                                          \
  V(JumpIfNullConstant, OperandType::kIdx)                                  \
  V(JumpIfUndefined, OperandType::kImm)                                     \
  V(JumpIfUndefinedConstant, OperandType::kIdx)                             \
  V(JumpIfNotHole, OperandType::kImm)                                       \
  V(JumpIfNotHoleConstant, OperandType::kIdx)                               \
                                                                            \
  /* Complex flow control For..in */                                        \
  V(ForInPrepare, OperandType::kRegOutTriple)                               \
  V(ForInDone, OperandType::kReg, OperandType::kReg)                        \
  V(ForInNext, OperandType::kReg, OperandType::kReg, OperandType::kRegPair, \
    OperandType::kIdx)                                                      \
  V(ForInStep, OperandType::kReg)                                           \
                                                                            \
  /* Perform a stack guard check */                                         \
  V(StackCheck, OperandType::kNone)                                         \
                                                                            \
  /* Non-local flow control */                                              \
  V(Throw, OperandType::kNone)                                              \
  V(ReThrow, OperandType::kNone)                                            \
  V(Return, OperandType::kNone)                                             \
                                                                            \
  /* Debugger */                                                            \
  V(Debugger, OperandType::kNone)                                           \
  DEBUG_BREAK_BYTECODE_LIST(V)                                              \
                                                                            \
  /* Illegal bytecode (terminates execution) */                             \
  V(Illegal, OperandType::kNone)

// Enumeration of scaling factors applicable to scalable operands. Code
// relies on being able to cast values to integer scaling values.
enum class OperandScale : uint8_t {
  kSingle = 1,
  kDouble = 2,
  kQuadruple = 4,
  kMaxValid = kQuadruple,
  kInvalid = 8,
};

// Enumeration of the size classes of operand types used by
// bytecodes. Code relies on being able to cast values to integer
// types to get the size in bytes.
enum class OperandSize : uint8_t {
  kNone = 0,
  kByte = 1,
  kShort = 2,
  kQuad = 4,
  kLast = kQuad
};

// Primitive operand info used that summarize properties of operands.
// Columns are Name, IsScalable, IsUnsigned, UnscaledSize.
#define OPERAND_TYPE_INFO_LIST(V)                         \
  V(None, false, false, OperandSize::kNone)               \
  V(ScalableSignedByte, true, false, OperandSize::kByte)  \
  V(ScalableUnsignedByte, true, true, OperandSize::kByte) \
  V(FixedUnsignedByte, false, true, OperandSize::kByte)   \
  V(FixedUnsignedShort, false, true, OperandSize::kShort)

enum class OperandTypeInfo : uint8_t {
#define DECLARE_OPERAND_TYPE_INFO(Name, ...) k##Name,
  OPERAND_TYPE_INFO_LIST(DECLARE_OPERAND_TYPE_INFO)
#undef DECLARE_OPERAND_TYPE_INFO
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
  explicit Register(int index = kInvalidIndex) : index_(index) {}

  int index() const { return index_; }
  bool is_parameter() const { return index() < 0; }
  bool is_valid() const { return index_ != kInvalidIndex; }
  bool is_byte_operand() const;
  bool is_short_operand() const;

  static Register FromParameterIndex(int index, int parameter_count);
  int ToParameterIndex(int parameter_count) const;

  // Returns an invalid register.
  static Register invalid_value() { return Register(); }

  // Returns the register for the function's closure object.
  static Register function_closure();
  bool is_function_closure() const;

  // Returns the register which holds the current context object.
  static Register current_context();
  bool is_current_context() const;

  // Returns the register for the incoming new target value.
  static Register new_target();
  bool is_new_target() const;

  int32_t ToOperand() const { return -index_; }
  static Register FromOperand(int32_t operand) { return Register(-operand); }

  static bool AreContiguous(Register reg1, Register reg2,
                            Register reg3 = Register(),
                            Register reg4 = Register(),
                            Register reg5 = Register());

  std::string ToString(int parameter_count);

  bool operator==(const Register& other) const {
    return index() == other.index();
  }
  bool operator!=(const Register& other) const {
    return index() != other.index();
  }
  bool operator<(const Register& other) const {
    return index() < other.index();
  }
  bool operator<=(const Register& other) const {
    return index() <= other.index();
  }
  bool operator>(const Register& other) const {
    return index() > other.index();
  }
  bool operator>=(const Register& other) const {
    return index() >= other.index();
  }

 private:
  static const int kInvalidIndex = kMaxInt;

  void* operator new(size_t size);
  void operator delete(void* p);

  int index_;
};


class Bytecodes {
 public:
  // Returns string representation of |bytecode|.
  static const char* ToString(Bytecode bytecode);

  // Returns string representation of |bytecode|.
  static std::string ToString(Bytecode bytecode, OperandScale operand_scale);

  // Returns string representation of |operand_type|.
  static const char* OperandTypeToString(OperandType operand_type);

  // Returns string representation of |operand_scale|.
  static const char* OperandScaleToString(OperandScale operand_scale);

  // Returns string representation of |operand_size|.
  static const char* OperandSizeToString(OperandSize operand_size);

  // Returns byte value of bytecode.
  static uint8_t ToByte(Bytecode bytecode);

  // Returns bytecode for |value|.
  static Bytecode FromByte(uint8_t value);

  // Returns the number of operands expected by |bytecode|.
  static int NumberOfOperands(Bytecode bytecode);

  // Returns the number of register operands expected by |bytecode|.
  static int NumberOfRegisterOperands(Bytecode bytecode);

  // Returns the prefix bytecode representing an operand scale to be
  // applied to a a bytecode.
  static Bytecode OperandScaleToPrefixBytecode(OperandScale operand_scale);

  // Returns true if the operand scale requires a prefix bytecode.
  static bool OperandScaleRequiresPrefixBytecode(OperandScale operand_scale);

  // Returns the scaling applied to scalable operands if bytecode is
  // is a scaling prefix.
  static OperandScale PrefixBytecodeToOperandScale(Bytecode bytecode);

  // Returns the i-th operand of |bytecode|.
  static OperandType GetOperandType(Bytecode bytecode, int i);

  // Returns the size of the i-th operand of |bytecode|.
  static OperandSize GetOperandSize(Bytecode bytecode, int i,
                                    OperandScale operand_scale);

  // Returns the offset of the i-th operand of |bytecode| relative to the start
  // of the bytecode.
  static int GetOperandOffset(Bytecode bytecode, int i,
                              OperandScale operand_scale);

  // Returns a zero-based bitmap of the register operand positions of
  // |bytecode|.
  static int GetRegisterOperandBitmap(Bytecode bytecode);

  // Returns a debug break bytecode to replace |bytecode|.
  static Bytecode GetDebugBreak(Bytecode bytecode);

  // Returns the size of the bytecode including its operands for the
  // given |operand_scale|.
  static int Size(Bytecode bytecode, OperandScale operand_scale);

  // Returns the size of |operand|.
  static OperandSize SizeOfOperand(OperandType operand, OperandScale scale);

  // Returns true if the bytecode is a conditional jump taking
  // an immediate byte operand (OperandType::kImm).
  static bool IsConditionalJumpImmediate(Bytecode bytecode);

  // Returns true if the bytecode is a conditional jump taking
  // a constant pool entry (OperandType::kIdx).
  static bool IsConditionalJumpConstant(Bytecode bytecode);

  // Returns true if the bytecode is a conditional jump taking
  // any kind of operand.
  static bool IsConditionalJump(Bytecode bytecode);

  // Returns true if the bytecode is a jump or a conditional jump taking
  // an immediate byte operand (OperandType::kImm).
  static bool IsJumpImmediate(Bytecode bytecode);

  // Returns true if the bytecode is a jump or conditional jump taking a
  // constant pool entry (OperandType::kIdx).
  static bool IsJumpConstant(Bytecode bytecode);

  // Returns true if the bytecode is a jump or conditional jump taking
  // any kind of operand.
  static bool IsJump(Bytecode bytecode);

  // Returns true if the bytecode is a conditional jump, a jump, or a return.
  static bool IsJumpOrReturn(Bytecode bytecode);

  // Returns true if the bytecode is a call or a constructor call.
  static bool IsCallOrNew(Bytecode bytecode);

  // Returns true if the bytecode is a call to the runtime.
  static bool IsCallRuntime(Bytecode bytecode);

  // Returns true if the bytecode is a debug break.
  static bool IsDebugBreak(Bytecode bytecode);

  // Returns true if the bytecode has wider operand forms.
  static bool IsBytecodeWithScalableOperands(Bytecode bytecode);

  // Returns true if the bytecode is a scaling prefix bytecode.
  static bool IsPrefixScalingBytecode(Bytecode bytecode);

  // Returns true if |operand_type| is any type of register operand.
  static bool IsRegisterOperandType(OperandType operand_type);

  // Returns true if |operand_type| represents a register used as an input.
  static bool IsRegisterInputOperandType(OperandType operand_type);

  // Returns true if |operand_type| represents a register used as an output.
  static bool IsRegisterOutputOperandType(OperandType operand_type);

  // Returns true if |operand_type| is a maybe register operand
  // (kMaybeReg).
  static bool IsMaybeRegisterOperandType(OperandType operand_type);

  // Returns true if |operand_type| is a runtime-id operand (kRuntimeId).
  static bool IsRuntimeIdOperandType(OperandType operand_type);

  // Returns true if |operand_type| is unsigned, false if signed.
  static bool IsUnsignedOperandType(OperandType operand_type);

  // Decodes a register operand in a byte array.
  static Register DecodeRegisterOperand(const uint8_t* operand_start,
                                        OperandType operand_type,
                                        OperandScale operand_scale);

  // Decodes a signed operand in a byte array.
  static int32_t DecodeSignedOperand(const uint8_t* operand_start,
                                     OperandType operand_type,
                                     OperandScale operand_scale);

  // Decodes an unsigned operand in a byte array.
  static uint32_t DecodeUnsignedOperand(const uint8_t* operand_start,
                                        OperandType operand_type,
                                        OperandScale operand_scale);

  // Decode a single bytecode and operands to |os|.
  static std::ostream& Decode(std::ostream& os, const uint8_t* bytecode_start,
                              int number_of_parameters);

  // Return the next larger operand scale.
  static OperandScale NextOperandScale(OperandScale operand_scale);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Bytecodes);
};

std::ostream& operator<<(std::ostream& os, const Bytecode& bytecode);
std::ostream& operator<<(std::ostream& os, const OperandScale& operand_scale);
std::ostream& operator<<(std::ostream& os, const OperandSize& operand_size);
std::ostream& operator<<(std::ostream& os, const OperandType& operand_type);

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODES_H_
