// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/common-operator.h"
#include "src/compiler/node-properties.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::IsNull;

namespace v8 {
namespace internal {
namespace compiler {

typedef TestWithZone NodePropertiesTest;


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

}  // namespace compiler
}  // namespace internal
}  // namespace v8
