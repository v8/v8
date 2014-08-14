// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/bits.h"
#include "src/base/macros.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace base {
namespace bits {

TEST(BitsTest, RotateRight32) {
  TRACED_FORRANGE(uint32_t, shift, 0, 31) {
    EXPECT_EQ(0u, RotateRight32(0u, shift));
  }
  EXPECT_EQ(1u, RotateRight32(1, 0));
  EXPECT_EQ(1u, RotateRight32(2, 1));
  EXPECT_EQ(0x80000000u, RotateRight32(1, 1));
}


TEST(BitsTest, RotateRight64) {
  TRACED_FORRANGE(uint64_t, shift, 0, 63) {
    EXPECT_EQ(0u, RotateRight64(0u, shift));
  }
  EXPECT_EQ(1u, RotateRight64(1, 0));
  EXPECT_EQ(1u, RotateRight64(2, 1));
  EXPECT_EQ(V8_UINT64_C(0x8000000000000000), RotateRight64(1, 1));
}

}  // namespace bits
}  // namespace base
}  // namespace v8
