// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/base/incremental-marking-schedule.h"

#include "src/base/platform/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace heap::base {

namespace {

class IncrementalMarkingScheduleTest : public ::testing::Test {
 public:
  static constexpr size_t kEstimatedLiveSize =
      100 * IncrementalMarkingSchedule::kMinimumMarkedBytesPerIncrementalStep;
};

const v8::base::TimeDelta kHalfEstimatedMarkingTime =
    v8::base::TimeDelta::FromMillisecondsD(
        IncrementalMarkingSchedule::kEstimatedMarkingTime.InMillisecondsF() *
        0.5);

}  // namespace

TEST_F(IncrementalMarkingScheduleTest, FirstStepReturnsDefaultDuration) {
  IncrementalMarkingSchedule schedule;
  schedule.NotifyIncrementalMarkingStart();
  schedule.SetElapsedTimeForTesting(v8::base::TimeDelta::FromMilliseconds(0));
  EXPECT_EQ(IncrementalMarkingSchedule::kMinimumMarkedBytesPerIncrementalStep,
            schedule.GetNextIncrementalStepDuration(kEstimatedLiveSize));
}

// If marking is not behind schedule and very small time passed between steps
// the oracle should return the minimum step duration.
TEST_F(IncrementalMarkingScheduleTest, NoTimePassedReturnsMinimumDuration) {
  IncrementalMarkingSchedule schedule;
  schedule.NotifyIncrementalMarkingStart();
  // Add incrementally marked bytes to tell oracle this is not the first step.
  schedule.UpdateMutatorThreadMarkedBytes(
      IncrementalMarkingSchedule::kMinimumMarkedBytesPerIncrementalStep);
  schedule.SetElapsedTimeForTesting(v8::base::TimeDelta::FromMilliseconds(0));
  EXPECT_EQ(IncrementalMarkingSchedule::kMinimumMarkedBytesPerIncrementalStep,
            schedule.GetNextIncrementalStepDuration(kEstimatedLiveSize));
}

TEST_F(IncrementalMarkingScheduleTest, OracleDoesntExccedMaximumStepDuration) {
  IncrementalMarkingSchedule schedule;
  schedule.NotifyIncrementalMarkingStart();
  // Add incrementally marked bytes to tell oracle this is not the first step.
  static constexpr size_t kMarkedBytes = 1;
  schedule.UpdateMutatorThreadMarkedBytes(kMarkedBytes);
  schedule.SetElapsedTimeForTesting(
      IncrementalMarkingSchedule::kEstimatedMarkingTime);
  EXPECT_EQ(kEstimatedLiveSize - kMarkedBytes,
            schedule.GetNextIncrementalStepDuration(kEstimatedLiveSize));
}

TEST_F(IncrementalMarkingScheduleTest, AheadOfScheduleReturnsMinimumDuration) {
  IncrementalMarkingSchedule schedule;
  schedule.NotifyIncrementalMarkingStart();
  // Add incrementally marked bytes to tell oracle this is not the first step.
  schedule.UpdateMutatorThreadMarkedBytes(
      IncrementalMarkingSchedule::kMinimumMarkedBytesPerIncrementalStep);
  schedule.AddConcurrentlyMarkedBytes(0.6 * kEstimatedLiveSize);
  schedule.SetElapsedTimeForTesting(kHalfEstimatedMarkingTime);
  EXPECT_EQ(IncrementalMarkingSchedule::kMinimumMarkedBytesPerIncrementalStep,
            schedule.GetNextIncrementalStepDuration(kEstimatedLiveSize));
}

TEST_F(IncrementalMarkingScheduleTest, BehindScheduleReturnsCorrectDuration) {
  IncrementalMarkingSchedule schedule;
  schedule.NotifyIncrementalMarkingStart();
  schedule.UpdateMutatorThreadMarkedBytes(0.1 * kEstimatedLiveSize);
  schedule.AddConcurrentlyMarkedBytes(0.25 * kEstimatedLiveSize);
  schedule.SetElapsedTimeForTesting(kHalfEstimatedMarkingTime);
  EXPECT_EQ(0.15 * kEstimatedLiveSize,
            schedule.GetNextIncrementalStepDuration(kEstimatedLiveSize));
  schedule.AddConcurrentlyMarkedBytes(0.05 * kEstimatedLiveSize);
  schedule.SetElapsedTimeForTesting(kHalfEstimatedMarkingTime);
  EXPECT_EQ(0.1 * kEstimatedLiveSize,
            schedule.GetNextIncrementalStepDuration(kEstimatedLiveSize));
  schedule.AddConcurrentlyMarkedBytes(0.05 * kEstimatedLiveSize);
  schedule.SetElapsedTimeForTesting(kHalfEstimatedMarkingTime);
  EXPECT_EQ(0.05 * kEstimatedLiveSize,
            schedule.GetNextIncrementalStepDuration(kEstimatedLiveSize));
}

TEST_F(IncrementalMarkingScheduleTest, GetCurrentStepInfo) {
  IncrementalMarkingSchedule schedule;
  schedule.NotifyIncrementalMarkingStart();
  schedule.UpdateMutatorThreadMarkedBytes(0.3 * kEstimatedLiveSize);
  schedule.AddConcurrentlyMarkedBytes(0.4 * kEstimatedLiveSize);
  schedule.SetElapsedTimeForTesting(kHalfEstimatedMarkingTime);
  schedule.GetNextIncrementalStepDuration(kEstimatedLiveSize);
  const auto step_info = schedule.GetCurrentStepInfo();
  EXPECT_EQ(step_info.elapsed_time, kHalfEstimatedMarkingTime);
  EXPECT_EQ(step_info.mutator_marked_bytes, 0.3 * kEstimatedLiveSize);
  EXPECT_EQ(step_info.concurrent_marked_bytes, 0.4 * kEstimatedLiveSize);
  EXPECT_EQ(step_info.marked_bytes(), 0.7 * kEstimatedLiveSize);
  EXPECT_EQ(step_info.estimated_live_bytes, kEstimatedLiveSize);
  EXPECT_NE(step_info.scheduled_delta_bytes(), 0);
}

}  // namespace heap::base
