// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/gc-idle-time-handler.h"
#include "src/heap/gc-tracer.h"
#include "src/utils.h"

namespace v8 {
namespace internal {

const double GCIdleTimeHandler::kConservativeTimeRatio = 0.9;
const size_t GCIdleTimeHandler::kMaxMarkCompactTimeInMs = 1000;
const size_t GCIdleTimeHandler::kMinTimeForFinalizeSweeping = 100;
const int GCIdleTimeHandler::kMaxMarkCompactsInIdleRound = 7;
const int GCIdleTimeHandler::kIdleScavengeThreshold = 5;


void GCIdleTimeAction::Print() {
  switch (type) {
    case DO_NOTHING:
      PrintF("no action");
      break;
    case DO_INCREMENTAL_MARKING:
      PrintF("incremental marking with step %" V8_PTR_PREFIX "d", parameter);
      break;
    case DO_SCAVENGE:
      PrintF("scavenge");
      break;
    case DO_FULL_GC:
      PrintF("full GC");
      break;
    case DO_FINALIZE_SWEEPING:
      PrintF("finalize sweeping");
      break;
  }
}


size_t GCIdleTimeHandler::EstimateMarkingStepSize(
    size_t idle_time_in_ms, size_t marking_speed_in_bytes_per_ms) {
  DCHECK(idle_time_in_ms > 0);

  if (marking_speed_in_bytes_per_ms == 0) {
    marking_speed_in_bytes_per_ms = kInitialConservativeMarkingSpeed;
  }

  size_t marking_step_size = marking_speed_in_bytes_per_ms * idle_time_in_ms;
  if (marking_step_size / marking_speed_in_bytes_per_ms != idle_time_in_ms) {
    // In the case of an overflow we return maximum marking step size.
    return kMaximumMarkingStepSize;
  }

  if (marking_step_size > kMaximumMarkingStepSize)
    return kMaximumMarkingStepSize;

  return static_cast<size_t>(marking_step_size * kConservativeTimeRatio);
}


size_t GCIdleTimeHandler::EstimateMarkCompactTime(
    size_t size_of_objects, size_t mark_compact_speed_in_bytes_per_ms) {
  if (mark_compact_speed_in_bytes_per_ms == 0) {
    mark_compact_speed_in_bytes_per_ms = kInitialConservativeMarkCompactSpeed;
  }
  size_t result = size_of_objects / mark_compact_speed_in_bytes_per_ms;
  return Min(result, kMaxMarkCompactTimeInMs);
}


GCIdleTimeAction GCIdleTimeHandler::Compute(size_t idle_time_in_ms,
                                            HeapState heap_state) {
  if (IsIdleRoundFinished()) {
    if (EnoughGarbageSinceLastIdleRound() || heap_state.contexts_disposed > 0) {
      StartIdleRound();
    } else {
      return GCIdleTimeAction::Nothing();
    }
  }
  if (heap_state.incremental_marking_stopped) {
    size_t estimated_time_in_ms =
        EstimateMarkCompactTime(heap_state.size_of_objects,
                                heap_state.mark_compact_speed_in_bytes_per_ms);
    if (idle_time_in_ms >= estimated_time_in_ms ||
        (heap_state.size_of_objects < kSmallHeapSize &&
         heap_state.contexts_disposed > 0)) {
      // If there are no more than two GCs left in this idle round and we are
      // allowed to do a full GC, then make those GCs full in order to compact
      // the code space.
      // TODO(ulan): Once we enable code compaction for incremental marking, we
      // can get rid of this special case and always start incremental marking.
      int remaining_mark_sweeps =
          kMaxMarkCompactsInIdleRound - mark_compacts_since_idle_round_started_;
      if (heap_state.contexts_disposed > 0 || remaining_mark_sweeps <= 2 ||
          !heap_state.can_start_incremental_marking) {
        return GCIdleTimeAction::FullGC();
      }
    }
    if (!heap_state.can_start_incremental_marking) {
      return GCIdleTimeAction::Nothing();
    }
  }
  // TODO(hpayer): Estimate finalize sweeping time.
  if (heap_state.sweeping_in_progress &&
      idle_time_in_ms >= kMinTimeForFinalizeSweeping) {
    return GCIdleTimeAction::FinalizeSweeping();
  }

  size_t step_size = EstimateMarkingStepSize(
      idle_time_in_ms, heap_state.incremental_marking_speed_in_bytes_per_ms);
  return GCIdleTimeAction::IncrementalMarking(step_size);
}
}
}
