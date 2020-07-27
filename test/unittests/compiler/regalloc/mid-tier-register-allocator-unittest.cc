// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-inl.h"
#include "src/compiler/pipeline.h"
#include "test/unittests/compiler/backend/instruction-sequence-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

class MidTierRegisterAllocatorTest : public InstructionSequenceTest {
 public:
  void Allocate() {
    WireBlocks();
    Pipeline::AllocateRegistersForTesting(config(), sequence(), true, true);
  }
};

TEST_F(MidTierRegisterAllocatorTest, CanAllocateThreeRegisters) {
  // return p0 + p1;
  StartBlock();
  auto a_reg = Parameter();
  auto b_reg = Parameter();
  auto c_reg = EmitOI(Reg(1), Reg(a_reg, 1), Reg(b_reg));
  Return(c_reg);
  EndBlock(Last());

  Allocate();
}

TEST_F(MidTierRegisterAllocatorTest, CanAllocateFPRegisters) {
  StartBlock();
  TestOperand inputs[] = {
      Reg(FPParameter(kFloat64)), Reg(FPParameter(kFloat64)),
      Reg(FPParameter(kFloat32)), Reg(FPParameter(kFloat32)),
      Reg(FPParameter(kSimd128)), Reg(FPParameter(kSimd128))};
  VReg out1 = EmitOI(FPReg(1, kFloat64), arraysize(inputs), inputs);
  Return(out1);
  EndBlock(Last());

  Allocate();
}

TEST_F(MidTierRegisterAllocatorTest, MoveLotsOfConstants) {
  FLAG_trace_turbo = true;
  StartBlock();
  VReg constants[Register::kNumRegisters];
  for (size_t i = 0; i < arraysize(constants); ++i) {
    constants[i] = DefineConstant();
  }
  TestOperand call_ops[Register::kNumRegisters * 2];
  for (int i = 0; i < Register::kNumRegisters; ++i) {
    call_ops[i] = Reg(constants[i], i);
  }
  for (int i = 0; i < Register::kNumRegisters; ++i) {
    call_ops[i + Register::kNumRegisters] = Slot(constants[i], i);
  }
  EmitCall(Slot(-1), arraysize(call_ops), call_ops);
  EndBlock(Last());

  Allocate();
}

TEST_F(MidTierRegisterAllocatorTest, SplitBeforeInstruction) {
  const int kNumRegs = 6;
  SetNumRegs(kNumRegs, kNumRegs);

  StartBlock();

  // Stack parameters/spilled values.
  auto p_0 = Define(Slot(-1));
  auto p_1 = Define(Slot(-2));

  // Fill registers.
  VReg values[kNumRegs];
  for (size_t i = 0; i < arraysize(values); ++i) {
    values[i] = Define(Reg(static_cast<int>(i)));
  }

  // values[0] will be split in the second half of this instruction.
  // Models Intel mod instructions.
  EmitOI(Reg(0), Reg(p_0, 1), UniqueReg(p_1));
  EmitI(Reg(values[0], 0));
  EndBlock(Last());

  Allocate();
}

TEST_F(MidTierRegisterAllocatorTest, SplitBeforeInstruction2) {
  const int kNumRegs = 6;
  SetNumRegs(kNumRegs, kNumRegs);

  StartBlock();

  // Stack parameters/spilled values.
  auto p_0 = Define(Slot(-1));
  auto p_1 = Define(Slot(-2));

  // Fill registers.
  VReg values[kNumRegs];
  for (size_t i = 0; i < arraysize(values); ++i) {
    values[i] = Define(Reg(static_cast<int>(i)));
  }

  // values[0] and [1] will be split in the second half of this instruction.
  EmitOOI(Reg(0), Reg(1), Reg(p_0, 0), Reg(p_1, 1));
  EmitI(Reg(values[0]), Reg(values[1]));
  EndBlock(Last());

  Allocate();
}

TEST_F(MidTierRegisterAllocatorTest, SplitBeforeAndMove) {
  StartBlock();

  // Fill registers.
  VReg values[Register::kNumRegisters];
  for (size_t i = 0; i < arraysize(values); ++i) {
    if (i == 0 || i == 1) continue;  // Leave a hole for c_1 to take.
    values[i] = Define(Reg(static_cast<int>(i)));
  }

  auto c_0 = DefineConstant();
  auto c_1 = DefineConstant();

  EmitOI(Reg(1), Reg(c_0, 0), UniqueReg(c_1));

  // Use previous values to force c_1 to split before the previous instruction.
  for (size_t i = 0; i < arraysize(values); ++i) {
    if (i == 0 || i == 1) continue;
    EmitI(Reg(values[i], static_cast<int>(i)));
  }

  EndBlock(Last());

  Allocate();
}

TEST_F(MidTierRegisterAllocatorTest, SpillTwice) {
  StartBlock();
  auto p_0 = Parameter(Reg(1));
  EmitCall(Slot(-2), Unique(p_0), Reg(p_0, 1));
  EndBlock(Last());

  Allocate();
}

namespace {

enum class ParameterType { kFixedSlot, kSlot, kRegister, kFixedRegister };

const ParameterType kParameterTypes[] = {
    ParameterType::kFixedSlot, ParameterType::kSlot, ParameterType::kRegister,
    ParameterType::kFixedRegister};

class MidTierRegAllocSlotConstraintTest
    : public MidTierRegisterAllocatorTest,
      public ::testing::WithParamInterface<
          ::testing::tuple<ParameterType, int>> {
 public:
  static const int kMaxVariant = 5;

 protected:
  ParameterType parameter_type() const {
    return ::testing::get<0>(B::GetParam());
  }
  int variant() const { return ::testing::get<1>(B::GetParam()); }

 private:
  using B = ::testing::WithParamInterface<::testing::tuple<ParameterType, int>>;
};

}  // namespace

TEST_P(MidTierRegAllocSlotConstraintTest, SlotConstraint) {
  FLAG_trace_turbo = true;
  StartBlock();
  VReg p_0;
  switch (parameter_type()) {
    case ParameterType::kFixedSlot:
      p_0 = Parameter(Slot(-1));
      break;
    case ParameterType::kSlot:
      p_0 = Parameter(Slot(-1));
      break;
    case ParameterType::kRegister:
      p_0 = Parameter(Reg());
      break;
    case ParameterType::kFixedRegister:
      p_0 = Parameter(Reg(1));
      break;
  }
  switch (variant()) {
    case 0:
      EmitI(Slot(p_0), Reg(p_0));
      break;
    case 1:
      EmitI(Slot(p_0));
      break;
    case 2:
      EmitI(Reg(p_0));
      EmitI(Slot(p_0));
      break;
    case 3:
      EmitI(Slot(p_0));
      EmitI(Reg(p_0));
      break;
    case 4:
      EmitI(Slot(p_0, -1), Slot(p_0), Reg(p_0), Reg(p_0, 1));
      break;
    default:
      UNREACHABLE();
  }
  EndBlock(Last());

  Allocate();
}

INSTANTIATE_TEST_SUITE_P(
    MidTierRegisterAllocatorTest, MidTierRegAllocSlotConstraintTest,
    ::testing::Combine(
        ::testing::ValuesIn(kParameterTypes),
        ::testing::Range(0, MidTierRegAllocSlotConstraintTest::kMaxVariant)));

}  // namespace
}  // namespace compiler
}  // namespace internal
}  // namespace v8
