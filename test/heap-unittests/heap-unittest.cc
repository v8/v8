// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <climits>

#include "src/heap/gc-idle-time-handler.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

TEST(EstimateMarkingStepSizeTest, EstimateMarkingStepSizeInitial) {
  intptr_t step_size = GCIdleTimeHandler::EstimateMarkingStepSize(1, 0);
  EXPECT_EQ(static_cast<intptr_t>(
                GCIdleTimeHandler::kInitialConservativeMarkingSpeed *
                GCIdleTimeHandler::kConservativeTimeRatio),
            step_size);
}


TEST(EstimateMarkingStepSizeTest, EstimateMarkingStepSizeNonZero) {
  intptr_t marking_speed_in_bytes_per_millisecond = 100;
  intptr_t step_size = GCIdleTimeHandler::EstimateMarkingStepSize(
      1, marking_speed_in_bytes_per_millisecond);
  EXPECT_EQ(static_cast<intptr_t>(marking_speed_in_bytes_per_millisecond *
                                  GCIdleTimeHandler::kConservativeTimeRatio),
            step_size);
}


TEST(EstimateMarkingStepSizeTest, EstimateMarkingStepSizeOverflow1) {
  intptr_t step_size = GCIdleTimeHandler::EstimateMarkingStepSize(10, INT_MAX);
  EXPECT_EQ(INT_MAX, step_size);
}


TEST(EstimateMarkingStepSizeTest, EstimateMarkingStepSizeOverflow2) {
  intptr_t step_size = GCIdleTimeHandler::EstimateMarkingStepSize(INT_MAX, 10);
  EXPECT_EQ(INT_MAX, step_size);
}

}  // namespace internal
}  // namespace v8
