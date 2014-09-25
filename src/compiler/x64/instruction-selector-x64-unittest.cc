// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/instruction-selector-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

// -----------------------------------------------------------------------------
// Conversions.


TEST_F(InstructionSelectorTest, ChangeFloat32ToFloat64WithParameter) {
  StreamBuilder m(this, kMachFloat32, kMachFloat64);
  m.Return(m.ChangeFloat32ToFloat64(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kSSECvtss2sd, s[0]->arch_opcode());
  EXPECT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}


TEST_F(InstructionSelectorTest, ChangeInt32ToInt64WithParameter) {
  StreamBuilder m(this, kMachInt64, kMachInt32);
  m.Return(m.ChangeInt32ToInt64(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kX64Movsxlq, s[0]->arch_opcode());
}


TEST_F(InstructionSelectorTest, ChangeUint32ToUint64WithParameter) {
  StreamBuilder m(this, kMachUint64, kMachUint32);
  m.Return(m.ChangeUint32ToUint64(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kX64Movl, s[0]->arch_opcode());
}


TEST_F(InstructionSelectorTest, TruncateFloat64ToFloat32WithParameter) {
  StreamBuilder m(this, kMachFloat64, kMachFloat32);
  m.Return(m.TruncateFloat64ToFloat32(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kSSECvtsd2ss, s[0]->arch_opcode());
  EXPECT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}


TEST_F(InstructionSelectorTest, TruncateInt64ToInt32WithParameter) {
  StreamBuilder m(this, kMachInt32, kMachInt64);
  m.Return(m.TruncateInt64ToInt32(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kX64Movl, s[0]->arch_opcode());
}


// -----------------------------------------------------------------------------
// Better left operand for commutative binops

TEST_F(InstructionSelectorTest, BetterLeftOperandTestAddBinop) {
  StreamBuilder m(this, kMachInt32, kMachInt32, kMachInt32);
  Node* param1 = m.Parameter(0);
  Node* param2 = m.Parameter(1);
  Node* add = m.Int32Add(param1, param2);
  m.Return(m.Int32Add(add, param1));
  Stream s = m.Build();
  ASSERT_EQ(2U, s.size());
  EXPECT_EQ(kX64Add32, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  ASSERT_TRUE(s[0]->InputAt(0)->IsUnallocated());
  EXPECT_EQ(param2->id(), s.ToVreg(s[0]->InputAt(0)));
}


TEST_F(InstructionSelectorTest, BetterLeftOperandTestMulBinop) {
  StreamBuilder m(this, kMachInt32, kMachInt32, kMachInt32);
  Node* param1 = m.Parameter(0);
  Node* param2 = m.Parameter(1);
  Node* mul = m.Int32Mul(param1, param2);
  m.Return(m.Int32Mul(mul, param1));
  Stream s = m.Build();
  ASSERT_EQ(2U, s.size());
  EXPECT_EQ(kX64Imul32, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  ASSERT_TRUE(s[0]->InputAt(0)->IsUnallocated());
  EXPECT_EQ(param2->id(), s.ToVreg(s[0]->InputAt(0)));
}


// -----------------------------------------------------------------------------
// Loads and stores

namespace {

struct MemoryAccess {
  MachineType type;
  ArchOpcode load_opcode;
  ArchOpcode store_opcode;
};


std::ostream& operator<<(std::ostream& os, const MemoryAccess& memacc) {
  OStringStream ost;
  ost << memacc.type;
  return os << ost.c_str();
}


static const MemoryAccess kMemoryAccesses[] = {
    {kMachInt8, kX64Movsxbl, kX64Movb},
    {kMachUint8, kX64Movzxbl, kX64Movb},
    {kMachInt16, kX64Movsxwl, kX64Movw},
    {kMachUint16, kX64Movzxwl, kX64Movw},
    {kMachInt32, kX64Movl, kX64Movl},
    {kMachUint32, kX64Movl, kX64Movl},
    {kMachInt64, kX64Movq, kX64Movq},
    {kMachUint64, kX64Movq, kX64Movq},
    {kMachFloat32, kX64Movss, kX64Movss},
    {kMachFloat64, kX64Movsd, kX64Movsd}};

}  // namespace


typedef InstructionSelectorTestWithParam<MemoryAccess>
    InstructionSelectorMemoryAccessTest;


TEST_P(InstructionSelectorMemoryAccessTest, LoadWithParameters) {
  const MemoryAccess memacc = GetParam();
  StreamBuilder m(this, memacc.type, kMachPtr, kMachInt32);
  m.Return(m.Load(memacc.type, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(memacc.load_opcode, s[0]->arch_opcode());
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
  EXPECT_EQ(memacc.store_opcode, s[0]->arch_opcode());
  EXPECT_EQ(3U, s[0]->InputCount());
  EXPECT_EQ(0U, s[0]->OutputCount());
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest,
                        InstructionSelectorMemoryAccessTest,
                        ::testing::ValuesIn(kMemoryAccesses));

}  // namespace compiler
}  // namespace internal
}  // namespace v8
