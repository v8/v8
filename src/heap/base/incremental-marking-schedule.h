// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_BASE_INCREMENTAL_MARKING_SCHEDULE_H_
#define V8_HEAP_BASE_INCREMENTAL_MARKING_SCHEDULE_H_

#include <atomic>

#include "src/base/optional.h"
#include "src/base/platform/time.h"

namespace heap::base {

// Incremental marking schedule that assumes a fixed time window for scheduling
// estimated set of bytes.
class V8_EXPORT_PRIVATE IncrementalMarkingSchedule final {
 public:
  // Estimated duration of marking time per GC cycle.
  static constexpr v8::base::TimeDelta kEstimatedMarkingTime =
      v8::base::TimeDelta::FromMilliseconds(500);

  // Minimum number of bytes that should be marked during an incremental
  // marking step.
  static constexpr size_t kMinimumMarkedBytesPerIncrementalStep = 64 * 1024;

  void NotifyIncrementalMarkingStart();

  void UpdateMutatorThreadMarkedBytes(size_t);
  void AddConcurrentlyMarkedBytes(size_t);

  size_t GetOverallMarkedBytes() const;
  size_t GetConcurrentlyMarkedBytes() const;

  size_t GetNextIncrementalStepDuration(size_t);

  bool ShouldFlushEphemeronPairs();

  void SetElapsedTimeForTesting(v8::base::TimeDelta);

 private:
  static constexpr double kEphemeronPairsFlushingRatioIncrements = 0.25;

  v8::base::TimeDelta GetElapsedTime();

  v8::base::TimeTicks incremental_marking_start_time_;
  size_t mutator_thread_marked_bytes_ = 0;
  std::atomic_size_t concurrently_marked_bytes_{0};
  size_t last_estimated_live_bytes_ = 0;
  double ephemeron_pairs_flushing_ratio_target_ = 0.25;
  v8::base::Optional<v8::base::TimeDelta> elapsed_time_for_testing_;
};

}  // namespace heap::base

#endif  // V8_HEAP_BASE_INCREMENTAL_MARKING_SCHEDULE_H_
