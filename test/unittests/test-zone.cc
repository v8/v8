// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/test-zone.h"

#include "testing/gmock/include/gmock/gmock.h"

namespace v8 {
namespace internal {

TEST_F(ZoneTest, AllocationSizeIsEmptyOnStart) {
  EXPECT_EQ(0u, zone()->allocation_size());
}

}  // namespace internal
}  // namespace v8
