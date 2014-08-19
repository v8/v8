// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

TEST(HeapTest, Dummy) {
  EXPECT_FALSE(false);
  EXPECT_TRUE(true);
}

}  // namespace internal
}  // namespace v8
