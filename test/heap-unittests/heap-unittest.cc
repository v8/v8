// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "src/heap/gc-idle-time-handler.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

TEST(EstimateMarkingStepSizeTest, EstimateMarkingStepSizeInitial) {
  size_t step_size = GCIdleTimeHandler::EstimateMarkingStepSize(1, 0);
  EXPECT_EQ(
      static_cast<size_t>(GCIdleTimeHandler::kInitialConservativeMarkingSpeed *
                          GCIdleTimeHandler::kConservativeTimeRatio),
      step_size);
}


TEST(EstimateMarkingStepSizeTest, EstimateMarkingStepSizeNonZero) {
  size_t marking_speed_in_bytes_per_millisecond = 100;
  size_t step_size = GCIdleTimeHandler::EstimateMarkingStepSize(
      1, marking_speed_in_bytes_per_millisecond);
  EXPECT_EQ(static_cast<size_t>(marking_speed_in_bytes_per_millisecond *
                                GCIdleTimeHandler::kConservativeTimeRatio),
            step_size);
}


TEST(EstimateMarkingStepSizeTest, EstimateMarkingStepSizeOverflow1) {
  size_t step_size = GCIdleTimeHandler::EstimateMarkingStepSize(
      10, std::numeric_limits<size_t>::max());
  EXPECT_EQ(static_cast<size_t>(GCIdleTimeHandler::kMaximumMarkingStepSize),
            step_size);
}


TEST(EstimateMarkingStepSizeTest, EstimateMarkingStepSizeOverflow2) {
  size_t step_size = GCIdleTimeHandler::EstimateMarkingStepSize(
      std::numeric_limits<size_t>::max(), 10);
  EXPECT_EQ(static_cast<size_t>(GCIdleTimeHandler::kMaximumMarkingStepSize),
            step_size);
}

}  // namespace internal
}  // namespace v8
