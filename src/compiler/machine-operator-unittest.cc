// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/compiler-unittests.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/operator-properties-inl.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::IsNull;

namespace v8 {
namespace internal {
namespace compiler {

class MachineOperatorCommonTest : public CompilerTestWithParam<MachineType> {
 public:
  MachineOperatorCommonTest() : machine_(NULL) {}
  virtual ~MachineOperatorCommonTest() { EXPECT_THAT(machine_, IsNull()); }

  virtual void SetUp() OVERRIDE {
    CompilerTestWithParam<MachineType>::SetUp();
    machine_ = new MachineOperatorBuilder(zone(), GetParam());
  }

  virtual void TearDown() OVERRIDE {
    delete machine_;
    machine_ = NULL;
    CompilerTestWithParam<MachineType>::TearDown();
  }

 protected:
  MachineOperatorBuilder* machine() const { return machine_; }

 private:
  MachineOperatorBuilder* machine_;
};


TEST_P(MachineOperatorCommonTest, ChangeInt32ToInt64) {
  Operator* op = machine()->ChangeInt32ToInt64();
  EXPECT_EQ(1, OperatorProperties::GetValueInputCount(op));
  EXPECT_EQ(1, OperatorProperties::GetTotalInputCount(op));
  EXPECT_EQ(0, OperatorProperties::GetControlOutputCount(op));
  EXPECT_EQ(0, OperatorProperties::GetEffectOutputCount(op));
  EXPECT_EQ(1, OperatorProperties::GetValueOutputCount(op));
}


TEST_P(MachineOperatorCommonTest, ChangeUint32ToUint64) {
  Operator* op = machine()->ChangeUint32ToUint64();
  EXPECT_EQ(1, OperatorProperties::GetValueInputCount(op));
  EXPECT_EQ(1, OperatorProperties::GetTotalInputCount(op));
  EXPECT_EQ(0, OperatorProperties::GetControlOutputCount(op));
  EXPECT_EQ(0, OperatorProperties::GetEffectOutputCount(op));
  EXPECT_EQ(1, OperatorProperties::GetValueOutputCount(op));
}


TEST_P(MachineOperatorCommonTest, TruncateFloat64ToInt32) {
  Operator* op = machine()->TruncateFloat64ToInt32();
  EXPECT_EQ(1, OperatorProperties::GetValueInputCount(op));
  EXPECT_EQ(1, OperatorProperties::GetTotalInputCount(op));
  EXPECT_EQ(0, OperatorProperties::GetControlOutputCount(op));
  EXPECT_EQ(0, OperatorProperties::GetEffectOutputCount(op));
  EXPECT_EQ(1, OperatorProperties::GetValueOutputCount(op));
}


TEST_P(MachineOperatorCommonTest, TruncateInt64ToInt32) {
  Operator* op = machine()->TruncateInt64ToInt32();
  EXPECT_EQ(1, OperatorProperties::GetValueInputCount(op));
  EXPECT_EQ(1, OperatorProperties::GetTotalInputCount(op));
  EXPECT_EQ(0, OperatorProperties::GetControlOutputCount(op));
  EXPECT_EQ(0, OperatorProperties::GetEffectOutputCount(op));
  EXPECT_EQ(1, OperatorProperties::GetValueOutputCount(op));
}


INSTANTIATE_TEST_CASE_P(MachineOperatorTest, MachineOperatorCommonTest,
                        ::testing::Values(kRepWord32, kRepWord64));

}  // namespace compiler
}  // namespace internal
}  // namespace v8
