// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_GC_IDLE_TIME_HANDLER_H_
#define V8_HEAP_GC_IDLE_TIME_HANDLER_H_

#include "src/globals.h"

namespace v8 {
namespace internal {

enum GCIdleTimeActionType {
  DO_NOTHING,
  DO_INCREMENTAL_MARKING,
  DO_SCAVENGE,
  DO_FULL_GC
};


class GCIdleTimeAction {
 public:
  static GCIdleTimeAction Nothing() {
    GCIdleTimeAction result;
    result.type = DO_NOTHING;
    result.parameter = 0;
    return result;
  }
  static GCIdleTimeAction IncrementalMarking(intptr_t step_size) {
    GCIdleTimeAction result;
    result.type = DO_INCREMENTAL_MARKING;
    result.parameter = step_size;
    return result;
  }
  static GCIdleTimeAction Scavenge() {
    GCIdleTimeAction result;
    result.type = DO_SCAVENGE;
    result.parameter = 0;
    return result;
  }
  static GCIdleTimeAction FullGC() {
    GCIdleTimeAction result;
    result.type = DO_FULL_GC;
    result.parameter = 0;
    return result;
  }

  GCIdleTimeActionType type;
  intptr_t parameter;
};

class GCTracer;

// The idle time handler makes decisions about which garbage collection
// operations are executing during IdleNotification.
class GCIdleTimeHandler {
 public:
  // If we haven't recorded any incremental marking events yet, we carefully
  // mark with a conservative lower bound for the marking speed.
  static const size_t kInitialConservativeMarkingSpeed = 100 * KB;

  // Maximum marking step size returned by EstimateMarkingStepSize.
  static const size_t kMaximumMarkingStepSize = 700 * MB;

  // We have to make sure that we finish the IdleNotification before
  // idle_time_in_ms. Hence, we conservatively prune our workload estimate.
  static const double kConservativeTimeRatio;

  // If we haven't recorded any mark-compact events yet, we use
  // conservative lower bound for the mark-compact speed.
  static const size_t kInitialConservativeMarkCompactSpeed = 2 * MB;

  // Maximum mark-compact time returned by EstimateMarkCompactTime.
  static const size_t kMaxMarkCompactTimeInMs;

  GCIdleTimeHandler()
      : mark_compacts_since_idle_round_started_(0),
        scavenges_since_last_idle_round_(0) {}

  GCIdleTimeAction Compute(int idle_time_in_ms, int contexts_disposed,
                           size_t size_of_objects,
                           bool incremental_marking_stopped,
                           GCTracer* gc_tracer);

  void NotifyIdleMarkCompact() {
    if (mark_compacts_since_idle_round_started_ < kMaxMarkCompactsInIdleRound) {
      ++mark_compacts_since_idle_round_started_;
      if (mark_compacts_since_idle_round_started_ ==
          kMaxMarkCompactsInIdleRound) {
        scavenges_since_last_idle_round_ = 0;
      }
    }
  }

  void NotifyScavenge() { ++scavenges_since_last_idle_round_; }

  static size_t EstimateMarkingStepSize(size_t idle_time_in_ms,
                                        size_t marking_speed_in_bytes_per_ms);

  static size_t EstimateMarkCompactTime(
      size_t size_of_objects, size_t mark_compact_speed_in_bytes_per_ms);

 private:
  void StartIdleRound() { mark_compacts_since_idle_round_started_ = 0; }
  bool IsIdleRoundFinished() {
    return mark_compacts_since_idle_round_started_ ==
           kMaxMarkCompactsInIdleRound;
  }
  bool EnoughGarbageSinceLastIdleRound() {
    return scavenges_since_last_idle_round_ >= kIdleScavengeThreshold;
  }

  static const int kMaxMarkCompactsInIdleRound = 7;
  static const int kIdleScavengeThreshold = 5;
  int mark_compacts_since_idle_round_started_;
  int scavenges_since_last_idle_round_;

  DISALLOW_COPY_AND_ASSIGN(GCIdleTimeHandler);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_GC_IDLE_TIME_HANDLER_H_
