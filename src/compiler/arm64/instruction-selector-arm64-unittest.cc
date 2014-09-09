// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>

#include "src/compiler/instruction-selector-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

typedef RawMachineAssembler::Label MLabel;

template <typename T>
struct MachInst {
  T constructor;
  const char* constructor_name;
  ArchOpcode arch_opcode;
  MachineType machine_type;
};

typedef MachInst<Node* (RawMachineAssembler::*)(Node*)> MachInst1;
typedef MachInst<Node* (RawMachineAssembler::*)(Node*, Node*)> MachInst2;


template <typename T>
std::ostream& operator<<(std::ostream& os, const MachInst<T>& mi) {
  return os << mi.constructor_name;
}


// ARM64 logical instructions.
static const MachInst2 kLogicalInstructions[] = {
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


// ARM64 arithmetic instructions.
static const MachInst2 kAddSubInstructions[] = {
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


// ARM64 flag setting data processing instructions.
static const MachInst2 kDPFlagSetInstructions[] = {
    {&RawMachineAssembler::Word32And, "Word32And", kArm64Tst32, kMachInt32},
    {&RawMachineAssembler::Int32Add, "Int32Add", kArm64Cmn32, kMachInt32},
    {&RawMachineAssembler::Int32Sub, "Int32Sub", kArm64Cmp32, kMachInt32}};


// ARM64 arithmetic with overflow instructions.
static const MachInst2 kOvfAddSubInstructions[] = {
    {&RawMachineAssembler::Int32AddWithOverflow, "Int32AddWithOverflow",
     kArm64Add32, kMachInt32},
    {&RawMachineAssembler::Int32SubWithOverflow, "Int32SubWithOverflow",
     kArm64Sub32, kMachInt32}};


// ARM64 shift instructions.
static const MachInst2 kShiftInstructions[] = {
    {&RawMachineAssembler::Word32Shl, "Word32Shl", kArm64Shl32, kMachInt32},
    {&RawMachineAssembler::Word64Shl, "Word64Shl", kArm64Shl, kMachInt64},
    {&RawMachineAssembler::Word32Shr, "Word32Shr", kArm64Shr32, kMachInt32},
    {&RawMachineAssembler::Word64Shr, "Word64Shr", kArm64Shr, kMachInt64},
    {&RawMachineAssembler::Word32Sar, "Word32Sar", kArm64Sar32, kMachInt32},
    {&RawMachineAssembler::Word64Sar, "Word64Sar", kArm64Sar, kMachInt64},
    {&RawMachineAssembler::Word32Ror, "Word32Ror", kArm64Ror32, kMachInt32},
    {&RawMachineAssembler::Word64Ror, "Word64Ror", kArm64Ror, kMachInt64}};


// ARM64 Mul/Div instructions.
static const MachInst2 kMulDivInstructions[] = {
    {&RawMachineAssembler::Int32Mul, "Int32Mul", kArm64Mul32, kMachInt32},
    {&RawMachineAssembler::Int64Mul, "Int64Mul", kArm64Mul, kMachInt64},
    {&RawMachineAssembler::Int32Div, "Int32Div", kArm64Idiv32, kMachInt32},
    {&RawMachineAssembler::Int64Div, "Int64Div", kArm64Idiv, kMachInt64},
    {&RawMachineAssembler::Int32UDiv, "Int32UDiv", kArm64Udiv32, kMachInt32},
    {&RawMachineAssembler::Int64UDiv, "Int64UDiv", kArm64Udiv, kMachInt64}};


// ARM64 FP arithmetic instructions.
static const MachInst2 kFPArithInstructions[] = {
    {&RawMachineAssembler::Float64Add, "Float64Add", kArm64Float64Add,
     kMachFloat64},
    {&RawMachineAssembler::Float64Sub, "Float64Sub", kArm64Float64Sub,
     kMachFloat64},
    {&RawMachineAssembler::Float64Mul, "Float64Mul", kArm64Float64Mul,
     kMachFloat64},
    {&RawMachineAssembler::Float64Div, "Float64Div", kArm64Float64Div,
     kMachFloat64}};


struct FPCmp {
  MachInst2 mi;
  FlagsCondition cond;
};


std::ostream& operator<<(std::ostream& os, const FPCmp& cmp) {
  return os << cmp.mi;
}


// ARM64 FP comparison instructions.
static const FPCmp kFPCmpInstructions[] = {
    {{&RawMachineAssembler::Float64Equal, "Float64Equal", kArm64Float64Cmp,
      kMachFloat64},
     kUnorderedEqual},
    {{&RawMachineAssembler::Float64LessThan, "Float64LessThan",
      kArm64Float64Cmp, kMachFloat64},
     kUnorderedLessThan},
    {{&RawMachineAssembler::Float64LessThanOrEqual, "Float64LessThanOrEqual",
      kArm64Float64Cmp, kMachFloat64},
     kUnorderedLessThanOrEqual}};


struct Conversion {
  // The machine_type field in MachInst1 represents the destination type.
  MachInst1 mi;
  MachineType src_machine_type;
};


std::ostream& operator<<(std::ostream& os, const Conversion& conv) {
  return os << conv.mi;
}


// ARM64 type conversion instructions.
static const Conversion kConversionInstructions[] = {
    {{&RawMachineAssembler::ChangeInt32ToInt64, "ChangeInt32ToInt64",
      kArm64Sxtw, kMachInt64},
     kMachInt32},
    {{&RawMachineAssembler::ChangeUint32ToUint64, "ChangeUint32ToUint64",
      kArm64Mov32, kMachUint64},
     kMachUint32},
    {{&RawMachineAssembler::TruncateInt64ToInt32, "TruncateInt64ToInt32",
      kArm64Mov32, kMachInt32},
     kMachInt64},
    {{&RawMachineAssembler::ChangeInt32ToFloat64, "ChangeInt32ToFloat64",
      kArm64Int32ToFloat64, kMachFloat64},
     kMachInt32},
    {{&RawMachineAssembler::ChangeUint32ToFloat64, "ChangeUint32ToFloat64",
      kArm64Uint32ToFloat64, kMachFloat64},
     kMachUint32},
    {{&RawMachineAssembler::ChangeFloat64ToInt32, "ChangeFloat64ToInt32",
      kArm64Float64ToInt32, kMachInt32},
     kMachFloat64},
    {{&RawMachineAssembler::ChangeFloat64ToUint32, "ChangeFloat64ToUint32",
      kArm64Float64ToUint32, kMachUint32},
     kMachFloat64}};

}  // namespace


// -----------------------------------------------------------------------------
// Logical instructions.


typedef InstructionSelectorTestWithParam<MachInst2>
    InstructionSelectorLogicalTest;


TEST_P(InstructionSelectorLogicalTest, Parameter) {
  const MachInst2 dpi = GetParam();
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
  const MachInst2 dpi = GetParam();
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

typedef InstructionSelectorTestWithParam<MachInst2>
    InstructionSelectorAddSubTest;


TEST_P(InstructionSelectorAddSubTest, Parameter) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return((m.*dpi.constructor)(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}


TEST_P(InstructionSelectorAddSubTest, ImmediateOnRight) {
  const MachInst2 dpi = GetParam();
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


TEST_P(InstructionSelectorAddSubTest, ImmediateOnLeft) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;

  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, type, type);
    m.Return((m.*dpi.constructor)(m.Int32Constant(imm), m.Parameter(0)));
    Stream s = m.Build();

    // Add can support an immediate on the left by commuting, but Sub can't
    // commute. We test zero-on-left Sub later.
    if (strstr(dpi.constructor_name, "Add") != NULL) {
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
      ASSERT_EQ(2U, s[0]->InputCount());
      EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }
  }
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest, InstructionSelectorAddSubTest,
                        ::testing::ValuesIn(kAddSubInstructions));


TEST_F(InstructionSelectorTest, SubZeroOnLeft) {
  // Subtraction with zero on the left maps to Neg.
  {
    // 32-bit subtract.
    StreamBuilder m(this, kMachInt32, kMachInt32, kMachInt32);
    m.Return(m.Int32Sub(m.Int32Constant(0), m.Parameter(0)));
    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Neg32, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    // 64-bit subtract.
    StreamBuilder m(this, kMachInt64, kMachInt64, kMachInt64);
    m.Return(m.Int64Sub(m.Int64Constant(0), m.Parameter(0)));
    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Neg, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}


// -----------------------------------------------------------------------------
// Data processing controlled branches.


typedef InstructionSelectorTestWithParam<MachInst2>
    InstructionSelectorDPFlagSetTest;


TEST_P(InstructionSelectorDPFlagSetTest, BranchWithParameters) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  MLabel a, b;
  m.Branch((m.*dpi.constructor)(m.Parameter(0), m.Parameter(1)), &a, &b);
  m.Bind(&a);
  m.Return(m.Int32Constant(1));
  m.Bind(&b);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
  EXPECT_EQ(kNotEqual, s[0]->flags_condition());
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest,
                        InstructionSelectorDPFlagSetTest,
                        ::testing::ValuesIn(kDPFlagSetInstructions));


TEST_F(InstructionSelectorTest, AndBranchWithImmediateOnRight) {
  TRACED_FOREACH(int32_t, imm, kLogicalImmediates) {
    StreamBuilder m(this, kMachInt32, kMachInt32);
    MLabel a, b;
    m.Branch(m.Word32And(m.Parameter(0), m.Int32Constant(imm)), &a, &b);
    m.Bind(&a);
    m.Return(m.Int32Constant(1));
    m.Bind(&b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}


TEST_F(InstructionSelectorTest, AddBranchWithImmediateOnRight) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, kMachInt32, kMachInt32);
    MLabel a, b;
    m.Branch(m.Int32Add(m.Parameter(0), m.Int32Constant(imm)), &a, &b);
    m.Bind(&a);
    m.Return(m.Int32Constant(1));
    m.Bind(&b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmn32, s[0]->arch_opcode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}


TEST_F(InstructionSelectorTest, SubBranchWithImmediateOnRight) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, kMachInt32, kMachInt32);
    MLabel a, b;
    m.Branch(m.Int32Sub(m.Parameter(0), m.Int32Constant(imm)), &a, &b);
    m.Bind(&a);
    m.Return(m.Int32Constant(1));
    m.Bind(&b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}


TEST_F(InstructionSelectorTest, AndBranchWithImmediateOnLeft) {
  TRACED_FOREACH(int32_t, imm, kLogicalImmediates) {
    StreamBuilder m(this, kMachInt32, kMachInt32);
    MLabel a, b;
    m.Branch(m.Word32And(m.Int32Constant(imm), m.Parameter(0)), &a, &b);
    m.Bind(&a);
    m.Return(m.Int32Constant(1));
    m.Bind(&b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
    ASSERT_LE(1U, s[0]->InputCount());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}


TEST_F(InstructionSelectorTest, AddBranchWithImmediateOnLeft) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, kMachInt32, kMachInt32);
    MLabel a, b;
    m.Branch(m.Int32Add(m.Int32Constant(imm), m.Parameter(0)), &a, &b);
    m.Bind(&a);
    m.Return(m.Int32Constant(1));
    m.Bind(&b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmn32, s[0]->arch_opcode());
    ASSERT_LE(1U, s[0]->InputCount());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}


// -----------------------------------------------------------------------------
// Add and subtract instructions with overflow.


typedef InstructionSelectorTestWithParam<MachInst2>
    InstructionSelectorOvfAddSubTest;


TEST_P(InstructionSelectorOvfAddSubTest, OvfParameter) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(
      m.Projection(1, (m.*dpi.constructor)(m.Parameter(0), m.Parameter(1))));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_LE(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(kOverflow, s[0]->flags_condition());
}


TEST_P(InstructionSelectorOvfAddSubTest, OvfImmediateOnRight) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, type, type);
    m.Return(m.Projection(
        1, (m.*dpi.constructor)(m.Parameter(0), m.Int32Constant(imm))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}


TEST_P(InstructionSelectorOvfAddSubTest, ValParameter) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(
      m.Projection(0, (m.*dpi.constructor)(m.Parameter(0), m.Parameter(1))));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_LE(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_none, s[0]->flags_mode());
}


TEST_P(InstructionSelectorOvfAddSubTest, ValImmediateOnRight) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, type, type);
    m.Return(m.Projection(
        0, (m.*dpi.constructor)(m.Parameter(0), m.Int32Constant(imm))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
}


TEST_P(InstructionSelectorOvfAddSubTest, BothParameter) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  Node* n = (m.*dpi.constructor)(m.Parameter(0), m.Parameter(1));
  m.Return(m.Word32Equal(m.Projection(0, n), m.Projection(1, n)));
  Stream s = m.Build();
  ASSERT_LE(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(2U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(kOverflow, s[0]->flags_condition());
}


TEST_P(InstructionSelectorOvfAddSubTest, BothImmediateOnRight) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, type, type);
    Node* n = (m.*dpi.constructor)(m.Parameter(0), m.Int32Constant(imm));
    m.Return(m.Word32Equal(m.Projection(0, n), m.Projection(1, n)));
    Stream s = m.Build();
    ASSERT_LE(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(2U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}


TEST_P(InstructionSelectorOvfAddSubTest, BranchWithParameters) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  MLabel a, b;
  Node* n = (m.*dpi.constructor)(m.Parameter(0), m.Parameter(1));
  m.Branch(m.Projection(1, n), &a, &b);
  m.Bind(&a);
  m.Return(m.Int32Constant(0));
  m.Bind(&b);
  m.Return(m.Projection(0, n));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
  EXPECT_EQ(kOverflow, s[0]->flags_condition());
}


TEST_P(InstructionSelectorOvfAddSubTest, BranchWithImmediateOnRight) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, type, type);
    MLabel a, b;
    Node* n = (m.*dpi.constructor)(m.Parameter(0), m.Int32Constant(imm));
    m.Branch(m.Projection(1, n), &a, &b);
    m.Bind(&a);
    m.Return(m.Int32Constant(0));
    m.Bind(&b);
    m.Return(m.Projection(0, n));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest,
                        InstructionSelectorOvfAddSubTest,
                        ::testing::ValuesIn(kOvfAddSubInstructions));


TEST_F(InstructionSelectorTest, OvfFlagAddImmediateOnLeft) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, kMachInt32, kMachInt32);
    m.Return(m.Projection(
        1, m.Int32AddWithOverflow(m.Int32Constant(imm), m.Parameter(0))));
    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}


TEST_F(InstructionSelectorTest, OvfValAddImmediateOnLeft) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, kMachInt32, kMachInt32);
    m.Return(m.Projection(
        0, m.Int32AddWithOverflow(m.Int32Constant(imm), m.Parameter(0))));
    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
}


TEST_F(InstructionSelectorTest, OvfBothAddImmediateOnLeft) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, kMachInt32, kMachInt32);
    Node* n = m.Int32AddWithOverflow(m.Int32Constant(imm), m.Parameter(0));
    m.Return(m.Word32Equal(m.Projection(0, n), m.Projection(1, n)));
    Stream s = m.Build();

    ASSERT_LE(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(2U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}


TEST_F(InstructionSelectorTest, OvfBranchWithImmediateOnLeft) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, kMachInt32, kMachInt32);
    MLabel a, b;
    Node* n = m.Int32AddWithOverflow(m.Int32Constant(imm), m.Parameter(0));
    m.Branch(m.Projection(1, n), &a, &b);
    m.Bind(&a);
    m.Return(m.Int32Constant(0));
    m.Bind(&b);
    m.Return(m.Projection(0, n));
    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    ASSERT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}


// -----------------------------------------------------------------------------
// Shift instructions.


typedef InstructionSelectorTestWithParam<MachInst2>
    InstructionSelectorShiftTest;


TEST_P(InstructionSelectorShiftTest, Parameter) {
  const MachInst2 dpi = GetParam();
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
  const MachInst2 dpi = GetParam();
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


typedef InstructionSelectorTestWithParam<MachInst2>
    InstructionSelectorMulDivTest;


TEST_P(InstructionSelectorMulDivTest, Parameter) {
  const MachInst2 dpi = GetParam();
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
// Floating point instructions.

typedef InstructionSelectorTestWithParam<MachInst2>
    InstructionSelectorFPArithTest;


TEST_P(InstructionSelectorFPArithTest, Parameter) {
  const MachInst2 fpa = GetParam();
  StreamBuilder m(this, fpa.machine_type, fpa.machine_type, fpa.machine_type);
  m.Return((m.*fpa.constructor)(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(fpa.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest, InstructionSelectorFPArithTest,
                        ::testing::ValuesIn(kFPArithInstructions));


typedef InstructionSelectorTestWithParam<FPCmp> InstructionSelectorFPCmpTest;


TEST_P(InstructionSelectorFPCmpTest, Parameter) {
  const FPCmp cmp = GetParam();
  StreamBuilder m(this, kMachInt32, cmp.mi.machine_type, cmp.mi.machine_type);
  m.Return((m.*cmp.mi.constructor)(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(cmp.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.cond, s[0]->flags_condition());
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest, InstructionSelectorFPCmpTest,
                        ::testing::ValuesIn(kFPCmpInstructions));


// -----------------------------------------------------------------------------
// Conversions.

typedef InstructionSelectorTestWithParam<Conversion>
    InstructionSelectorConversionTest;


TEST_P(InstructionSelectorConversionTest, Parameter) {
  const Conversion conv = GetParam();
  StreamBuilder m(this, conv.mi.machine_type, conv.src_machine_type);
  m.Return((m.*conv.mi.constructor)(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(conv.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest,
                        InstructionSelectorConversionTest,
                        ::testing::ValuesIn(kConversionInstructions));


// -----------------------------------------------------------------------------
// Memory access instructions.


namespace {

struct MemoryAccess {
  MachineType type;
  ArchOpcode ldr_opcode;
  ArchOpcode str_opcode;
};


std::ostream& operator<<(std::ostream& os, const MemoryAccess& memacc) {
  OStringStream ost;
  ost << memacc.type;
  return os << ost.c_str();
}

}  // namespace


static const MemoryAccess kMemoryAccesses[] = {
    {kMachInt8, kArm64Ldrsb, kArm64Strb},
    {kMachUint8, kArm64Ldrb, kArm64Strb},
    {kMachInt16, kArm64Ldrsh, kArm64Strh},
    {kMachUint16, kArm64Ldrh, kArm64Strh},
    {kMachInt32, kArm64LdrW, kArm64StrW},
    {kMachUint32, kArm64LdrW, kArm64StrW},
    {kMachInt64, kArm64Ldr, kArm64Str},
    {kMachUint64, kArm64Ldr, kArm64Str},
    {kMachFloat32, kArm64LdrS, kArm64StrS},
    {kMachFloat64, kArm64LdrD, kArm64StrD}};


typedef InstructionSelectorTestWithParam<MemoryAccess>
    InstructionSelectorMemoryAccessTest;


TEST_P(InstructionSelectorMemoryAccessTest, LoadWithParameters) {
  const MemoryAccess memacc = GetParam();
  StreamBuilder m(this, memacc.type, kMachPtr, kMachInt32);
  m.Return(m.Load(memacc.type, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(memacc.ldr_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}


TEST_P(InstructionSelectorMemoryAccessTest, StoreWithParameters) {
  const MemoryAccess memacc = GetParam();
  StreamBuilder m(this, kMachInt32, kMachPtr, kMachInt32, memacc.type);
  m.Store(memacc.type, m.Parameter(0), m.Parameter(1), m.Parameter(2));
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(memacc.str_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
  EXPECT_EQ(3U, s[0]->InputCount());
  EXPECT_EQ(0U, s[0]->OutputCount());
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest,
                        InstructionSelectorMemoryAccessTest,
                        ::testing::ValuesIn(kMemoryAccesses));

}  // namespace compiler
}  // namespace internal
}  // namespace v8
