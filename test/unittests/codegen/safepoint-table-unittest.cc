// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/safepoint-table.h"

#include "src/codegen/macro-assembler.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8::internal {

using SafepointTableTest = TestWithZone;

TEST_F(SafepointTableTest, CreatePatch) {
  constexpr int kSize = 200;
  static_assert(kSize >= sizeof(uintptr_t) * 3 * kBitsPerByte);
  GrowableBitVector empty(0, zone());
  GrowableBitVector u(kSize, zone());
  GrowableBitVector v(kSize, zone());
  GrowableBitVector w(kSize, zone());
  u.Add(80, zone());
  v.Add(80, zone());
  w.Add(5, zone());
  uint32_t common_prefix_bits;
  BitVector* patch =
      CompareAndCreateXorPatch(zone(), u, v, &common_prefix_bits);
  EXPECT_EQ(kMaxUInt32, common_prefix_bits);
  EXPECT_EQ(nullptr, patch);

  // Difference to empty vector.
  patch = CompareAndCreateXorPatch(zone(), empty, v, &common_prefix_bits);
  EXPECT_EQ(80u, common_prefix_bits);
  EXPECT_EQ(1, patch->length());
  EXPECT_TRUE(patch->Contains(0));

  // Empty vector's difference to a non-empty vector.
  patch = CompareAndCreateXorPatch(zone(), u, empty, &common_prefix_bits);
  EXPECT_EQ(80u, common_prefix_bits);
  EXPECT_EQ(1, patch->length());
  EXPECT_TRUE(patch->Contains(0));

  // Difference 0->1 near the beginning.
  patch = CompareAndCreateXorPatch(zone(), empty, w, &common_prefix_bits);
  EXPECT_EQ(5u, common_prefix_bits);
  EXPECT_EQ(1, patch->length());
  EXPECT_TRUE(patch->Contains(0));

  // Difference in the middle of the second word.
  v.Add(81, zone());
  v.Add(191, zone());
  patch = CompareAndCreateXorPatch(zone(), u, v, &common_prefix_bits);
  EXPECT_EQ(81u, common_prefix_bits);
  EXPECT_EQ(192 - 81, patch->length());
  EXPECT_TRUE(patch->Contains(81 - 81));
  EXPECT_TRUE(patch->Contains(191 - 81));

  // Now with identical tails and only a small difference in the middle.
  v.Add(83, zone());
  u.Add(191, zone());
  patch = CompareAndCreateXorPatch(zone(), u, v, &common_prefix_bits);
  EXPECT_EQ(81u, common_prefix_bits);
  EXPECT_EQ(3, patch->length());
  EXPECT_TRUE(patch->Contains(81 - 81));
  EXPECT_TRUE(patch->Contains(83 - 81));

  // Difference 1->0 at the beginning of the second word.
  u.Add(64, zone());
  patch = CompareAndCreateXorPatch(zone(), u, v, &common_prefix_bits);
  EXPECT_EQ(64u, common_prefix_bits);
  EXPECT_EQ(84 - 64, patch->length());
  EXPECT_TRUE(patch->Contains(64 - 64));
  EXPECT_FALSE(patch->Contains(80 - 64));  // Both u and v have that bit.
  EXPECT_TRUE(patch->Contains(81 - 64));
  EXPECT_TRUE(patch->Contains(83 - 64));
}

}  // namespace v8::internal
