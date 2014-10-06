// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/compiler/instruction-selector-unittest.h"

#include "src/compiler/node-matchers.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

// Immediates (random subset).
static const int32_t kImmediates[] = {
    kMinInt, -42, -1, 0,  1,  2,    3,      4,          5,
    6,       7,   8,  16, 42, 0xff, 0xffff, 0x0f0f0f0f, kMaxInt};

}  // namespace


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


TEST_F(InstructionSelectorTest, ChangeUint32ToFloat64WithParameter) {
  StreamBuilder m(this, kMachFloat64, kMachUint32);
  m.Return(m.ChangeUint32ToFloat64(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kSSEUint32ToFloat64, s[0]->arch_opcode());
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
  EXPECT_EQ(s.ToVreg(param2), s.ToVreg(s[0]->InputAt(0)));
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
  EXPECT_EQ(s.ToVreg(param2), s.ToVreg(s[0]->InputAt(0)));
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
  return os << memacc.type;
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

// -----------------------------------------------------------------------------
// AddressingMode for loads and stores.

class AddressingModeUnitTest : public InstructionSelectorTest {
 public:
  AddressingModeUnitTest() : m(NULL) { Reset(); }
  ~AddressingModeUnitTest() { delete m; }

  void Run(Node* base, Node* index, AddressingMode mode) {
    Node* load = m->Load(kMachInt32, base, index);
    m->Store(kMachInt32, base, index, load);
    m->Return(m->Int32Constant(0));
    Stream s = m->Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(mode, s[0]->addressing_mode());
    EXPECT_EQ(mode, s[1]->addressing_mode());
  }

  Node* zero;
  Node* null_ptr;
  Node* non_zero;
  Node* base_reg;   // opaque value to generate base as register
  Node* index_reg;  // opaque value to generate index as register
  Node* scales[arraysize(ScaleFactorMatcher::kMatchedFactors)];
  StreamBuilder* m;

  void Reset() {
    delete m;
    m = new StreamBuilder(this, kMachInt32, kMachInt32, kMachInt32);
    zero = m->Int32Constant(0);
    null_ptr = m->Int64Constant(0);
    non_zero = m->Int32Constant(127);
    base_reg = m->Parameter(0);
    index_reg = m->Parameter(0);
    for (size_t i = 0; i < arraysize(ScaleFactorMatcher::kMatchedFactors);
         ++i) {
      scales[i] = m->Int32Constant(ScaleFactorMatcher::kMatchedFactors[i]);
    }
  }
};


TEST_F(AddressingModeUnitTest, AddressingMode_MR) {
  Node* base = base_reg;
  Node* index = zero;
  Run(base, index, kMode_MR);
}


TEST_F(AddressingModeUnitTest, AddressingMode_MRI) {
  Node* base = base_reg;
  Node* index = non_zero;
  Run(base, index, kMode_MRI);
}


TEST_F(AddressingModeUnitTest, AddressingMode_MR1) {
  Node* base = base_reg;
  Node* index = index_reg;
  Run(base, index, kMode_MR1);
}


TEST_F(AddressingModeUnitTest, AddressingMode_MRN) {
  AddressingMode expected[] = {kMode_MR1, kMode_MR2, kMode_MR4, kMode_MR8};
  for (size_t i = 0; i < arraysize(scales); ++i) {
    Reset();
    Node* base = base_reg;
    Node* index = m->Int32Mul(index_reg, scales[i]);
    Run(base, index, expected[i]);
  }
}


TEST_F(AddressingModeUnitTest, AddressingMode_MR1I) {
  Node* base = base_reg;
  Node* index = m->Int32Add(index_reg, non_zero);
  Run(base, index, kMode_MR1I);
}


TEST_F(AddressingModeUnitTest, AddressingMode_MRNI) {
  AddressingMode expected[] = {kMode_MR1I, kMode_MR2I, kMode_MR4I, kMode_MR8I};
  for (size_t i = 0; i < arraysize(scales); ++i) {
    Reset();
    Node* base = base_reg;
    Node* index = m->Int32Add(m->Int32Mul(index_reg, scales[i]), non_zero);
    Run(base, index, expected[i]);
  }
}


TEST_F(AddressingModeUnitTest, AddressingMode_M1) {
  Node* base = null_ptr;
  Node* index = index_reg;
  Run(base, index, kMode_M1);
}


TEST_F(AddressingModeUnitTest, AddressingMode_MN) {
  AddressingMode expected[] = {kMode_M1, kMode_M2, kMode_M4, kMode_M8};
  for (size_t i = 0; i < arraysize(scales); ++i) {
    Reset();
    Node* base = null_ptr;
    Node* index = m->Int32Mul(index_reg, scales[i]);
    Run(base, index, expected[i]);
  }
}


TEST_F(AddressingModeUnitTest, AddressingMode_M1I) {
  Node* base = null_ptr;
  Node* index = m->Int32Add(index_reg, non_zero);
  Run(base, index, kMode_M1I);
}


TEST_F(AddressingModeUnitTest, AddressingMode_MNI) {
  AddressingMode expected[] = {kMode_M1I, kMode_M2I, kMode_M4I, kMode_M8I};
  for (size_t i = 0; i < arraysize(scales); ++i) {
    Reset();
    Node* base = null_ptr;
    Node* index = m->Int32Add(m->Int32Mul(index_reg, scales[i]), non_zero);
    Run(base, index, expected[i]);
  }
}


// -----------------------------------------------------------------------------
// Multiplication.

namespace {

struct MultParam {
  int value;
  bool lea_expected;
  AddressingMode addressing_mode;
};


std::ostream& operator<<(std::ostream& os, const MultParam& m) {
  return os << m.value << "." << m.lea_expected << "." << m.addressing_mode;
}


const MultParam kMultParams[] = {{-1, false, kMode_None},
                                 {0, false, kMode_None},
                                 {1, true, kMode_M1},
                                 {2, true, kMode_M2},
                                 {3, true, kMode_MR2},
                                 {4, true, kMode_M4},
                                 {5, true, kMode_MR4},
                                 {6, false, kMode_None},
                                 {7, false, kMode_None},
                                 {8, true, kMode_M8},
                                 {9, true, kMode_MR8},
                                 {10, false, kMode_None},
                                 {11, false, kMode_None}};

}  // namespace


typedef InstructionSelectorTestWithParam<MultParam> InstructionSelectorMultTest;


static unsigned InputCountForLea(AddressingMode mode) {
  switch (mode) {
    case kMode_MR1I:
    case kMode_MR2I:
    case kMode_MR4I:
    case kMode_MR8I:
      return 3U;
    case kMode_M1I:
    case kMode_M2I:
    case kMode_M4I:
    case kMode_M8I:
      return 2U;
    case kMode_MR1:
    case kMode_MR2:
    case kMode_MR4:
    case kMode_MR8:
      return 2U;
    case kMode_M1:
    case kMode_M2:
    case kMode_M4:
    case kMode_M8:
      return 1U;
    default:
      UNREACHABLE();
      return 0U;
  }
}


static AddressingMode AddressingModeForAddMult(const MultParam& m) {
  switch (m.addressing_mode) {
    case kMode_MR1:
      return kMode_MR1I;
    case kMode_MR2:
      return kMode_MR2I;
    case kMode_MR4:
      return kMode_MR4I;
    case kMode_MR8:
      return kMode_MR8I;
    case kMode_M1:
      return kMode_M1I;
    case kMode_M2:
      return kMode_M2I;
    case kMode_M4:
      return kMode_M4I;
    case kMode_M8:
      return kMode_M8I;
    default:
      UNREACHABLE();
      return kMode_None;
  }
}


TEST_P(InstructionSelectorMultTest, Mult32) {
  const MultParam m_param = GetParam();
  StreamBuilder m(this, kMachInt32, kMachInt32);
  Node* param = m.Parameter(0);
  Node* mult = m.Int32Mul(param, m.Int32Constant(m_param.value));
  m.Return(mult);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(m_param.addressing_mode, s[0]->addressing_mode());
  if (m_param.lea_expected) {
    EXPECT_EQ(kX64Lea32, s[0]->arch_opcode());
    ASSERT_EQ(InputCountForLea(s[0]->addressing_mode()), s[0]->InputCount());
  } else {
    EXPECT_EQ(kX64Imul32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
  }
  EXPECT_EQ(s.ToVreg(param), s.ToVreg(s[0]->InputAt(0)));
}


TEST_P(InstructionSelectorMultTest, Mult64) {
  const MultParam m_param = GetParam();
  StreamBuilder m(this, kMachInt64, kMachInt64);
  Node* param = m.Parameter(0);
  Node* mult = m.Int64Mul(param, m.Int64Constant(m_param.value));
  m.Return(mult);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(m_param.addressing_mode, s[0]->addressing_mode());
  if (m_param.lea_expected) {
    EXPECT_EQ(kX64Lea, s[0]->arch_opcode());
    ASSERT_EQ(InputCountForLea(s[0]->addressing_mode()), s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(param), s.ToVreg(s[0]->InputAt(0)));
  } else {
    EXPECT_EQ(kX64Imul, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    // TODO(dcarney): why is this happening?
    EXPECT_EQ(s.ToVreg(param), s.ToVreg(s[0]->InputAt(1)));
  }
}


TEST_P(InstructionSelectorMultTest, MultAdd32) {
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    const MultParam m_param = GetParam();
    StreamBuilder m(this, kMachInt32, kMachInt32);
    Node* param = m.Parameter(0);
    Node* mult = m.Int32Add(m.Int32Mul(param, m.Int32Constant(m_param.value)),
                            m.Int32Constant(imm));
    m.Return(mult);
    Stream s = m.Build();
    if (m_param.lea_expected) {
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kX64Lea32, s[0]->arch_opcode());
      EXPECT_EQ(AddressingModeForAddMult(m_param), s[0]->addressing_mode());
      unsigned input_count = InputCountForLea(s[0]->addressing_mode());
      ASSERT_EQ(input_count, s[0]->InputCount());
      ASSERT_EQ(InstructionOperand::IMMEDIATE,
                s[0]->InputAt(input_count - 1)->kind());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(input_count - 1)));
    } else {
      ASSERT_EQ(2U, s.size());
      EXPECT_EQ(kX64Imul32, s[0]->arch_opcode());
      EXPECT_EQ(kX64Add32, s[1]->arch_opcode());
    }
  }
}


TEST_P(InstructionSelectorMultTest, MultAdd64) {
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    const MultParam m_param = GetParam();
    StreamBuilder m(this, kMachInt64, kMachInt64);
    Node* param = m.Parameter(0);
    Node* mult = m.Int64Add(m.Int64Mul(param, m.Int64Constant(m_param.value)),
                            m.Int64Constant(imm));
    m.Return(mult);
    Stream s = m.Build();
    if (m_param.lea_expected) {
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kX64Lea, s[0]->arch_opcode());
      EXPECT_EQ(AddressingModeForAddMult(m_param), s[0]->addressing_mode());
      unsigned input_count = InputCountForLea(s[0]->addressing_mode());
      ASSERT_EQ(input_count, s[0]->InputCount());
      ASSERT_EQ(InstructionOperand::IMMEDIATE,
                s[0]->InputAt(input_count - 1)->kind());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(input_count - 1)));
    } else {
      ASSERT_EQ(2U, s.size());
      EXPECT_EQ(kX64Imul, s[0]->arch_opcode());
      EXPECT_EQ(kX64Add, s[1]->arch_opcode());
    }
  }
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest, InstructionSelectorMultTest,
                        ::testing::ValuesIn(kMultParams));

}  // namespace compiler
}  // namespace internal
}  // namespace v8
