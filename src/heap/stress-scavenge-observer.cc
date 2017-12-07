// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/stress-scavenge-observer.h"

#include "src/base/utils/random-number-generator.h"
#include "src/heap/heap-inl.h"
#include "src/heap/spaces.h"
#include "src/isolate.h"

namespace v8 {
namespace internal {

// TODO(majeski): meaningful step_size
StressScavengeObserver::StressScavengeObserver(Heap& heap)
    : AllocationObserver(64), heap_(heap), has_requested_gc_(false) {
  if (FLAG_stress_scavenge > 0) {
    limit_percentage_ = NextLimit();

    if (FLAG_stress_scavenge_analysis) {
      heap_.isolate()->PrintWithTimestamp(
          "[StressScavenge] %d%% is the new limit\n", limit_percentage_);
    }
  }
}

void StressScavengeObserver::Step(int bytes_allocated, Address soon_object,
                                  size_t size) {
  if (has_requested_gc_ || heap_.new_space()->Capacity() == 0) {
    return;
  }

  double current_percent =
      heap_.new_space()->Size() * 100.0 / heap_.new_space()->Capacity();

  if (FLAG_stress_scavenge_analysis) {
    heap_.isolate()->PrintWithTimestamp(
        "[StressScavenge] %.2lf%% of the new space capacity reached\n",
        current_percent);
  }

  if (!FLAG_stress_scavenge) {
    return;
  }

  if (static_cast<int>(current_percent) >= limit_percentage_) {
    if (FLAG_stress_scavenge_analysis) {
      heap_.isolate()->PrintWithTimestamp("[StressScavenge] GC requested\n");
    }

    has_requested_gc_ = true;
    heap_.isolate()->stack_guard()->RequestGC();
  }
}

bool StressScavengeObserver::HasRequestedGC() const {
  return has_requested_gc_;
}

void StressScavengeObserver::RequestedGCDone() {
  double current_percent =
      heap_.new_space()->Size() * 100.0 / heap_.new_space()->Capacity();
  limit_percentage_ = NextLimit(static_cast<int>(current_percent));

  if (FLAG_stress_scavenge_analysis) {
    heap_.isolate()->PrintWithTimestamp(
        "[StressScavenge] %.2lf%% of the new space capacity reached\n",
        current_percent);
    heap_.isolate()->PrintWithTimestamp(
        "[StressScavenge] %d%% is the new limit\n", limit_percentage_);
  }

  has_requested_gc_ = false;
}

int StressScavengeObserver::NextLimit(int min) {
  int max = FLAG_stress_scavenge;
  if (min >= max) {
    return max;
  }

  return min + heap_.isolate()->fuzzer_rng()->NextInt(max - min + 1);
}

}  // namespace internal
}  // namespace v8
