// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "src/v8.h"

#include "src/interpreter/bytecodes.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace interpreter {

TEST(OperandConversion, Registers) {
  int register_count = 128;
  int step = register_count / 7;
  for (int i = 0; i < register_count; i += step) {
    if (i <= kMaxInt8) {
      uint32_t operand0 = Register(i).ToOperand();
      Register reg0 = Register::FromOperand(operand0);
      CHECK_EQ(i, reg0.index());
    }

    uint32_t operand1 = Register(i).ToOperand();
    Register reg1 = Register::FromOperand(operand1);
    CHECK_EQ(i, reg1.index());

    uint32_t operand2 = Register(i).ToOperand();
    Register reg2 = Register::FromOperand(operand2);
    CHECK_EQ(i, reg2.index());
  }

  for (int i = 0; i <= kMaxUInt8; i++) {
    Register reg = Register::FromOperand(i);
    if (i > 0) {
      CHECK(reg.is_parameter());
    } else {
      CHECK(!reg.is_parameter());
    }
  }
}

TEST(OperandConversion, Parameters) {
  int parameter_counts[] = {7, 13, 99};

  size_t count = sizeof(parameter_counts) / sizeof(parameter_counts[0]);
  for (size_t p = 0; p < count; p++) {
    int parameter_count = parameter_counts[p];
    for (int i = 0; i < parameter_count; i++) {
      Register r = Register::FromParameterIndex(i, parameter_count);
      uint32_t operand_value = r.ToOperand();
      Register s = Register::FromOperand(operand_value);
      CHECK_EQ(i, s.ToParameterIndex(parameter_count));
    }
  }
}

TEST(OperandConversion, RegistersParametersNoOverlap) {
  int register_count = 128;
  int parameter_count = 100;
  int32_t register_space_size = base::bits::RoundUpToPowerOfTwo32(
      static_cast<uint32_t>(register_count + parameter_count));
  uint32_t range = static_cast<uint32_t>(register_space_size);
  std::vector<uint8_t> operand_count(range);

  for (int i = 0; i < register_count; i += 1) {
    Register r = Register(i);
    int32_t operand = r.ToOperand();
    uint8_t index = static_cast<uint8_t>(operand);
    CHECK_LT(index, operand_count.size());
    operand_count[index] += 1;
    CHECK_EQ(operand_count[index], 1);
  }

  for (int i = 0; i < parameter_count; i += 1) {
    Register r = Register::FromParameterIndex(i, parameter_count);
    uint32_t operand = r.ToOperand();
    uint8_t index = static_cast<uint8_t>(operand);
    CHECK_LT(index, operand_count.size());
    operand_count[index] += 1;
    CHECK_EQ(operand_count[index], 1);
  }
}

TEST(OperandScaling, ScalableAndNonScalable) {
  for (OperandScale operand_scale = OperandScale::kSingle;
       operand_scale <= OperandScale::kMaxValid;
       operand_scale = Bytecodes::NextOperandScale(operand_scale)) {
    int scale = static_cast<int>(operand_scale);
    CHECK_EQ(Bytecodes::Size(Bytecode::kCallRuntime, operand_scale),
             1 + 2 + 2 * scale);
    CHECK_EQ(Bytecodes::Size(Bytecode::kCreateObjectLiteral, operand_scale),
             1 + 2 * scale + 1);
    CHECK_EQ(Bytecodes::Size(Bytecode::kTestIn, operand_scale), 1 + scale);
  }
}

TEST(Bytecodes, HasAnyRegisterOperands) {
  CHECK_EQ(Bytecodes::NumberOfRegisterOperands(Bytecode::kAdd), 1);
  CHECK_EQ(Bytecodes::NumberOfRegisterOperands(Bytecode::kCall), 2);
  CHECK_EQ(Bytecodes::NumberOfRegisterOperands(Bytecode::kCallRuntime), 1);
  CHECK_EQ(Bytecodes::NumberOfRegisterOperands(Bytecode::kCallRuntimeForPair),
           2);
  CHECK_EQ(Bytecodes::NumberOfRegisterOperands(Bytecode::kDeletePropertyStrict),
           1);
  CHECK_EQ(Bytecodes::NumberOfRegisterOperands(Bytecode::kForInPrepare), 1);
  CHECK_EQ(Bytecodes::NumberOfRegisterOperands(Bytecode::kInc), 0);
  CHECK_EQ(Bytecodes::NumberOfRegisterOperands(Bytecode::kJumpIfTrue), 0);
  CHECK_EQ(Bytecodes::NumberOfRegisterOperands(Bytecode::kNew), 2);
  CHECK_EQ(Bytecodes::NumberOfRegisterOperands(Bytecode::kToName), 0);
}

TEST(Bytecodes, RegisterOperandBitmaps) {
  CHECK_EQ(Bytecodes::GetRegisterOperandBitmap(Bytecode::kAdd), 1);
  CHECK_EQ(Bytecodes::GetRegisterOperandBitmap(Bytecode::kCallRuntimeForPair),
           10);
  CHECK_EQ(Bytecodes::GetRegisterOperandBitmap(Bytecode::kStar), 1);
  CHECK_EQ(Bytecodes::GetRegisterOperandBitmap(Bytecode::kMov), 3);
  CHECK_EQ(Bytecodes::GetRegisterOperandBitmap(Bytecode::kTestIn), 1);
  CHECK_EQ(Bytecodes::GetRegisterOperandBitmap(Bytecode::kForInPrepare), 1);
  CHECK_EQ(Bytecodes::GetRegisterOperandBitmap(Bytecode::kForInDone), 3);
  CHECK_EQ(Bytecodes::GetRegisterOperandBitmap(Bytecode::kForInNext), 7);
}

TEST(Bytecodes, RegisterOperands) {
  CHECK(Bytecodes::IsRegisterOperandType(OperandType::kReg));
  CHECK(Bytecodes::IsRegisterInputOperandType(OperandType::kReg));
  CHECK(!Bytecodes::IsRegisterOutputOperandType(OperandType::kReg));
  CHECK(!Bytecodes::IsRegisterInputOperandType(OperandType::kRegOut));
  CHECK(Bytecodes::IsRegisterOutputOperandType(OperandType::kRegOut));

#define IS_REGISTER_OPERAND_TYPE(Name, _) \
  CHECK(Bytecodes::IsRegisterOperandType(OperandType::k##Name));
  REGISTER_OPERAND_TYPE_LIST(IS_REGISTER_OPERAND_TYPE)
#undef IS_REGISTER_OPERAND_TYPE

#define IS_NOT_REGISTER_OPERAND_TYPE(Name, _) \
  CHECK(!Bytecodes::IsRegisterOperandType(OperandType::k##Name));
  NON_REGISTER_OPERAND_TYPE_LIST(IS_NOT_REGISTER_OPERAND_TYPE)
#undef IS_NOT_REGISTER_OPERAND_TYPE

#define IS_REGISTER_INPUT_OPERAND_TYPE(Name, _) \
  CHECK(Bytecodes::IsRegisterInputOperandType(OperandType::k##Name));
  REGISTER_INPUT_OPERAND_TYPE_LIST(IS_REGISTER_INPUT_OPERAND_TYPE)
#undef IS_REGISTER_INPUT_OPERAND_TYPE

#define IS_NOT_REGISTER_INPUT_OPERAND_TYPE(Name, _) \
  CHECK(!Bytecodes::IsRegisterInputOperandType(OperandType::k##Name));
  NON_REGISTER_OPERAND_TYPE_LIST(IS_NOT_REGISTER_INPUT_OPERAND_TYPE);
  REGISTER_OUTPUT_OPERAND_TYPE_LIST(IS_NOT_REGISTER_INPUT_OPERAND_TYPE)
#undef IS_NOT_REGISTER_INPUT_OPERAND_TYPE

#define IS_REGISTER_OUTPUT_OPERAND_TYPE(Name, _) \
  CHECK(Bytecodes::IsRegisterOutputOperandType(OperandType::k##Name));
  REGISTER_OUTPUT_OPERAND_TYPE_LIST(IS_REGISTER_OUTPUT_OPERAND_TYPE)
#undef IS_REGISTER_OUTPUT_OPERAND_TYPE

#define IS_NOT_REGISTER_OUTPUT_OPERAND_TYPE(Name, _) \
  CHECK(!Bytecodes::IsRegisterOutputOperandType(OperandType::k##Name));
  NON_REGISTER_OPERAND_TYPE_LIST(IS_NOT_REGISTER_OUTPUT_OPERAND_TYPE)
  REGISTER_INPUT_OPERAND_TYPE_LIST(IS_NOT_REGISTER_OUTPUT_OPERAND_TYPE)
#undef IS_NOT_REGISTER_INPUT_OPERAND_TYPE
}

TEST(Bytecodes, DebugBreakExistForEachBytecode) {
  static const OperandScale kOperandScale = OperandScale::kSingle;
#define CHECK_DEBUG_BREAK_SIZE(Name, ...)                                  \
  if (!Bytecodes::IsDebugBreak(Bytecode::k##Name) &&                       \
      !Bytecodes::IsPrefixScalingBytecode(Bytecode::k##Name)) {            \
    Bytecode debug_bytecode = Bytecodes::GetDebugBreak(Bytecode::k##Name); \
    CHECK_EQ(Bytecodes::Size(Bytecode::k##Name, kOperandScale),            \
             Bytecodes::Size(debug_bytecode, kOperandScale));              \
  }
  BYTECODE_LIST(CHECK_DEBUG_BREAK_SIZE)
#undef CHECK_DEBUG_BREAK_SIZE
}

TEST(Bytecodes, DebugBreakForPrefixBytecodes) {
  CHECK_EQ(Bytecode::kDebugBreakWide,
           Bytecodes::GetDebugBreak(Bytecode::kWide));
  CHECK_EQ(Bytecode::kDebugBreakExtraWide,
           Bytecodes::GetDebugBreak(Bytecode::kExtraWide));
}

TEST(Bytecodes, PrefixMappings) {
  Bytecode prefixes[] = {Bytecode::kWide, Bytecode::kExtraWide};
  TRACED_FOREACH(Bytecode, prefix, prefixes) {
    CHECK_EQ(prefix, Bytecodes::OperandScaleToPrefixBytecode(
                         Bytecodes::PrefixBytecodeToOperandScale(prefix)));
  }
}

TEST(OperandScale, PrefixesScale) {
  CHECK(Bytecodes::NextOperandScale(OperandScale::kSingle) ==
        OperandScale::kDouble);
  CHECK(Bytecodes::NextOperandScale(OperandScale::kDouble) ==
        OperandScale::kQuadruple);
  CHECK(Bytecodes::NextOperandScale(OperandScale::kQuadruple) ==
        OperandScale::kInvalid);
}

TEST(OperandScale, PrefixesRequired) {
  CHECK(!Bytecodes::OperandScaleRequiresPrefixBytecode(OperandScale::kSingle));
  CHECK(Bytecodes::OperandScaleRequiresPrefixBytecode(OperandScale::kDouble));
  CHECK(
      Bytecodes::OperandScaleRequiresPrefixBytecode(OperandScale::kQuadruple));
  CHECK(Bytecodes::OperandScaleToPrefixBytecode(OperandScale::kDouble) ==
        Bytecode::kWide);
  CHECK(Bytecodes::OperandScaleToPrefixBytecode(OperandScale::kQuadruple) ==
        Bytecode::kExtraWide);
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
