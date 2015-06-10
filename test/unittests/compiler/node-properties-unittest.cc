// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/common-operator.h"
#include "src/compiler/node-properties.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::AnyOf;
using testing::ElementsAre;
using testing::IsNull;

namespace v8 {
namespace internal {
namespace compiler {

class NodePropertiesTest : public TestWithZone {
 public:
  Node* NewMockNode(const Operator* op, int input_count, Node** inputs) {
    return Node::New(zone(), 0, op, input_count, inputs, false);
  }
};

namespace {

const Operator kMockOperator(IrOpcode::kDead, Operator::kNoProperties,
                             "MockOperator", 0, 0, 0, 1, 1, 2);
const Operator kMockCallOperator(IrOpcode::kCall, Operator::kNoProperties,
                                 "MockCallOperator", 0, 0, 0, 0, 0, 2);

}  // namespace


TEST_F(NodePropertiesTest, ReplaceUses) {
  CommonOperatorBuilder common(zone());
  IfExceptionHint kNoHint = IfExceptionHint::kLocallyCaught;
  Node* node = NewMockNode(&kMockOperator, 0, nullptr);
  Node* use_value = NewMockNode(common.Return(), 1, &node);
  Node* use_effect = NewMockNode(common.EffectPhi(1), 1, &node);
  Node* use_success = NewMockNode(common.IfSuccess(), 1, &node);
  Node* use_exception = NewMockNode(common.IfException(kNoHint), 1, &node);
  Node* r_value = NewMockNode(&kMockOperator, 0, nullptr);
  Node* r_effect = NewMockNode(&kMockOperator, 0, nullptr);
  Node* r_success = NewMockNode(&kMockOperator, 0, nullptr);
  Node* r_exception = NewMockNode(&kMockOperator, 0, nullptr);
  NodeProperties::ReplaceUses(node, r_value, r_effect, r_success, r_exception);
  EXPECT_EQ(r_value, use_value->InputAt(0));
  EXPECT_EQ(r_effect, use_effect->InputAt(0));
  EXPECT_EQ(r_success, use_success->InputAt(0));
  EXPECT_EQ(r_exception, use_exception->InputAt(0));
  EXPECT_EQ(0, node->UseCount());
  EXPECT_EQ(1, r_value->UseCount());
  EXPECT_EQ(1, r_effect->UseCount());
  EXPECT_EQ(1, r_success->UseCount());
  EXPECT_EQ(1, r_exception->UseCount());
  EXPECT_THAT(r_value->uses(), ElementsAre(use_value));
  EXPECT_THAT(r_effect->uses(), ElementsAre(use_effect));
  EXPECT_THAT(r_success->uses(), ElementsAre(use_success));
  EXPECT_THAT(r_exception->uses(), ElementsAre(use_exception));
}


TEST_F(NodePropertiesTest, FindProjection) {
  CommonOperatorBuilder common(zone());
  Node* start = Node::New(zone(), 0, common.Start(1), 0, nullptr, false);
  Node* proj0 = Node::New(zone(), 1, common.Projection(0), 1, &start, false);
  Node* proj1 = Node::New(zone(), 2, common.Projection(1), 1, &start, false);
  EXPECT_EQ(proj0, NodeProperties::FindProjection(start, 0));
  EXPECT_EQ(proj1, NodeProperties::FindProjection(start, 1));
  EXPECT_THAT(NodeProperties::FindProjection(start, 2), IsNull());
  EXPECT_THAT(NodeProperties::FindProjection(start, 1234567890), IsNull());
}


TEST_F(NodePropertiesTest, CollectControlProjections_Branch) {
  Node* result[2];
  CommonOperatorBuilder common(zone());
  Node* branch = Node::New(zone(), 1, common.Branch(), 0, nullptr, false);
  Node* if_false = Node::New(zone(), 2, common.IfFalse(), 1, &branch, false);
  Node* if_true = Node::New(zone(), 3, common.IfTrue(), 1, &branch, false);
  NodeProperties::CollectControlProjections(branch, result, arraysize(result));
  EXPECT_EQ(if_true, result[0]);
  EXPECT_EQ(if_false, result[1]);
}


TEST_F(NodePropertiesTest, CollectControlProjections_Call) {
  Node* result[2];
  CommonOperatorBuilder common(zone());
  IfExceptionHint h = IfExceptionHint::kLocallyUncaught;
  Node* call = Node::New(zone(), 1, &kMockCallOperator, 0, nullptr, false);
  Node* if_ex = Node::New(zone(), 2, common.IfException(h), 1, &call, false);
  Node* if_ok = Node::New(zone(), 3, common.IfSuccess(), 1, &call, false);
  NodeProperties::CollectControlProjections(call, result, arraysize(result));
  EXPECT_EQ(if_ok, result[0]);
  EXPECT_EQ(if_ex, result[1]);
}


TEST_F(NodePropertiesTest, CollectControlProjections_Switch) {
  Node* result[3];
  CommonOperatorBuilder common(zone());
  Node* sw = Node::New(zone(), 1, common.Switch(3), 0, nullptr, false);
  Node* if_default = Node::New(zone(), 2, common.IfDefault(), 1, &sw, false);
  Node* if_value1 = Node::New(zone(), 3, common.IfValue(1), 1, &sw, false);
  Node* if_value2 = Node::New(zone(), 4, common.IfValue(2), 1, &sw, false);
  NodeProperties::CollectControlProjections(sw, result, arraysize(result));
  EXPECT_THAT(result[0], AnyOf(if_value1, if_value2));
  EXPECT_THAT(result[1], AnyOf(if_value1, if_value2));
  EXPECT_EQ(if_default, result[2]);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
