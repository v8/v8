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
};


std::ostream& operator<<(std::ostream& os, const DPI& dpi) {
  return os << dpi.constructor_name;
}


// ARM64 Logical instructions.
static const DPI kLogicalInstructions[] = {
    {&RawMachineAssembler::Word32And, "Word32And", kArm64And32},
    {&RawMachineAssembler::Word64And, "Word64And", kArm64And},
    {&RawMachineAssembler::Word32Or, "Word32Or", kArm64Or32},
    {&RawMachineAssembler::Word64Or, "Word64Or", kArm64Or},
    {&RawMachineAssembler::Word32Xor, "Word32Xor", kArm64Xor32},
    {&RawMachineAssembler::Word64Xor, "Word64Xor", kArm64Xor}};


// ARM64 Arithmetic instructions.
static const DPI kAddSubInstructions[] = {
    {&RawMachineAssembler::Int32Add, "Int32Add", kArm64Add32},
    {&RawMachineAssembler::Int64Add, "Int64Add", kArm64Add},
    {&RawMachineAssembler::Int32Sub, "Int32Sub", kArm64Sub32},
    {&RawMachineAssembler::Int64Sub, "Int64Sub", kArm64Sub}};


// ARM64 Add/Sub immediates.
// TODO(all): Test only a subset of the immediates, similar to what we do for
// arm. Unit tests should be really fast!
class AddSubImmediates V8_FINAL : public std::list<int32_t> {
 public:
  AddSubImmediates() {
    for (int32_t imm12 = 0; imm12 < 4096; ++imm12) {
      CHECK(Assembler::IsImmAddSub(imm12));
      CHECK(Assembler::IsImmAddSub(imm12 << 12));
      push_back(imm12);
      push_back(imm12 << 12);
    }
  }
};


// ARM64 Mul/Div instructions.
static const DPI kMulDivInstructions[] = {
    {&RawMachineAssembler::Int32Mul, "Int32Mul", kArm64Mul32},
    {&RawMachineAssembler::Int64Mul, "Int64Mul", kArm64Mul},
    {&RawMachineAssembler::Int32Div, "Int32Div", kArm64Idiv32},
    {&RawMachineAssembler::Int64Div, "Int64Div", kArm64Idiv},
    {&RawMachineAssembler::Int32UDiv, "Int32UDiv", kArm64Udiv32},
    {&RawMachineAssembler::Int64UDiv, "Int64UDiv", kArm64Udiv}};

}  // namespace


// TODO(all): Use TEST_P, see instruction-selector-arm-unittest.cc.
TEST_F(InstructionSelectorTest, LogicalWithParameter) {
  TRACED_FOREACH(DPI, dpi, kLogicalInstructions) {
    StreamBuilder m(this, kMachInt32, kMachInt32, kMachInt32);
    m.Return((m.*dpi.constructor)(m.Parameter(0), m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  }
}


// TODO(all): Use TEST_P, see instruction-selector-arm-unittest.cc.
TEST_F(InstructionSelectorTest, AddSubWithParameter) {
  TRACED_FOREACH(DPI, dpi, kAddSubInstructions) {
    StreamBuilder m(this, kMachInt32, kMachInt32, kMachInt32);
    m.Return((m.*dpi.constructor)(m.Parameter(0), m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  }
}


// TODO(all): Use TEST_P, see instruction-selector-arm-unittest.cc.
TEST_F(InstructionSelectorTest, AddSubWithImmediate) {
  AddSubImmediates immediates;
  TRACED_FOREACH(DPI, dpi, kAddSubInstructions) {
    for (AddSubImmediates::const_iterator j = immediates.begin();
         j != immediates.end(); ++j) {
      int32_t imm = *j;
      SCOPED_TRACE(::testing::Message() << "imm = " << imm);
      StreamBuilder m(this, kMachInt32, kMachInt32);
      m.Return((m.*dpi.constructor)(m.Parameter(0), m.Int32Constant(imm)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
      EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
    }
  }
}


// TODO(all): Use TEST_P, see instruction-selector-arm-unittest.cc.
TEST_F(InstructionSelectorTest, MulDivWithParameter) {
  TRACED_FOREACH(DPI, dpi, kMulDivInstructions) {
    StreamBuilder m(this, kMachInt32, kMachInt32, kMachInt32);
    m.Return((m.*dpi.constructor)(m.Parameter(0), m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  }
}


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
