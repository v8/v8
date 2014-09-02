// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-unittests.h"
#include "src/compiler/operator-properties-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

class CommonOperatorTest : public CompilerTest {
 public:
  CommonOperatorTest() : common_(zone()) {}
  virtual ~CommonOperatorTest() {}

  CommonOperatorBuilder* common() { return &common_; }

 private:
  CommonOperatorBuilder common_;
};


const int kArguments[] = {1, 5, 6, 42, 100, 10000, kMaxInt};

}  // namespace


TEST_F(CommonOperatorTest, ControlEffect) {
  Operator* op = common()->ControlEffect();
  EXPECT_EQ(1, OperatorProperties::GetControlInputCount(op));
  EXPECT_EQ(1, OperatorProperties::GetTotalInputCount(op));
  EXPECT_EQ(0, OperatorProperties::GetControlOutputCount(op));
  EXPECT_EQ(1, OperatorProperties::GetEffectOutputCount(op));
  EXPECT_EQ(0, OperatorProperties::GetValueOutputCount(op));
}


TEST_F(CommonOperatorTest, ValueEffect) {
  TRACED_FOREACH(int, arguments, kArguments) {
    Operator* op = common()->ValueEffect(arguments);
    EXPECT_EQ(arguments, OperatorProperties::GetValueInputCount(op));
    EXPECT_EQ(arguments, OperatorProperties::GetTotalInputCount(op));
    EXPECT_EQ(0, OperatorProperties::GetControlOutputCount(op));
    EXPECT_EQ(1, OperatorProperties::GetEffectOutputCount(op));
    EXPECT_EQ(0, OperatorProperties::GetValueOutputCount(op));
  }
}


TEST_F(CommonOperatorTest, Finish) {
  TRACED_FOREACH(int, arguments, kArguments) {
    Operator* op = common()->Finish(arguments);
    EXPECT_EQ(1, OperatorProperties::GetValueInputCount(op));
    EXPECT_EQ(arguments, OperatorProperties::GetEffectInputCount(op));
    EXPECT_EQ(arguments + 1, OperatorProperties::GetTotalInputCount(op));
    EXPECT_EQ(0, OperatorProperties::GetControlOutputCount(op));
    EXPECT_EQ(0, OperatorProperties::GetEffectOutputCount(op));
    EXPECT_EQ(1, OperatorProperties::GetValueOutputCount(op));
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
