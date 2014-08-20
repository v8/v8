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


// ARM64 logical immediates: contiguous set bits, rotated about a power of two
// sized block. The block is then duplicated across the word. Below is a random
// subset of the 32-bit immediates.
static const uint32_t kLogicalImmediates[] = {
    0x00000002, 0x00000003, 0x00000070, 0x00000080, 0x00000100, 0x000001c0,
    0x00000300, 0x000007e0, 0x00003ffc, 0x00007fc0, 0x0003c000, 0x0003f000,
    0x0003ffc0, 0x0003fff8, 0x0007ff00, 0x0007ffe0, 0x000e0000, 0x001e0000,
    0x001ffffc, 0x003f0000, 0x003f8000, 0x00780000, 0x007fc000, 0x00ff0000,
    0x01800000, 0x01800180, 0x01f801f8, 0x03fe0000, 0x03ffffc0, 0x03fffffc,
    0x06000000, 0x07fc0000, 0x07ffc000, 0x07ffffc0, 0x07ffffe0, 0x0ffe0ffe,
    0x0ffff800, 0x0ffffff0, 0x0fffffff, 0x18001800, 0x1f001f00, 0x1f801f80,
    0x30303030, 0x3ff03ff0, 0x3ff83ff8, 0x3fff0000, 0x3fff8000, 0x3fffffc0,
    0x70007000, 0x7f7f7f7f, 0x7fc00000, 0x7fffffc0, 0x8000001f, 0x800001ff,
    0x81818181, 0x9fff9fff, 0xc00007ff, 0xc0ffffff, 0xdddddddd, 0xe00001ff,
    0xe00003ff, 0xe007ffff, 0xefffefff, 0xf000003f, 0xf001f001, 0xf3fff3ff,
    0xf800001f, 0xf80fffff, 0xf87ff87f, 0xfbfbfbfb, 0xfc00001f, 0xfc0000ff,
    0xfc0001ff, 0xfc03fc03, 0xfe0001ff, 0xff000001, 0xff03ff03, 0xff800000,
    0xff800fff, 0xff801fff, 0xff87ffff, 0xffc0003f, 0xffc007ff, 0xffcfffcf,
    0xffe00003, 0xffe1ffff, 0xfff0001f, 0xfff07fff, 0xfff80007, 0xfff87fff,
    0xfffc00ff, 0xfffe07ff, 0xffff00ff, 0xffffc001, 0xfffff007, 0xfffff3ff,
    0xfffff807, 0xfffff9ff, 0xfffffc0f, 0xfffffeff};


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


// ARM64 shift instructions.
static const DPI kShiftInstructions[] = {
    {&RawMachineAssembler::Word32Shl, "Word32Shl", kArm64Shl32, kMachInt32},
    {&RawMachineAssembler::Word64Shl, "Word64Shl", kArm64Shl, kMachInt64},
    {&RawMachineAssembler::Word32Shr, "Word32Shr", kArm64Shr32, kMachInt32},
    {&RawMachineAssembler::Word64Shr, "Word64Shr", kArm64Shr, kMachInt64},
    {&RawMachineAssembler::Word32Sar, "Word32Sar", kArm64Sar32, kMachInt32},
    {&RawMachineAssembler::Word64Sar, "Word64Sar", kArm64Sar, kMachInt64},
    {&RawMachineAssembler::Word32Ror, "Word32Ror", kArm64Ror32, kMachInt32},
    {&RawMachineAssembler::Word64Ror, "Word64Ror", kArm64Ror, kMachInt64}};


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


TEST_P(InstructionSelectorLogicalTest, Immediate) {
  const DPI dpi = GetParam();
  const MachineType type = dpi.machine_type;
  // TODO(all): Add support for testing 64-bit immediates.
  if (type == kMachInt32) {
    // Immediate on the right.
    TRACED_FOREACH(int32_t, imm, kLogicalImmediates) {
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

    // Immediate on the left; all logical ops should commute.
    TRACED_FOREACH(int32_t, imm, kLogicalImmediates) {
      StreamBuilder m(this, type, type);
      m.Return((m.*dpi.constructor)(m.Int32Constant(imm), m.Parameter(0)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
      ASSERT_EQ(2U, s[0]->InputCount());
      EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }
  }
}


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
// Shift instructions.


typedef InstructionSelectorTestWithParam<DPI> InstructionSelectorShiftTest;

TEST_P(InstructionSelectorShiftTest, Parameter) {
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


TEST_P(InstructionSelectorShiftTest, Immediate) {
  const DPI dpi = GetParam();
  const MachineType type = dpi.machine_type;
  TRACED_FORRANGE(int32_t, imm, 0, (ElementSizeOf(type) * 8) - 1) {
    StreamBuilder m(this, type, type);
    m.Return((m.*dpi.constructor)(m.Parameter(0), m.Int32Constant(imm)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest, InstructionSelectorShiftTest,
                        ::testing::ValuesIn(kShiftInstructions));


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
