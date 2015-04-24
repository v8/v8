// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-operator.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/operator-properties.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {


namespace {

typedef const Operator* (JSOperatorBuilder::*no_params_t) () ;

typedef const Operator* (JSOperatorBuilder::*with_language_mode_t)
          (LanguageMode language_mode) ;


template <typename T>
struct SharedOperator {
  T constructor;
  IrOpcode::Value opcode;
  Operator::Properties properties;
  int value_input_count;
  int frame_state_input_count;
  int effect_input_count;
  int control_input_count;
  int value_output_count;
  int effect_output_count;
  int control_output_count;
};


const SharedOperator<no_params_t> kSharedOperators[] = {
#define SHARED(Name, properties, value_input_count, frame_state_input_count, \
               effect_input_count, control_input_count, value_output_count,  \
               effect_output_count, control_output_count)                    \
  {                                                                          \
    &JSOperatorBuilder::Name, IrOpcode::kJS##Name, properties,               \
        value_input_count, frame_state_input_count, effect_input_count,      \
        control_input_count, value_output_count, effect_output_count,        \
        control_output_count                                                 \
  }
    SHARED(Equal, Operator::kNoProperties, 2, 1, 1, 1, 1, 1, 2),
    SHARED(NotEqual, Operator::kNoProperties, 2, 1, 1, 1, 1, 1, 2),
    SHARED(StrictEqual, Operator::kPure, 2, 0, 0, 0, 1, 0, 0),
    SHARED(StrictNotEqual, Operator::kPure, 2, 0, 0, 0, 1, 0, 0),
    SHARED(UnaryNot, Operator::kPure, 1, 0, 0, 0, 1, 0, 0),
    SHARED(ToBoolean, Operator::kPure, 1, 0, 0, 0, 1, 0, 0),
    SHARED(ToNumber, Operator::kNoProperties, 1, 1, 1, 1, 1, 1, 2),
    SHARED(ToString, Operator::kNoProperties, 1, 0, 1, 1, 1, 1, 2),
    SHARED(ToName, Operator::kNoProperties, 1, 1, 1, 1, 1, 1, 2),
    SHARED(ToObject, Operator::kNoProperties, 1, 1, 1, 1, 1, 1, 2),
    SHARED(Yield, Operator::kNoProperties, 1, 0, 1, 1, 1, 1, 2),
    SHARED(Create, Operator::kEliminatable, 0, 0, 1, 0, 1, 1, 0),
    SHARED(HasProperty, Operator::kNoProperties, 2, 1, 1, 1, 1, 1, 2),
    SHARED(TypeOf, Operator::kPure, 1, 0, 0, 0, 1, 0, 0),
    SHARED(InstanceOf, Operator::kNoProperties, 2, 1, 1, 1, 1, 1, 2),
    SHARED(CreateFunctionContext, Operator::kNoProperties, 1, 0, 1, 1, 1, 1, 2),
    SHARED(CreateWithContext, Operator::kNoProperties, 2, 1, 1, 1, 1, 1, 2),
    SHARED(CreateBlockContext, Operator::kNoProperties, 2, 0, 1, 1, 1, 1, 2),
    SHARED(CreateModuleContext, Operator::kNoProperties, 2, 0, 1, 1, 1, 1, 2),
    SHARED(CreateScriptContext, Operator::kNoProperties, 2, 1, 1, 1, 1, 1, 2)
#undef SHARED
};


const SharedOperator<with_language_mode_t>
  kSharedOperatorsWithlanguageMode[] = {
#define SHARED(Name, properties, value_input_count, frame_state_input_count, \
               effect_input_count, control_input_count, value_output_count,  \
               effect_output_count, control_output_count)                    \
  {                                                                          \
    &JSOperatorBuilder::Name, IrOpcode::kJS##Name, properties,               \
        value_input_count, frame_state_input_count, effect_input_count,      \
        control_input_count, value_output_count, effect_output_count,        \
        control_output_count                                                 \
  }
    SHARED(LessThan, Operator::kNoProperties, 2, 1, 1, 1, 1, 1, 2),
    SHARED(GreaterThan, Operator::kNoProperties, 2, 1, 1, 1, 1, 1, 2),
    SHARED(LessThanOrEqual, Operator::kNoProperties, 2, 1, 1, 1, 1, 1, 2),
    SHARED(GreaterThanOrEqual, Operator::kNoProperties, 2, 1, 1, 1, 1, 1, 2),
    SHARED(BitwiseOr, Operator::kNoProperties, 2, 2, 1, 1, 1, 1, 2),
    SHARED(BitwiseXor, Operator::kNoProperties, 2, 2, 1, 1, 1, 1, 2),
    SHARED(BitwiseAnd, Operator::kNoProperties, 2, 2, 1, 1, 1, 1, 2),
    SHARED(ShiftLeft, Operator::kNoProperties, 2, 2, 1, 1, 1, 1, 2),
    SHARED(ShiftRight, Operator::kNoProperties, 2, 2, 1, 1, 1, 1, 2),
    SHARED(ShiftRightLogical, Operator::kNoProperties, 2, 2, 1, 1, 1, 1, 2),
    SHARED(Add, Operator::kNoProperties, 2, 2, 1, 1, 1, 1, 2),
    SHARED(Subtract, Operator::kNoProperties, 2, 2, 1, 1, 1, 1, 2),
    SHARED(Multiply, Operator::kNoProperties, 2, 2, 1, 1, 1, 1, 2),
    SHARED(Divide, Operator::kNoProperties, 2, 2, 1, 1, 1, 1, 2),
    SHARED(Modulus, Operator::kNoProperties, 2, 2, 1, 1, 1, 1, 2),
#undef SHARED
};


template <typename T>
void testNumberOfInputsAndOutputs(const SharedOperator<T>& sop,
                                  const Operator* op) {
  const int context_input_count = 1;
  EXPECT_EQ(sop.value_input_count, op->ValueInputCount());
  EXPECT_EQ(context_input_count, OperatorProperties::GetContextInputCount(op));
  EXPECT_EQ(sop.frame_state_input_count,
            OperatorProperties::GetFrameStateInputCount(op));
  EXPECT_EQ(sop.effect_input_count, op->EffectInputCount());
  EXPECT_EQ(sop.control_input_count, op->ControlInputCount());
  EXPECT_EQ(sop.value_input_count + context_input_count +
                sop.frame_state_input_count + sop.effect_input_count +
                sop.control_input_count,
            OperatorProperties::GetTotalInputCount(op));

  EXPECT_EQ(sop.value_output_count, op->ValueOutputCount());
  EXPECT_EQ(sop.effect_output_count, op->EffectOutputCount());
  EXPECT_EQ(sop.control_output_count, op->ControlOutputCount());
}


std::ostream& operator<<(std::ostream& os,
                         const SharedOperator<no_params_t>& sop) {
  return os << IrOpcode::Mnemonic(sop.opcode);
}


std::ostream& operator<<(std::ostream& os,
                         const SharedOperator<with_language_mode_t>& sop) {
  return os << IrOpcode::Mnemonic(sop.opcode);
}

}  // namespace


// -----------------------------------------------------------------------------
// Shared operators.


class JSSharedOperatorTest
    : public TestWithZone,
      public ::testing::WithParamInterface<SharedOperator<no_params_t>> {};


TEST_P(JSSharedOperatorTest, InstancesAreGloballyShared) {
  const SharedOperator<no_params_t>& sop = GetParam();
  JSOperatorBuilder javascript1(zone());
  JSOperatorBuilder javascript2(zone());
  EXPECT_EQ((javascript1.*sop.constructor)(), (javascript2.*sop.constructor)());
}


TEST_P(JSSharedOperatorTest, NumberOfInputsAndOutputs) {
  JSOperatorBuilder javascript(zone());
  const SharedOperator<no_params_t>& sop = GetParam();
  const Operator* op = (javascript.*sop.constructor)();
  testNumberOfInputsAndOutputs(sop, op);
}


TEST_P(JSSharedOperatorTest, OpcodeIsCorrect) {
  JSOperatorBuilder javascript(zone());
  const SharedOperator<no_params_t>& sop = GetParam();
  const Operator* op = (javascript.*sop.constructor)();
  EXPECT_EQ(sop.opcode, op->opcode());
}


TEST_P(JSSharedOperatorTest, Properties) {
  JSOperatorBuilder javascript(zone());
  const SharedOperator<no_params_t>& sop = GetParam();
  const Operator* op = (javascript.*sop.constructor)();
  EXPECT_EQ(sop.properties, op->properties());
}


INSTANTIATE_TEST_CASE_P(JSOperatorTest, JSSharedOperatorTest,
                        ::testing::ValuesIn(kSharedOperators));

// -----------------------------------------------------------------------------
// Shared operators which behave differently in strong mode


class JSSharedOperatorWithStrongTest
    : public TestWithZone,
      public ::testing::WithParamInterface<
                          SharedOperator<with_language_mode_t>>{};


TEST_P(JSSharedOperatorWithStrongTest, InstancesAreGloballyShared) {
  const SharedOperator<with_language_mode_t>& sop = GetParam();
  JSOperatorBuilder javascript1(zone());
  JSOperatorBuilder javascript2(zone());
  EXPECT_EQ((javascript1.*sop.constructor)(LanguageMode::SLOPPY),
            (javascript2.*sop.constructor)(LanguageMode::SLOPPY));
  EXPECT_EQ((javascript1.*sop.constructor)(LanguageMode::STRONG),
            (javascript2.*sop.constructor)(LanguageMode::STRONG));
}


TEST_P(JSSharedOperatorWithStrongTest, NumberOfInputsAndOutputs) {
  JSOperatorBuilder javascript(zone());
  const SharedOperator<with_language_mode_t>& sop = GetParam();
  const Operator* op_sloppy = (javascript.*sop.constructor)
      (LanguageMode::SLOPPY);
  testNumberOfInputsAndOutputs(sop, op_sloppy);
  const Operator* op_strong = (javascript.*sop.constructor)
      (LanguageMode::STRONG);
  testNumberOfInputsAndOutputs(sop, op_strong);
}


TEST_P(JSSharedOperatorWithStrongTest, OpcodeIsCorrect) {
  JSOperatorBuilder javascript(zone());
  const SharedOperator<with_language_mode_t>& sop = GetParam();
  const Operator* op_sloppy = (javascript.*sop.constructor)
      (LanguageMode::SLOPPY);
  EXPECT_EQ(sop.opcode, op_sloppy->opcode());
  const Operator* op_strong = (javascript.*sop.constructor)
      (LanguageMode::STRONG);
  EXPECT_EQ(sop.opcode, op_strong->opcode());
}


TEST_P(JSSharedOperatorWithStrongTest, Properties) {
  JSOperatorBuilder javascript(zone());
  const SharedOperator<with_language_mode_t>& sop = GetParam();
  const Operator* op_sloppy = (javascript.*sop.constructor)
      (LanguageMode::SLOPPY);
  EXPECT_EQ(sop.properties, op_sloppy->properties());
  const Operator* op_strong = (javascript.*sop.constructor)
      (LanguageMode::STRONG);
  EXPECT_EQ(sop.properties, op_strong->properties());
}


INSTANTIATE_TEST_CASE_P(JSOperatorTest, JSSharedOperatorWithStrongTest,
                        ::testing::ValuesIn(kSharedOperatorsWithlanguageMode));

// -----------------------------------------------------------------------------
// JSStoreProperty.


class JSStorePropertyOperatorTest
    : public TestWithZone,
      public ::testing::WithParamInterface<LanguageMode> {};


TEST_P(JSStorePropertyOperatorTest, InstancesAreGloballyShared) {
  const LanguageMode mode = GetParam();
  JSOperatorBuilder javascript1(zone());
  JSOperatorBuilder javascript2(zone());
  EXPECT_EQ(javascript1.StoreProperty(mode), javascript2.StoreProperty(mode));
}


TEST_P(JSStorePropertyOperatorTest, NumberOfInputsAndOutputs) {
  JSOperatorBuilder javascript(zone());
  const LanguageMode mode = GetParam();
  const Operator* op = javascript.StoreProperty(mode);

  EXPECT_EQ(3, op->ValueInputCount());
  EXPECT_EQ(1, OperatorProperties::GetContextInputCount(op));
  EXPECT_EQ(2, OperatorProperties::GetFrameStateInputCount(op));
  EXPECT_EQ(1, op->EffectInputCount());
  EXPECT_EQ(1, op->ControlInputCount());
  EXPECT_EQ(8, OperatorProperties::GetTotalInputCount(op));

  EXPECT_EQ(0, op->ValueOutputCount());
  EXPECT_EQ(1, op->EffectOutputCount());
  EXPECT_EQ(2, op->ControlOutputCount());
}


TEST_P(JSStorePropertyOperatorTest, OpcodeIsCorrect) {
  JSOperatorBuilder javascript(zone());
  const LanguageMode mode = GetParam();
  const Operator* op = javascript.StoreProperty(mode);
  EXPECT_EQ(IrOpcode::kJSStoreProperty, op->opcode());
}


TEST_P(JSStorePropertyOperatorTest, OpParameter) {
  JSOperatorBuilder javascript(zone());
  const LanguageMode mode = GetParam();
  const Operator* op = javascript.StoreProperty(mode);
  EXPECT_EQ(mode, OpParameter<LanguageMode>(op));
}


TEST_P(JSStorePropertyOperatorTest, Properties) {
  JSOperatorBuilder javascript(zone());
  const LanguageMode mode = GetParam();
  const Operator* op = javascript.StoreProperty(mode);
  EXPECT_EQ(Operator::kNoProperties, op->properties());
}


INSTANTIATE_TEST_CASE_P(JSOperatorTest, JSStorePropertyOperatorTest,
                        ::testing::Values(SLOPPY, STRICT));

}  // namespace compiler
}  // namespace internal
}  // namespace v8
