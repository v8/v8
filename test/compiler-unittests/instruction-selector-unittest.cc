// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/compiler-unittests/instruction-selector-unittest.h"

#include "src/flags.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

typedef RawMachineAssembler::Label MLabel;

}  // namespace


InstructionSelectorTest::InstructionSelectorTest() : rng_(FLAG_random_seed) {}


InstructionSelectorTest::Stream InstructionSelectorTest::StreamBuilder::Build(
    InstructionSelector::Features features,
    InstructionSelectorTest::StreamBuilderMode mode) {
  Schedule* schedule = Export();
  if (FLAG_trace_turbo) {
    OFStream out(stdout);
    out << "=== Schedule before instruction selection ===" << endl << *schedule;
  }
  EXPECT_NE(0, graph()->NodeCount());
  CompilationInfo info(test_->isolate(), test_->zone());
  Linkage linkage(&info, call_descriptor());
  InstructionSequence sequence(&linkage, graph(), schedule);
  SourcePositionTable source_position_table(graph());
  InstructionSelector selector(&sequence, &source_position_table, features);
  selector.SelectInstructions();
  if (FLAG_trace_turbo) {
    OFStream out(stdout);
    out << "=== Code sequence after instruction selection ===" << endl
        << sequence;
  }
  Stream s;
  std::set<int> virtual_registers;
  for (InstructionSequence::const_iterator i = sequence.begin();
       i != sequence.end(); ++i) {
    Instruction* instr = *i;
    if (instr->opcode() < 0) continue;
    if (mode == kTargetInstructions) {
      switch (instr->arch_opcode()) {
#define CASE(Name) \
  case k##Name:    \
    break;
        TARGET_ARCH_OPCODE_LIST(CASE)
#undef CASE
        default:
          continue;
      }
    }
    for (size_t i = 0; i < instr->OutputCount(); ++i) {
      InstructionOperand* output = instr->OutputAt(i);
      EXPECT_NE(InstructionOperand::IMMEDIATE, output->kind());
      if (output->IsConstant()) {
        s.constants_.insert(std::make_pair(
            output->index(), sequence.GetConstant(output->index())));
        virtual_registers.insert(output->index());
      } else if (output->IsUnallocated()) {
        virtual_registers.insert(
            UnallocatedOperand::cast(output)->virtual_register());
      }
    }
    for (size_t i = 0; i < instr->InputCount(); ++i) {
      InstructionOperand* input = instr->InputAt(i);
      EXPECT_NE(InstructionOperand::CONSTANT, input->kind());
      if (input->IsImmediate()) {
        s.immediates_.insert(std::make_pair(
            input->index(), sequence.GetImmediate(input->index())));
      } else if (input->IsUnallocated()) {
        virtual_registers.insert(
            UnallocatedOperand::cast(input)->virtual_register());
      }
    }
    s.instructions_.push_back(instr);
  }
  for (std::set<int>::const_iterator i = virtual_registers.begin();
       i != virtual_registers.end(); ++i) {
    int virtual_register = *i;
    if (sequence.IsDouble(virtual_register)) {
      EXPECT_FALSE(sequence.IsReference(virtual_register));
      s.doubles_.insert(virtual_register);
    }
    if (sequence.IsReference(virtual_register)) {
      EXPECT_FALSE(sequence.IsDouble(virtual_register));
      s.references_.insert(virtual_register);
    }
  }
  return s;
}


// -----------------------------------------------------------------------------
// Return.


TARGET_TEST_F(InstructionSelectorTest, ReturnParameter) {
  StreamBuilder m(this, kMachInt32, kMachInt32);
  m.Return(m.Parameter(0));
  Stream s = m.Build(kAllInstructions);
  ASSERT_EQ(2U, s.size());
  EXPECT_EQ(kArchNop, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kArchRet, s[1]->arch_opcode());
  EXPECT_EQ(1U, s[1]->InputCount());
}


TARGET_TEST_F(InstructionSelectorTest, ReturnZero) {
  StreamBuilder m(this, kMachInt32);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build(kAllInstructions);
  ASSERT_EQ(2U, s.size());
  EXPECT_EQ(kArchNop, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(InstructionOperand::CONSTANT, s[0]->OutputAt(0)->kind());
  EXPECT_EQ(0, s.ToInt32(s[0]->OutputAt(0)));
  EXPECT_EQ(kArchRet, s[1]->arch_opcode());
  EXPECT_EQ(1U, s[1]->InputCount());
}


// -----------------------------------------------------------------------------
// Conversions.


TARGET_TEST_F(InstructionSelectorTest, TruncateFloat64ToInt32WithParameter) {
  StreamBuilder m(this, kMachInt32, kMachFloat64);
  m.Return(m.TruncateFloat64ToInt32(m.Parameter(0)));
  Stream s = m.Build(kAllInstructions);
  ASSERT_EQ(3U, s.size());
  EXPECT_EQ(kArchNop, s[0]->arch_opcode());
  EXPECT_EQ(kArchTruncateDoubleToI, s[1]->arch_opcode());
  EXPECT_EQ(1U, s[1]->InputCount());
  EXPECT_EQ(1U, s[1]->OutputCount());
  EXPECT_EQ(kArchRet, s[2]->arch_opcode());
}


// -----------------------------------------------------------------------------
// Parameters.


TARGET_TEST_F(InstructionSelectorTest, DoubleParameter) {
  StreamBuilder m(this, kMachFloat64, kMachFloat64);
  Node* param = m.Parameter(0);
  m.Return(param);
  Stream s = m.Build(kAllInstructions);
  EXPECT_TRUE(s.IsDouble(param->id()));
}


TARGET_TEST_F(InstructionSelectorTest, ReferenceParameter) {
  StreamBuilder m(this, kMachAnyTagged, kMachAnyTagged);
  Node* param = m.Parameter(0);
  m.Return(param);
  Stream s = m.Build(kAllInstructions);
  EXPECT_TRUE(s.IsReference(param->id()));
}


// -----------------------------------------------------------------------------
// Finish.


typedef InstructionSelectorTestWithParam<MachineType>
    InstructionSelectorFinishTest;


TARGET_TEST_P(InstructionSelectorFinishTest, Parameter) {
  const MachineType type = GetParam();
  StreamBuilder m(this, type, type);
  Node* param = m.Parameter(0);
  Node* finish = m.NewNode(m.common()->Finish(1), param, m.graph()->start());
  m.Return(finish);
  Stream s = m.Build(kAllInstructions);
  ASSERT_EQ(3U, s.size());
  EXPECT_EQ(kArchNop, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->OutputCount());
  ASSERT_TRUE(s[0]->Output()->IsUnallocated());
  EXPECT_EQ(param->id(),
            UnallocatedOperand::cast(s[0]->Output())->virtual_register());
  EXPECT_EQ(kArchNop, s[1]->arch_opcode());
  ASSERT_EQ(1U, s[1]->InputCount());
  ASSERT_TRUE(s[1]->InputAt(0)->IsUnallocated());
  EXPECT_EQ(param->id(),
            UnallocatedOperand::cast(s[1]->InputAt(0))->virtual_register());
  ASSERT_EQ(1U, s[1]->OutputCount());
  ASSERT_TRUE(s[1]->Output()->IsUnallocated());
  EXPECT_TRUE(UnallocatedOperand::cast(s[1]->Output())->HasSameAsInputPolicy());
  EXPECT_EQ(finish->id(),
            UnallocatedOperand::cast(s[1]->Output())->virtual_register());
}


TARGET_TEST_P(InstructionSelectorFinishTest, PropagateDoubleness) {
  const MachineType type = GetParam();
  StreamBuilder m(this, type, type);
  Node* param = m.Parameter(0);
  Node* finish = m.NewNode(m.common()->Finish(1), param, m.graph()->start());
  m.Return(finish);
  Stream s = m.Build(kAllInstructions);
  EXPECT_EQ(s.IsDouble(param->id()), s.IsDouble(finish->id()));
}


TARGET_TEST_P(InstructionSelectorFinishTest, PropagateReferenceness) {
  const MachineType type = GetParam();
  StreamBuilder m(this, type, type);
  Node* param = m.Parameter(0);
  Node* finish = m.NewNode(m.common()->Finish(1), param, m.graph()->start());
  m.Return(finish);
  Stream s = m.Build(kAllInstructions);
  EXPECT_EQ(s.IsReference(param->id()), s.IsReference(finish->id()));
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest, InstructionSelectorFinishTest,
                        ::testing::Values(kMachFloat64, kMachInt8, kMachUint8,
                                          kMachInt16, kMachUint16, kMachInt32,
                                          kMachUint32, kMachInt64, kMachUint64,
                                          kMachPtr, kMachAnyTagged));


// -----------------------------------------------------------------------------
// Finish.


typedef InstructionSelectorTestWithParam<MachineType>
    InstructionSelectorPhiTest;


TARGET_TEST_P(InstructionSelectorPhiTest, PropagateDoubleness) {
  const MachineType type = GetParam();
  StreamBuilder m(this, type, type, type);
  Node* param0 = m.Parameter(0);
  Node* param1 = m.Parameter(1);
  MLabel a, b, c;
  m.Branch(m.Int32Constant(0), &a, &b);
  m.Bind(&a);
  m.Goto(&c);
  m.Bind(&b);
  m.Goto(&c);
  m.Bind(&c);
  Node* phi = m.Phi(param0, param1);
  m.Return(phi);
  Stream s = m.Build(kAllInstructions);
  EXPECT_EQ(s.IsDouble(phi->id()), s.IsDouble(param0->id()));
  EXPECT_EQ(s.IsDouble(phi->id()), s.IsDouble(param1->id()));
}


TARGET_TEST_P(InstructionSelectorPhiTest, PropagateReferenceness) {
  const MachineType type = GetParam();
  StreamBuilder m(this, type, type, type);
  Node* param0 = m.Parameter(0);
  Node* param1 = m.Parameter(1);
  MLabel a, b, c;
  m.Branch(m.Int32Constant(1), &a, &b);
  m.Bind(&a);
  m.Goto(&c);
  m.Bind(&b);
  m.Goto(&c);
  m.Bind(&c);
  Node* phi = m.Phi(param0, param1);
  m.Return(phi);
  Stream s = m.Build(kAllInstructions);
  EXPECT_EQ(s.IsReference(phi->id()), s.IsReference(param0->id()));
  EXPECT_EQ(s.IsReference(phi->id()), s.IsReference(param1->id()));
}


INSTANTIATE_TEST_CASE_P(InstructionSelectorTest, InstructionSelectorPhiTest,
                        ::testing::Values(kMachFloat64, kMachInt8, kMachUint8,
                                          kMachInt16, kMachUint16, kMachInt32,
                                          kMachUint32, kMachInt64, kMachUint64,
                                          kMachPtr, kMachAnyTagged));


// -----------------------------------------------------------------------------
// ValueEffect.


TARGET_TEST_F(InstructionSelectorTest, ValueEffect) {
  StreamBuilder m1(this, kMachInt32, kMachPtr);
  Node* p1 = m1.Parameter(0);
  m1.Return(m1.Load(kMachInt32, p1, m1.Int32Constant(0)));
  Stream s1 = m1.Build(kAllInstructions);
  StreamBuilder m2(this, kMachInt32, kMachPtr);
  Node* p2 = m2.Parameter(0);
  m2.Return(m2.NewNode(m2.machine()->Load(kMachInt32), p2, m2.Int32Constant(0),
                       m2.NewNode(m2.common()->ValueEffect(1), p2)));
  Stream s2 = m2.Build(kAllInstructions);
  EXPECT_LE(3U, s1.size());
  ASSERT_EQ(s1.size(), s2.size());
  TRACED_FORRANGE(size_t, i, 0, s1.size() - 1) {
    const Instruction* i1 = s1[i];
    const Instruction* i2 = s2[i];
    EXPECT_EQ(i1->arch_opcode(), i2->arch_opcode());
    EXPECT_EQ(i1->InputCount(), i2->InputCount());
    EXPECT_EQ(i1->OutputCount(), i2->OutputCount());
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
