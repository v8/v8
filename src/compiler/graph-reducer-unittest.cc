// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/graph.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/operator.h"
#include "src/test/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::_;
using testing::DefaultValue;
using testing::Return;
using testing::Sequence;
using testing::StrictMock;

namespace v8 {
namespace internal {
namespace compiler {

namespace {

SimpleOperator OP0(0, Operator::kNoWrite, 0, 0, "op0");


struct MockReducer : public Reducer {
  MOCK_METHOD1(Reduce, Reduction(Node*));
};

}  // namespace


class GraphReducerTest : public TestWithZone {
 public:
  GraphReducerTest() : graph_(zone()) {}

  static void SetUpTestCase() {
    TestWithZone::SetUpTestCase();
    DefaultValue<Reduction>::Set(Reducer::NoChange());
  }

  static void TearDownTestCase() {
    DefaultValue<Reduction>::Clear();
    TestWithZone::TearDownTestCase();
  }

 protected:
  void ReduceNode(Node* node, Reducer* r) {
    GraphReducer reducer(graph());
    reducer.AddReducer(r);
    reducer.ReduceNode(node);
  }

  void ReduceNode(Node* node, Reducer* r1, Reducer* r2) {
    GraphReducer reducer(graph());
    reducer.AddReducer(r1);
    reducer.AddReducer(r2);
    reducer.ReduceNode(node);
  }

  void ReduceNode(Node* node, Reducer* r1, Reducer* r2, Reducer* r3) {
    GraphReducer reducer(graph());
    reducer.AddReducer(r1);
    reducer.AddReducer(r2);
    reducer.AddReducer(r3);
    reducer.ReduceNode(node);
  }

  Graph* graph() { return &graph_; }

 private:
  Graph graph_;
};


TEST_F(GraphReducerTest, ReduceOnceForEveryReducer) {
  StrictMock<MockReducer> r1, r2;
  Node* node0 = graph()->NewNode(&OP0);
  EXPECT_CALL(r1, Reduce(node0));
  EXPECT_CALL(r2, Reduce(node0));
  ReduceNode(node0, &r1, &r2);
}


TEST_F(GraphReducerTest, ReduceAgainAfterChanged) {
  Sequence s1, s2;
  StrictMock<MockReducer> r1, r2, r3;
  Node* node0 = graph()->NewNode(&OP0);
  EXPECT_CALL(r1, Reduce(node0));
  EXPECT_CALL(r2, Reduce(node0));
  EXPECT_CALL(r3, Reduce(node0)).InSequence(s1, s2).WillOnce(
      Return(Reducer::Changed(node0)));
  EXPECT_CALL(r1, Reduce(node0)).InSequence(s1);
  EXPECT_CALL(r2, Reduce(node0)).InSequence(s2);
  ReduceNode(node0, &r1, &r2, &r3);
}


TEST_F(GraphReducerTest, OperatorIsNullAfterReplace) {
  StrictMock<MockReducer> r;
  Node* node0 = graph()->NewNode(&OP0);
  Node* node1 = graph()->NewNode(&OP0);
  EXPECT_CALL(r, Reduce(node0)).WillOnce(Return(Reducer::Replace(node1)));
  ReduceNode(node0, &r);
  EXPECT_EQ(NULL, node0->op());
  EXPECT_EQ(&OP0, node1->op());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
