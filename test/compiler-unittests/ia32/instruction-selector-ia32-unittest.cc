// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/compiler-unittests/instruction-selector-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

// Immediates (random subset).
static const int32_t kImmediates[] = {
    kMinInt, -42, -1, 0,  1,  2,    3,      4,          5,
    6,       7,   8,  16, 42, 0xff, 0xffff, 0x0f0f0f0f, kMaxInt};

}  // namespace


TEST_F(InstructionSelectorTest, Int32AddWithParameter) {
  StreamBuilder m(this, kMachInt32, kMachInt32, kMachInt32);
  m.Return(m.Int32Add(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kIA32Add, s[0]->arch_opcode());
}


TEST_F(InstructionSelectorTest, Int32AddWithImmediate) {
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    {
      StreamBuilder m(this, kMachInt32, kMachInt32);
      m.Return(m.Int32Add(m.Parameter(0), m.Int32Constant(imm)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kIA32Add, s[0]->arch_opcode());
      ASSERT_EQ(2U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    }
    {
      StreamBuilder m(this, kMachInt32, kMachInt32);
      m.Return(m.Int32Add(m.Int32Constant(imm), m.Parameter(0)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kIA32Add, s[0]->arch_opcode());
      ASSERT_EQ(2U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    }
  }
}


TEST_F(InstructionSelectorTest, Int32SubWithParameter) {
  StreamBuilder m(this, kMachInt32, kMachInt32, kMachInt32);
  m.Return(m.Int32Sub(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kIA32Sub, s[0]->arch_opcode());
  EXPECT_EQ(1U, s[0]->OutputCount());
}


TEST_F(InstructionSelectorTest, Int32SubWithImmediate) {
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, kMachInt32, kMachInt32);
    m.Return(m.Int32Sub(m.Parameter(0), m.Int32Constant(imm)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kIA32Sub, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
