// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/opcodes.h"

#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

class NodeMatcherTest : public GraphTest {
 public:
  NodeMatcherTest() : machine_(zone()) {}
  virtual ~NodeMatcherTest() {}

  MachineOperatorBuilder* machine() { return &machine_; }

 private:
  MachineOperatorBuilder machine_;
};

namespace {

template <class Matcher>
void CheckScaledWithOffsetMatch(Matcher* matcher, Node* scaled,
                                int scale_exponent, Node* offset,
                                Node* constant) {
  EXPECT_TRUE(matcher->matches());
  EXPECT_EQ(scaled, matcher->scaled());
  EXPECT_EQ(scale_exponent, matcher->scale_exponent());
  EXPECT_EQ(offset, matcher->offset());
  EXPECT_EQ(constant, matcher->constant());
}
};


TEST_F(NodeMatcherTest, ScaledWithOffset32Matcher) {
  graph()->SetStart(graph()->NewNode(common()->Start(0)));

  const Operator* c0_op = common()->Int32Constant(0);
  Node* c0 = graph()->NewNode(c0_op);
  USE(c0);
  const Operator* c1_op = common()->Int32Constant(1);
  Node* c1 = graph()->NewNode(c1_op);
  USE(c1);
  const Operator* c2_op = common()->Int32Constant(2);
  Node* c2 = graph()->NewNode(c2_op);
  USE(c2);
  const Operator* c3_op = common()->Int32Constant(3);
  Node* c3 = graph()->NewNode(c3_op);
  USE(c3);
  const Operator* c4_op = common()->Int32Constant(4);
  Node* c4 = graph()->NewNode(c4_op);
  USE(c4);
  const Operator* c8_op = common()->Int32Constant(8);
  Node* c8 = graph()->NewNode(c8_op);
  USE(c8);
  const Operator* c15_op = common()->Int32Constant(15);
  Node* c15 = graph()->NewNode(c15_op);
  USE(c15);

  const Operator* o0_op = common()->Parameter(0);
  Node* o0 = graph()->NewNode(o0_op, graph()->start());
  USE(o0);
  const Operator* o1_op = common()->Parameter(1);
  Node* o1 = graph()->NewNode(o1_op, graph()->start());
  USE(o0);

  const Operator* p1_op = common()->Parameter(3);
  Node* p1 = graph()->NewNode(p1_op, graph()->start());
  USE(p1);

  const Operator* a_op = machine()->Int32Add();
  USE(a_op);

  const Operator* m_op = machine()->Int32Mul();
  Node* m1 = graph()->NewNode(m_op, p1, c1);
  Node* m2 = graph()->NewNode(m_op, p1, c2);
  Node* m4 = graph()->NewNode(m_op, p1, c4);
  Node* m8 = graph()->NewNode(m_op, p1, c8);
  Node* m3 = graph()->NewNode(m_op, p1, c3);
  USE(m1);
  USE(m2);
  USE(m4);
  USE(m8);
  USE(m3);

  const Operator* s_op = machine()->Word32Shl();
  Node* s0 = graph()->NewNode(s_op, p1, c0);
  Node* s1 = graph()->NewNode(s_op, p1, c1);
  Node* s2 = graph()->NewNode(s_op, p1, c2);
  Node* s3 = graph()->NewNode(s_op, p1, c3);
  Node* s4 = graph()->NewNode(s_op, p1, c4);
  USE(s0);
  USE(s1);
  USE(s2);
  USE(s3);
  USE(s4);

  // 1 INPUT

  // Only relevant test cases is checking for non-match.
  ScaledWithOffset32Matcher match0(c15);
  EXPECT_FALSE(match0.matches());

  // 2 INPUT

  // (O0 + O1) -> [O0, 0, O1, NULL]
  ScaledWithOffset32Matcher match1(graph()->NewNode(a_op, o0, o1));
  CheckScaledWithOffsetMatch(&match1, o1, 0, o0, NULL);

  // (O0 + C15) -> [NULL, 0, O0, C15]
  ScaledWithOffset32Matcher match2(graph()->NewNode(a_op, o0, c15));
  CheckScaledWithOffsetMatch(&match2, NULL, 0, o0, c15);

  // (C15 + O0) -> [NULL, 0, O0, C15]
  ScaledWithOffset32Matcher match3(graph()->NewNode(a_op, c15, o0));
  CheckScaledWithOffsetMatch(&match3, NULL, 0, o0, c15);

  // (O0 + M1) -> [p1, 0, O0, NULL]
  ScaledWithOffset32Matcher match4(graph()->NewNode(a_op, o0, m1));
  CheckScaledWithOffsetMatch(&match4, p1, 0, o0, NULL);

  // (M1 + O0) -> [p1, 0, O0, NULL]
  m1 = graph()->NewNode(m_op, p1, c1);
  ScaledWithOffset32Matcher match5(graph()->NewNode(a_op, m1, o0));
  CheckScaledWithOffsetMatch(&match5, p1, 0, o0, NULL);

  // (C15 + M1) -> [P1, 0, NULL, C15]
  m1 = graph()->NewNode(m_op, p1, c1);
  ScaledWithOffset32Matcher match6(graph()->NewNode(a_op, c15, m1));
  CheckScaledWithOffsetMatch(&match6, p1, 0, NULL, c15);

  // (M1 + C15) -> [P1, 0, NULL, C15]
  m1 = graph()->NewNode(m_op, p1, c1);
  ScaledWithOffset32Matcher match7(graph()->NewNode(a_op, m1, c15));
  CheckScaledWithOffsetMatch(&match7, p1, 0, NULL, c15);

  // (O0 + S0) -> [p1, 0, O0, NULL]
  ScaledWithOffset32Matcher match8(graph()->NewNode(a_op, o0, s0));
  CheckScaledWithOffsetMatch(&match8, p1, 0, o0, NULL);

  // (S0 + O0) -> [p1, 0, O0, NULL]
  s0 = graph()->NewNode(s_op, p1, c0);
  ScaledWithOffset32Matcher match9(graph()->NewNode(a_op, s0, o0));
  CheckScaledWithOffsetMatch(&match9, p1, 0, o0, NULL);

  // (C15 + S0) -> [P1, 0, NULL, C15]
  s0 = graph()->NewNode(s_op, p1, c0);
  ScaledWithOffset32Matcher match10(graph()->NewNode(a_op, c15, s0));
  CheckScaledWithOffsetMatch(&match10, p1, 0, NULL, c15);

  // (S0 + C15) -> [P1, 0, NULL, C15]
  s0 = graph()->NewNode(s_op, p1, c0);
  ScaledWithOffset32Matcher match11(graph()->NewNode(a_op, s0, c15));
  CheckScaledWithOffsetMatch(&match11, p1, 0, NULL, c15);

  // (O0 + M2) -> [p1, 1, O0, NULL]
  ScaledWithOffset32Matcher match12(graph()->NewNode(a_op, o0, m2));
  CheckScaledWithOffsetMatch(&match12, p1, 1, o0, NULL);

  // (M2 + O0) -> [p1, 1, O0, NULL]
  m2 = graph()->NewNode(m_op, p1, c2);
  ScaledWithOffset32Matcher match13(graph()->NewNode(a_op, m2, o0));
  CheckScaledWithOffsetMatch(&match13, p1, 1, o0, NULL);

  // (C15 + M2) -> [P1, 1, NULL, C15]
  m2 = graph()->NewNode(m_op, p1, c2);
  ScaledWithOffset32Matcher match14(graph()->NewNode(a_op, c15, m2));
  CheckScaledWithOffsetMatch(&match14, p1, 1, NULL, c15);

  // (M2 + C15) -> [P1, 1, NULL, C15]
  m2 = graph()->NewNode(m_op, p1, c2);
  ScaledWithOffset32Matcher match15(graph()->NewNode(a_op, m2, c15));
  CheckScaledWithOffsetMatch(&match15, p1, 1, NULL, c15);

  // (O0 + S1) -> [p1, 1, O0, NULL]
  ScaledWithOffset32Matcher match16(graph()->NewNode(a_op, o0, s1));
  CheckScaledWithOffsetMatch(&match16, p1, 1, o0, NULL);

  // (S1 + O0) -> [p1, 1, O0, NULL]
  s1 = graph()->NewNode(s_op, p1, c1);
  ScaledWithOffset32Matcher match17(graph()->NewNode(a_op, s1, o0));
  CheckScaledWithOffsetMatch(&match17, p1, 1, o0, NULL);

  // (C15 + S1) -> [P1, 1, NULL, C15]
  s1 = graph()->NewNode(s_op, p1, c1);
  ScaledWithOffset32Matcher match18(graph()->NewNode(a_op, c15, s1));
  CheckScaledWithOffsetMatch(&match18, p1, 1, NULL, c15);

  // (S1 + C15) -> [P1, 1, NULL, C15]
  s1 = graph()->NewNode(s_op, p1, c1);
  ScaledWithOffset32Matcher match19(graph()->NewNode(a_op, s1, c15));
  CheckScaledWithOffsetMatch(&match19, p1, 1, NULL, c15);

  // (O0 + M4) -> [p1, 2, O0, NULL]
  ScaledWithOffset32Matcher match20(graph()->NewNode(a_op, o0, m4));
  CheckScaledWithOffsetMatch(&match20, p1, 2, o0, NULL);

  // (M4 + O0) -> [p1, 2, O0, NULL]
  m4 = graph()->NewNode(m_op, p1, c4);
  ScaledWithOffset32Matcher match21(graph()->NewNode(a_op, m4, o0));
  CheckScaledWithOffsetMatch(&match21, p1, 2, o0, NULL);

  // (C15 + M4) -> [p1, 2, NULL, C15]
  m4 = graph()->NewNode(m_op, p1, c4);
  ScaledWithOffset32Matcher match22(graph()->NewNode(a_op, c15, m4));
  CheckScaledWithOffsetMatch(&match22, p1, 2, NULL, c15);

  // (M4 + C15) -> [p1, 2, NULL, C15]
  m4 = graph()->NewNode(m_op, p1, c4);
  ScaledWithOffset32Matcher match23(graph()->NewNode(a_op, m4, c15));
  CheckScaledWithOffsetMatch(&match23, p1, 2, NULL, c15);

  // (O0 + S2) -> [p1, 2, O0, NULL]
  ScaledWithOffset32Matcher match24(graph()->NewNode(a_op, o0, s2));
  CheckScaledWithOffsetMatch(&match24, p1, 2, o0, NULL);

  // (S2 + O0) -> [p1, 2, O0, NULL]
  s2 = graph()->NewNode(s_op, p1, c2);
  ScaledWithOffset32Matcher match25(graph()->NewNode(a_op, s2, o0));
  CheckScaledWithOffsetMatch(&match25, p1, 2, o0, NULL);

  // (C15 + S2) -> [p1, 2, NULL, C15]
  s2 = graph()->NewNode(s_op, p1, c2);
  ScaledWithOffset32Matcher match26(graph()->NewNode(a_op, c15, s2));
  CheckScaledWithOffsetMatch(&match26, p1, 2, NULL, c15);

  // (S2 + C15) -> [p1, 2, NULL, C15]
  s2 = graph()->NewNode(s_op, p1, c2);
  ScaledWithOffset32Matcher match27(graph()->NewNode(a_op, s2, c15));
  CheckScaledWithOffsetMatch(&match27, p1, 2, NULL, c15);

  // (O0 + M8) -> [p1, 2, O0, NULL]
  ScaledWithOffset32Matcher match28(graph()->NewNode(a_op, o0, m8));
  CheckScaledWithOffsetMatch(&match28, p1, 3, o0, NULL);

  // (M8 + O0) -> [p1, 2, O0, NULL]
  m8 = graph()->NewNode(m_op, p1, c8);
  ScaledWithOffset32Matcher match29(graph()->NewNode(a_op, m8, o0));
  CheckScaledWithOffsetMatch(&match29, p1, 3, o0, NULL);

  // (C15 + M8) -> [p1, 2, NULL, C15]
  m8 = graph()->NewNode(m_op, p1, c8);
  ScaledWithOffset32Matcher match30(graph()->NewNode(a_op, c15, m8));
  CheckScaledWithOffsetMatch(&match30, p1, 3, NULL, c15);

  // (M8 + C15) -> [p1, 2, NULL, C15]
  m8 = graph()->NewNode(m_op, p1, c8);
  ScaledWithOffset32Matcher match31(graph()->NewNode(a_op, m8, c15));
  CheckScaledWithOffsetMatch(&match31, p1, 3, NULL, c15);

  // (O0 + S3) -> [p1, 2, O0, NULL]
  ScaledWithOffset32Matcher match32(graph()->NewNode(a_op, o0, s3));
  CheckScaledWithOffsetMatch(&match32, p1, 3, o0, NULL);

  // (S3 + O0) -> [p1, 2, O0, NULL]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset32Matcher match33(graph()->NewNode(a_op, s3, o0));
  CheckScaledWithOffsetMatch(&match33, p1, 3, o0, NULL);

  // (C15 + S3) -> [p1, 2, NULL, C15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset32Matcher match34(graph()->NewNode(a_op, c15, s3));
  CheckScaledWithOffsetMatch(&match34, p1, 3, NULL, c15);

  // (S3 + C15) -> [p1, 2, NULL, C15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset32Matcher match35(graph()->NewNode(a_op, s3, c15));
  CheckScaledWithOffsetMatch(&match35, p1, 3, NULL, c15);

  // 2 INPUT - NEGATIVE CASES

  // (M3 + O1) -> [O0, 0, M3, NULL]
  ScaledWithOffset32Matcher match36(graph()->NewNode(a_op, o1, m3));
  CheckScaledWithOffsetMatch(&match36, m3, 0, o1, NULL);

  // (S4 + O1) -> [O0, 0, S4, NULL]
  ScaledWithOffset32Matcher match37(graph()->NewNode(a_op, o1, s4));
  CheckScaledWithOffsetMatch(&match37, s4, 0, o1, NULL);

  // 3 INPUT

  // (C15 + S3) + O0 -> [p1, 2, o0, c15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset32Matcher match38(
      graph()->NewNode(a_op, graph()->NewNode(a_op, c15, s3), o0));
  CheckScaledWithOffsetMatch(&match38, p1, 3, o0, c15);

  // (O0 + C15) + S3 -> [p1, 2, o0, c15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset32Matcher match39(
      graph()->NewNode(a_op, graph()->NewNode(a_op, o0, c15), s3));
  CheckScaledWithOffsetMatch(&match39, p1, 3, o0, c15);

  // (S3 + O0) + C15 -> [p1, 2, o0, c15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset32Matcher match40(
      graph()->NewNode(a_op, graph()->NewNode(a_op, s3, o0), c15));
  CheckScaledWithOffsetMatch(&match40, p1, 3, o0, c15);

  // C15 + (S3 + O0) -> [p1, 2, o0, c15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset32Matcher match41(
      graph()->NewNode(a_op, c15, graph()->NewNode(a_op, s3, o0)));
  CheckScaledWithOffsetMatch(&match41, p1, 3, o0, c15);

  // O0 + (C15 + S3) -> [p1, 2, o0, c15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset32Matcher match42(
      graph()->NewNode(a_op, o0, graph()->NewNode(a_op, c15, s3)));
  CheckScaledWithOffsetMatch(&match42, p1, 3, o0, c15);

  // S3 + (O0 + C15) -> [p1, 2, o0, c15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset32Matcher match43(
      graph()->NewNode(a_op, s3, graph()->NewNode(a_op, o0, c15)));
  CheckScaledWithOffsetMatch(&match43, p1, 3, o0, c15);
}


TEST_F(NodeMatcherTest, ScaledWithOffset64Matcher) {
  graph()->SetStart(graph()->NewNode(common()->Start(0)));

  const Operator* c0_op = common()->Int64Constant(0);
  Node* c0 = graph()->NewNode(c0_op);
  USE(c0);
  const Operator* c1_op = common()->Int64Constant(1);
  Node* c1 = graph()->NewNode(c1_op);
  USE(c1);
  const Operator* c2_op = common()->Int64Constant(2);
  Node* c2 = graph()->NewNode(c2_op);
  USE(c2);
  const Operator* c3_op = common()->Int64Constant(3);
  Node* c3 = graph()->NewNode(c3_op);
  USE(c3);
  const Operator* c4_op = common()->Int64Constant(4);
  Node* c4 = graph()->NewNode(c4_op);
  USE(c4);
  const Operator* c8_op = common()->Int64Constant(8);
  Node* c8 = graph()->NewNode(c8_op);
  USE(c8);
  const Operator* c15_op = common()->Int64Constant(15);
  Node* c15 = graph()->NewNode(c15_op);
  USE(c15);

  const Operator* o0_op = common()->Parameter(0);
  Node* o0 = graph()->NewNode(o0_op, graph()->start());
  USE(o0);
  const Operator* o1_op = common()->Parameter(1);
  Node* o1 = graph()->NewNode(o1_op, graph()->start());
  USE(o0);

  const Operator* p1_op = common()->Parameter(3);
  Node* p1 = graph()->NewNode(p1_op, graph()->start());
  USE(p1);

  const Operator* a_op = machine()->Int64Add();
  USE(a_op);

  const Operator* m_op = machine()->Int64Mul();
  Node* m1 = graph()->NewNode(m_op, p1, c1);
  Node* m2 = graph()->NewNode(m_op, p1, c2);
  Node* m4 = graph()->NewNode(m_op, p1, c4);
  Node* m8 = graph()->NewNode(m_op, p1, c8);
  Node* m3 = graph()->NewNode(m_op, p1, c3);
  USE(m1);
  USE(m2);
  USE(m4);
  USE(m8);
  USE(m3);

  const Operator* s_op = machine()->Word64Shl();
  Node* s0 = graph()->NewNode(s_op, p1, c0);
  Node* s1 = graph()->NewNode(s_op, p1, c1);
  Node* s2 = graph()->NewNode(s_op, p1, c2);
  Node* s3 = graph()->NewNode(s_op, p1, c3);
  Node* s4 = graph()->NewNode(s_op, p1, c4);
  USE(s0);
  USE(s1);
  USE(s2);
  USE(s3);
  USE(s4);

  // 1 INPUT

  // Only relevant test cases is checking for non-match.
  ScaledWithOffset64Matcher match0(c15);
  EXPECT_FALSE(match0.matches());

  // 2 INPUT

  // (O0 + O1) -> [O0, 0, O1, NULL]
  ScaledWithOffset64Matcher match1(graph()->NewNode(a_op, o0, o1));
  CheckScaledWithOffsetMatch(&match1, o1, 0, o0, NULL);

  // (O0 + C15) -> [NULL, 0, O0, C15]
  ScaledWithOffset64Matcher match2(graph()->NewNode(a_op, o0, c15));
  CheckScaledWithOffsetMatch(&match2, NULL, 0, o0, c15);

  // (C15 + O0) -> [NULL, 0, O0, C15]
  ScaledWithOffset64Matcher match3(graph()->NewNode(a_op, c15, o0));
  CheckScaledWithOffsetMatch(&match3, NULL, 0, o0, c15);

  // (O0 + M1) -> [p1, 0, O0, NULL]
  ScaledWithOffset64Matcher match4(graph()->NewNode(a_op, o0, m1));
  CheckScaledWithOffsetMatch(&match4, p1, 0, o0, NULL);

  // (M1 + O0) -> [p1, 0, O0, NULL]
  m1 = graph()->NewNode(m_op, p1, c1);
  ScaledWithOffset64Matcher match5(graph()->NewNode(a_op, m1, o0));
  CheckScaledWithOffsetMatch(&match5, p1, 0, o0, NULL);

  // (C15 + M1) -> [P1, 0, NULL, C15]
  m1 = graph()->NewNode(m_op, p1, c1);
  ScaledWithOffset64Matcher match6(graph()->NewNode(a_op, c15, m1));
  CheckScaledWithOffsetMatch(&match6, p1, 0, NULL, c15);

  // (M1 + C15) -> [P1, 0, NULL, C15]
  m1 = graph()->NewNode(m_op, p1, c1);
  ScaledWithOffset64Matcher match7(graph()->NewNode(a_op, m1, c15));
  CheckScaledWithOffsetMatch(&match7, p1, 0, NULL, c15);

  // (O0 + S0) -> [p1, 0, O0, NULL]
  ScaledWithOffset64Matcher match8(graph()->NewNode(a_op, o0, s0));
  CheckScaledWithOffsetMatch(&match8, p1, 0, o0, NULL);

  // (S0 + O0) -> [p1, 0, O0, NULL]
  s0 = graph()->NewNode(s_op, p1, c0);
  ScaledWithOffset64Matcher match9(graph()->NewNode(a_op, s0, o0));
  CheckScaledWithOffsetMatch(&match9, p1, 0, o0, NULL);

  // (C15 + S0) -> [P1, 0, NULL, C15]
  s0 = graph()->NewNode(s_op, p1, c0);
  ScaledWithOffset64Matcher match10(graph()->NewNode(a_op, c15, s0));
  CheckScaledWithOffsetMatch(&match10, p1, 0, NULL, c15);

  // (S0 + C15) -> [P1, 0, NULL, C15]
  s0 = graph()->NewNode(s_op, p1, c0);
  ScaledWithOffset64Matcher match11(graph()->NewNode(a_op, s0, c15));
  CheckScaledWithOffsetMatch(&match11, p1, 0, NULL, c15);

  // (O0 + M2) -> [p1, 1, O0, NULL]
  ScaledWithOffset64Matcher match12(graph()->NewNode(a_op, o0, m2));
  CheckScaledWithOffsetMatch(&match12, p1, 1, o0, NULL);

  // (M2 + O0) -> [p1, 1, O0, NULL]
  m2 = graph()->NewNode(m_op, p1, c2);
  ScaledWithOffset64Matcher match13(graph()->NewNode(a_op, m2, o0));
  CheckScaledWithOffsetMatch(&match13, p1, 1, o0, NULL);

  // (C15 + M2) -> [P1, 1, NULL, C15]
  m2 = graph()->NewNode(m_op, p1, c2);
  ScaledWithOffset64Matcher match14(graph()->NewNode(a_op, c15, m2));
  CheckScaledWithOffsetMatch(&match14, p1, 1, NULL, c15);

  // (M2 + C15) -> [P1, 1, NULL, C15]
  m2 = graph()->NewNode(m_op, p1, c2);
  ScaledWithOffset64Matcher match15(graph()->NewNode(a_op, m2, c15));
  CheckScaledWithOffsetMatch(&match15, p1, 1, NULL, c15);

  // (O0 + S1) -> [p1, 1, O0, NULL]
  ScaledWithOffset64Matcher match16(graph()->NewNode(a_op, o0, s1));
  CheckScaledWithOffsetMatch(&match16, p1, 1, o0, NULL);

  // (S1 + O0) -> [p1, 1, O0, NULL]
  s1 = graph()->NewNode(s_op, p1, c1);
  ScaledWithOffset64Matcher match17(graph()->NewNode(a_op, s1, o0));
  CheckScaledWithOffsetMatch(&match17, p1, 1, o0, NULL);

  // (C15 + S1) -> [P1, 1, NULL, C15]
  s1 = graph()->NewNode(s_op, p1, c1);
  ScaledWithOffset64Matcher match18(graph()->NewNode(a_op, c15, s1));
  CheckScaledWithOffsetMatch(&match18, p1, 1, NULL, c15);

  // (S1 + C15) -> [P1, 1, NULL, C15]
  s1 = graph()->NewNode(s_op, p1, c1);
  ScaledWithOffset64Matcher match19(graph()->NewNode(a_op, s1, c15));
  CheckScaledWithOffsetMatch(&match19, p1, 1, NULL, c15);

  // (O0 + M4) -> [p1, 2, O0, NULL]
  ScaledWithOffset64Matcher match20(graph()->NewNode(a_op, o0, m4));
  CheckScaledWithOffsetMatch(&match20, p1, 2, o0, NULL);

  // (M4 + O0) -> [p1, 2, O0, NULL]
  m4 = graph()->NewNode(m_op, p1, c4);
  ScaledWithOffset64Matcher match21(graph()->NewNode(a_op, m4, o0));
  CheckScaledWithOffsetMatch(&match21, p1, 2, o0, NULL);

  // (C15 + M4) -> [p1, 2, NULL, C15]
  m4 = graph()->NewNode(m_op, p1, c4);
  ScaledWithOffset64Matcher match22(graph()->NewNode(a_op, c15, m4));
  CheckScaledWithOffsetMatch(&match22, p1, 2, NULL, c15);

  // (M4 + C15) -> [p1, 2, NULL, C15]
  m4 = graph()->NewNode(m_op, p1, c4);
  ScaledWithOffset64Matcher match23(graph()->NewNode(a_op, m4, c15));
  CheckScaledWithOffsetMatch(&match23, p1, 2, NULL, c15);

  // (O0 + S2) -> [p1, 2, O0, NULL]
  ScaledWithOffset64Matcher match24(graph()->NewNode(a_op, o0, s2));
  CheckScaledWithOffsetMatch(&match24, p1, 2, o0, NULL);

  // (S2 + O0) -> [p1, 2, O0, NULL]
  s2 = graph()->NewNode(s_op, p1, c2);
  ScaledWithOffset64Matcher match25(graph()->NewNode(a_op, s2, o0));
  CheckScaledWithOffsetMatch(&match25, p1, 2, o0, NULL);

  // (C15 + S2) -> [p1, 2, NULL, C15]
  s2 = graph()->NewNode(s_op, p1, c2);
  ScaledWithOffset64Matcher match26(graph()->NewNode(a_op, c15, s2));
  CheckScaledWithOffsetMatch(&match26, p1, 2, NULL, c15);

  // (S2 + C15) -> [p1, 2, NULL, C15]
  s2 = graph()->NewNode(s_op, p1, c2);
  ScaledWithOffset64Matcher match27(graph()->NewNode(a_op, s2, c15));
  CheckScaledWithOffsetMatch(&match27, p1, 2, NULL, c15);

  // (O0 + M8) -> [p1, 2, O0, NULL]
  ScaledWithOffset64Matcher match28(graph()->NewNode(a_op, o0, m8));
  CheckScaledWithOffsetMatch(&match28, p1, 3, o0, NULL);

  // (M8 + O0) -> [p1, 2, O0, NULL]
  m8 = graph()->NewNode(m_op, p1, c8);
  ScaledWithOffset64Matcher match29(graph()->NewNode(a_op, m8, o0));
  CheckScaledWithOffsetMatch(&match29, p1, 3, o0, NULL);

  // (C15 + M8) -> [p1, 2, NULL, C15]
  m8 = graph()->NewNode(m_op, p1, c8);
  ScaledWithOffset64Matcher match30(graph()->NewNode(a_op, c15, m8));
  CheckScaledWithOffsetMatch(&match30, p1, 3, NULL, c15);

  // (M8 + C15) -> [p1, 2, NULL, C15]
  m8 = graph()->NewNode(m_op, p1, c8);
  ScaledWithOffset64Matcher match31(graph()->NewNode(a_op, m8, c15));
  CheckScaledWithOffsetMatch(&match31, p1, 3, NULL, c15);

  // (O0 + S3) -> [p1, 2, O0, NULL]
  ScaledWithOffset64Matcher match64(graph()->NewNode(a_op, o0, s3));
  CheckScaledWithOffsetMatch(&match64, p1, 3, o0, NULL);

  // (S3 + O0) -> [p1, 2, O0, NULL]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset64Matcher match33(graph()->NewNode(a_op, s3, o0));
  CheckScaledWithOffsetMatch(&match33, p1, 3, o0, NULL);

  // (C15 + S3) -> [p1, 2, NULL, C15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset64Matcher match34(graph()->NewNode(a_op, c15, s3));
  CheckScaledWithOffsetMatch(&match34, p1, 3, NULL, c15);

  // (S3 + C15) -> [p1, 2, NULL, C15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset64Matcher match35(graph()->NewNode(a_op, s3, c15));
  CheckScaledWithOffsetMatch(&match35, p1, 3, NULL, c15);

  // 2 INPUT - NEGATIVE CASES

  // (M3 + O1) -> [O0, 0, M3, NULL]
  ScaledWithOffset64Matcher match36(graph()->NewNode(a_op, o1, m3));
  CheckScaledWithOffsetMatch(&match36, m3, 0, o1, NULL);

  // (S4 + O1) -> [O0, 0, S4, NULL]
  ScaledWithOffset64Matcher match37(graph()->NewNode(a_op, o1, s4));
  CheckScaledWithOffsetMatch(&match37, s4, 0, o1, NULL);

  // 3 INPUT

  // (C15 + S3) + O0 -> [p1, 2, o0, c15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset64Matcher match38(
      graph()->NewNode(a_op, graph()->NewNode(a_op, c15, s3), o0));
  CheckScaledWithOffsetMatch(&match38, p1, 3, o0, c15);

  // (O0 + C15) + S3 -> [p1, 2, o0, c15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset64Matcher match39(
      graph()->NewNode(a_op, graph()->NewNode(a_op, o0, c15), s3));
  CheckScaledWithOffsetMatch(&match39, p1, 3, o0, c15);

  // (S3 + O0) + C15 -> [p1, 2, o0, c15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset64Matcher match40(
      graph()->NewNode(a_op, graph()->NewNode(a_op, s3, o0), c15));
  CheckScaledWithOffsetMatch(&match40, p1, 3, o0, c15);

  // C15 + (S3 + O0) -> [p1, 2, o0, c15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset64Matcher match41(
      graph()->NewNode(a_op, c15, graph()->NewNode(a_op, s3, o0)));
  CheckScaledWithOffsetMatch(&match41, p1, 3, o0, c15);

  // O0 + (C15 + S3) -> [p1, 2, o0, c15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset64Matcher match42(
      graph()->NewNode(a_op, o0, graph()->NewNode(a_op, c15, s3)));
  CheckScaledWithOffsetMatch(&match42, p1, 3, o0, c15);

  // S3 + (O0 + C15) -> [p1, 2, o0, c15]
  s3 = graph()->NewNode(s_op, p1, c3);
  ScaledWithOffset64Matcher match43(
      graph()->NewNode(a_op, s3, graph()->NewNode(a_op, o0, c15)));
  CheckScaledWithOffsetMatch(&match43, p1, 3, o0, c15);
}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
