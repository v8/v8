// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>

#include "test/compiler-unittests/instruction-selector-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

typedef Node* (RawMachineAssembler::*Constructor)(Node*, Node*);

struct DPI {
  Constructor constructor;
  const char* constructor_name;
  ArchOpcode arch_opcode;
  MachineType machine_type;
};


std::ostream& operator<<(std::ostream& os, const DPI& dpi) {
  return os << dpi.constructor_name;
}


// ARM64 Logical instructions.
static const DPI kLogicalInstructions[] = {
    {&RawMachineAssembler::Word32And, "Word32And", kArm64And32, kMachInt32},
    {&RawMachineAssembler::Word64And, "Word64And", kArm64And, kMachInt64},
    {&RawMachineAssembler::Word32Or, "Word32Or", kArm64Or32, kMachInt32},
    {&RawMachineAssembler::Word64Or, "Word64Or", kArm64Or, kMachInt64},
    {&RawMachineAssembler::Word32Xor, "Word32Xor", kArm64Xor32, kMachInt32},
    {&RawMachineAssembler::Word64Xor, "Word64Xor", kArm64Xor, kMachInt64}};


// ARM64 Arithmetic instructions.
static const DPI kAddSubInstructions[] = {
    {&RawMachineAssembler::Int32Add, "Int32Add", kArm64Add32, kMachInt32},
    {&RawMachineAssembler::Int64Add, "Int64Add", kArm64Add, kMachInt64},
    {&RawMachineAssembler::Int32Sub, "Int32Sub", kArm64Sub32, kMachInt32},
    {&RawMachineAssembler::Int64Sub, "Int64Sub", kArm64Sub, kMachInt64}};


// ARM64 Add/Sub immediates: 12-bit immediate optionally shifted by 12.
// Below is a combination of a random subset and some edge values.
static const int32_t kAddSubImmediates[] = {
    0,        1,        69,       493,      599,      701,      719,
    768,      818,      842,      945,      1246,     1286,     1429,
    1669,     2171,     2179,     2182,     2254,     2334,     2338,
    2343,     2396,     2449,     2610,     2732,     2855,     2876,
    2944,     3377,     3458,     3475,     3476,     3540,     3574,
    3601,     3813,     3871,     3917,     4095,     4096,     16384,
    364544,   462848,   970752,   1523712,  1863680,  2363392,  3219456,
    3280896,  4247552,  4526080,  4575232,  4960256,  5505024,  5894144,
    6004736,  6193152,  6385664,  6795264,  7114752,  7233536,  7348224,
    7499776,  7573504,  7729152,  8634368,  8937472,  9465856,  10354688,
    10682368, 11059200, 11460608, 13168640, 13176832, 14336000, 15028224,
    15597568, 15892480, 16773120};


// ARM64 Mul/Div instructions.
static const DPI kMulDivInstructions[] = {
    {&RawMachineAssembler::Int32Mul, "Int32Mul", kArm64Mul32, kMachInt32},
    {&RawMachineAssembler::Int64Mul, "Int64Mul", kArm64Mul, kMachInt64},
    {&RawMachineAssembler::Int32Div, "Int32Div", kArm64Idiv32, kMachInt32},
    {&RawMachineAssembler::Int64Div, "Int64Div", kArm64Idiv, kMachInt64},
    {&RawMachineAssembler::Int32UDiv, "Int32UDiv", kArm64Udiv32, kMachInt32},
    {&RawMachineAssembler::Int64UDiv, "Int64UDiv", kArm64Udiv, kMachInt64}};

}  // namespace


// -----------------------------------------------------------------------------
// Logical instructions.


typedef InstructionSelectorTestWithParam<DPI> InstructionSelectorLogicalTest;

TEST_P(InstructionSelectorLogicalTest, Parameter) {
  const DPI dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return((m.*dpi.constructor)(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}


// TODO(all): add immediate tests.


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest, InstructionSelectorLogicalTest,
                        ::testing::ValuesIn(kLogicalInstructions));


// -----------------------------------------------------------------------------
// Add and Sub instructions.

typedef InstructionSelectorTestWithParam<DPI> InstructionSelectorAddSubTest;

TEST_P(InstructionSelectorAddSubTest, Parameter) {
  const DPI dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return((m.*dpi.constructor)(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}


TEST_P(InstructionSelectorAddSubTest, Immediate) {
  const DPI dpi = GetParam();
  const MachineType type = dpi.machine_type;
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, type, type);
    m.Return((m.*dpi.constructor)(m.Parameter(0), m.Int32Constant(imm)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest, InstructionSelectorAddSubTest,
                        ::testing::ValuesIn(kAddSubInstructions));


// -----------------------------------------------------------------------------
// Mul and Div instructions.


typedef InstructionSelectorTestWithParam<DPI> InstructionSelectorMulDivTest;


TEST_P(InstructionSelectorMulDivTest, Parameter) {
  const DPI dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return((m.*dpi.constructor)(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

INSTANTIATE_TEST_CASE_P(InstructionSelectorTest, InstructionSelectorMulDivTest,
                        ::testing::ValuesIn(kMulDivInstructions));


// -----------------------------------------------------------------------------
// Conversions.


TEST_F(InstructionSelectorTest, ChangeInt32ToInt64WithParameter) {
  StreamBuilder m(this, kMachInt64, kMachInt32);
  m.Return(m.ChangeInt32ToInt64(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Sxtw, s[0]->arch_opcode());
}


TEST_F(InstructionSelectorTest, ChangeUint32ToUint64WithParameter) {
  StreamBuilder m(this, kMachUint64, kMachUint32);
  m.Return(m.ChangeUint32ToUint64(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Mov32, s[0]->arch_opcode());
}


TEST_F(InstructionSelectorTest, TruncateInt64ToInt32WithParameter) {
  StreamBuilder m(this, kMachInt32, kMachInt64);
  m.Return(m.TruncateInt64ToInt32(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Mov32, s[0]->arch_opcode());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
