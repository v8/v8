// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/ast-value-factory.h"
#include "src/heap/heap-inl.h"
#include "src/isolate-inl.h"
#include "src/zone/zone.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

class AstValueTest : public TestWithIsolateAndZone {
 protected:
  AstValueTest()
      : ast_value_factory_(zone(), i_isolate()->ast_string_constants(),
                           i_isolate()->heap()->HashSeed()) {}

  AstValueFactory ast_value_factory_;
};

TEST_F(AstValueTest, BigIntBooleanValue) {
  EXPECT_FALSE(ast_value_factory_.NewBigInt("0")->BooleanValue());
  EXPECT_FALSE(ast_value_factory_.NewBigInt("0b0")->BooleanValue());
  EXPECT_FALSE(ast_value_factory_.NewBigInt("0o0")->BooleanValue());
  EXPECT_FALSE(ast_value_factory_.NewBigInt("0x0")->BooleanValue());
  EXPECT_FALSE(ast_value_factory_.NewBigInt("0b000")->BooleanValue());
  EXPECT_FALSE(ast_value_factory_.NewBigInt("0o00000")->BooleanValue());
  EXPECT_FALSE(ast_value_factory_.NewBigInt("0x000000000")->BooleanValue());

  EXPECT_TRUE(ast_value_factory_.NewBigInt("3")->BooleanValue());
  EXPECT_TRUE(ast_value_factory_.NewBigInt("0b1")->BooleanValue());
  EXPECT_TRUE(ast_value_factory_.NewBigInt("0o6")->BooleanValue());
  EXPECT_TRUE(ast_value_factory_.NewBigInt("0xa")->BooleanValue());
  EXPECT_TRUE(ast_value_factory_.NewBigInt("0b0000001")->BooleanValue());
  EXPECT_TRUE(ast_value_factory_.NewBigInt("0o00005000")->BooleanValue());
  EXPECT_TRUE(ast_value_factory_.NewBigInt("0x0000d00c0")->BooleanValue());
}

}  // namespace internal
}  // namespace v8
