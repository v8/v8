// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/incremental-marking.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace v8::internal::heap {

TEST(V8IncrementalMarkingTest, EstimateMarkingStepSizeInitial) {
  const size_t step_size = IncrementalMarking::EstimateMarkingStepSize(1, 0);
  EXPECT_EQ(
      static_cast<size_t>(IncrementalMarking::kInitialConservativeMarkingSpeed *
                          IncrementalMarking::kConservativeTimeRatio),
      step_size);
}

TEST(V8IncrementalMarkingTest, EstimateMarkingStepSizeNonZero) {
  const size_t marking_speed_in_bytes_per_millisecond = 100;
  const size_t step_size = IncrementalMarking::EstimateMarkingStepSize(
      1, marking_speed_in_bytes_per_millisecond);
  EXPECT_EQ(static_cast<size_t>(marking_speed_in_bytes_per_millisecond *
                                IncrementalMarking::kConservativeTimeRatio),
            step_size);
}

TEST(V8IncrementalMarkingTest, EstimateMarkingStepSizeOverflow1) {
  const size_t step_size = IncrementalMarking::EstimateMarkingStepSize(
      10, static_cast<double>(std::numeric_limits<size_t>::max()));
  EXPECT_EQ(static_cast<size_t>(IncrementalMarking::kMaximumMarkingStepSize),
            step_size);
}

TEST(V8IncrementalMarkingTest, EstimateMarkingStepSizeOverflow2) {
  const size_t step_size = IncrementalMarking::EstimateMarkingStepSize(
      static_cast<double>(std::numeric_limits<size_t>::max()), 10);
  EXPECT_EQ(static_cast<size_t>(IncrementalMarking::kMaximumMarkingStepSize),
            step_size);
}

}  // namespace v8::internal::heap
