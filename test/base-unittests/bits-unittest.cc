// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/bits.h"
#include "src/base/macros.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace base {
namespace bits {

TEST(BitsTest, CountSetBits32) {
  EXPECT_EQ(0u, CountSetBits32(0));
  EXPECT_EQ(1u, CountSetBits32(1));
  EXPECT_EQ(8u, CountSetBits32(0x11111111));
  EXPECT_EQ(16u, CountSetBits32(0xf0f0f0f0));
  EXPECT_EQ(24u, CountSetBits32(0xfff0f0ff));
  EXPECT_EQ(32u, CountSetBits32(0xffffffff));
}


TEST(BitsTest, CountLeadingZeros32) {
  EXPECT_EQ(32u, CountLeadingZeros32(0));
  EXPECT_EQ(31u, CountLeadingZeros32(1));
  TRACED_FORRANGE(uint32_t, shift, 0, 31) {
    EXPECT_EQ(31u - shift, CountLeadingZeros32(1u << shift));
  }
  EXPECT_EQ(4u, CountLeadingZeros32(0x0f0f0f0f));
}


TEST(BitsTest, CountTrailingZeros32) {
  EXPECT_EQ(32u, CountTrailingZeros32(0));
  EXPECT_EQ(31u, CountTrailingZeros32(0x80000000));
  TRACED_FORRANGE(uint32_t, shift, 0, 31) {
    EXPECT_EQ(shift, CountTrailingZeros32(1u << shift));
  }
  EXPECT_EQ(4u, CountTrailingZeros32(0xf0f0f0f0));
}


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
