// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/base/incremental-marking-schedule.h"

#include <cmath>

namespace heap::base {

void IncrementalMarkingSchedule::NotifyIncrementalMarkingStart() {
  DCHECK(incremental_marking_start_time_.IsNull());
  incremental_marking_start_time_ = v8::base::TimeTicks::Now();
}

void IncrementalMarkingSchedule::UpdateMutatorThreadMarkedBytes(
    size_t overall_marked_bytes) {
  mutator_thread_marked_bytes_ = overall_marked_bytes;
}

void IncrementalMarkingSchedule::AddConcurrentlyMarkedBytes(
    size_t marked_bytes) {
  concurrently_marked_bytes_.fetch_add(marked_bytes, std::memory_order_relaxed);
}

size_t IncrementalMarkingSchedule::GetOverallMarkedBytes() const {
  return mutator_thread_marked_bytes_ + GetConcurrentlyMarkedBytes();
}

size_t IncrementalMarkingSchedule::GetConcurrentlyMarkedBytes() const {
  return concurrently_marked_bytes_.load(std::memory_order_relaxed);
}

v8::base::TimeDelta IncrementalMarkingSchedule::GetElapsedTime() {
  if (elapsed_time_for_testing_.has_value()) {
    const v8::base::TimeDelta elapsed_time = *elapsed_time_for_testing_;
    elapsed_time_for_testing_.reset();
    return elapsed_time;
  }
  return v8::base::TimeTicks::Now() - incremental_marking_start_time_;
}

size_t IncrementalMarkingSchedule::GetNextIncrementalStepDuration(
    size_t estimated_live_bytes) {
  last_estimated_live_bytes_ = estimated_live_bytes;
  DCHECK(!incremental_marking_start_time_.IsNull());
  const auto elapsed_time = GetElapsedTime();
  const size_t actual_marked_bytes = GetOverallMarkedBytes();
  const size_t expected_marked_bytes =
      std::ceil(estimated_live_bytes * elapsed_time.InMillisecondsF() /
                kEstimatedMarkingTime.InMillisecondsF());
  if (expected_marked_bytes < actual_marked_bytes) {
    // Marking is ahead of schedule, incremental marking should do the minimum.
    return kMinimumMarkedBytesPerIncrementalStep;
  }
  // Assuming marking will take |kEstimatedMarkingTime|, overall there will
  // be |estimated_live_bytes| live bytes to mark, and that marking speed is
  // constant, after |elapsed_time| the number of marked_bytes should be
  // |estimated_live_bytes| * (|elapsed_time| / |kEstimatedMarkingTime|),
  // denoted as |expected_marked_bytes|.  If |actual_marked_bytes| is less,
  // i.e. marking is behind schedule, incremental marking should help "catch
  // up" by marking (|expected_marked_bytes| - |actual_marked_bytes|).
  return std::max(kMinimumMarkedBytesPerIncrementalStep,
                  expected_marked_bytes - actual_marked_bytes);
}

constexpr double
    IncrementalMarkingSchedule::kEphemeronPairsFlushingRatioIncrements;
bool IncrementalMarkingSchedule::ShouldFlushEphemeronPairs() {
  if (GetOverallMarkedBytes() <
      (ephemeron_pairs_flushing_ratio_target_ * last_estimated_live_bytes_))
    return false;
  ephemeron_pairs_flushing_ratio_target_ +=
      kEphemeronPairsFlushingRatioIncrements;
  return true;
}

void IncrementalMarkingSchedule::SetElapsedTimeForTesting(
    v8::base::TimeDelta elapsed_time) {
  elapsed_time_for_testing_.emplace(elapsed_time);
}

}  // namespace heap::base
