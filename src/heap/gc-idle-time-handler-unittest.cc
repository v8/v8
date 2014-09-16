// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "src/heap/gc-idle-time-handler.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

namespace {

class GCIdleTimeHandlerTest : public ::testing::Test {
 public:
  GCIdleTimeHandlerTest() {}
  virtual ~GCIdleTimeHandlerTest() {}

  GCIdleTimeHandler* handler() { return &handler_; }

  GCIdleTimeHandler::HeapState DefaultHeapState() {
    GCIdleTimeHandler::HeapState result;
    result.contexts_disposed = 0;
    result.size_of_objects = kSizeOfObjects;
    result.incremental_marking_stopped = false;
    result.can_start_incremental_marking = true;
    result.sweeping_in_progress = false;
    result.mark_compact_speed_in_bytes_per_ms = kMarkCompactSpeed;
    result.incremental_marking_speed_in_bytes_per_ms = kMarkingSpeed;
    return result;
  }

  static const size_t kSizeOfObjects = 100 * MB;
  static const size_t kMarkCompactSpeed = 200 * KB;
  static const size_t kMarkingSpeed = 200 * KB;

 private:
  GCIdleTimeHandler handler_;
};

}  // namespace


TEST(GCIdleTimeHandler, EstimateMarkingStepSizeInitial) {
  size_t step_size = GCIdleTimeHandler::EstimateMarkingStepSize(1, 0);
  EXPECT_EQ(
      static_cast<size_t>(GCIdleTimeHandler::kInitialConservativeMarkingSpeed *
                          GCIdleTimeHandler::kConservativeTimeRatio),
      step_size);
}


TEST(GCIdleTimeHandler, EstimateMarkingStepSizeNonZero) {
  size_t marking_speed_in_bytes_per_millisecond = 100;
  size_t step_size = GCIdleTimeHandler::EstimateMarkingStepSize(
      1, marking_speed_in_bytes_per_millisecond);
  EXPECT_EQ(static_cast<size_t>(marking_speed_in_bytes_per_millisecond *
                                GCIdleTimeHandler::kConservativeTimeRatio),
            step_size);
}


TEST(GCIdleTimeHandler, EstimateMarkingStepSizeOverflow1) {
  size_t step_size = GCIdleTimeHandler::EstimateMarkingStepSize(
      10, std::numeric_limits<size_t>::max());
  EXPECT_EQ(static_cast<size_t>(GCIdleTimeHandler::kMaximumMarkingStepSize),
            step_size);
}


TEST(GCIdleTimeHandler, EstimateMarkingStepSizeOverflow2) {
  size_t step_size = GCIdleTimeHandler::EstimateMarkingStepSize(
      std::numeric_limits<size_t>::max(), 10);
  EXPECT_EQ(static_cast<size_t>(GCIdleTimeHandler::kMaximumMarkingStepSize),
            step_size);
}


TEST(GCIdleTimeHandler, EstimateMarkCompactTimeInitial) {
  size_t size = 100 * MB;
  size_t time = GCIdleTimeHandler::EstimateMarkCompactTime(size, 0);
  EXPECT_EQ(size / GCIdleTimeHandler::kInitialConservativeMarkCompactSpeed,
            time);
}


TEST(GCIdleTimeHandler, EstimateMarkCompactTimeNonZero) {
  size_t size = 100 * MB;
  size_t speed = 1 * MB;
  size_t time = GCIdleTimeHandler::EstimateMarkCompactTime(size, speed);
  EXPECT_EQ(size / speed, time);
}


TEST(GCIdleTimeHandler, EstimateMarkCompactTimeMax) {
  size_t size = std::numeric_limits<size_t>::max();
  size_t speed = 1;
  size_t time = GCIdleTimeHandler::EstimateMarkCompactTime(size, speed);
  EXPECT_EQ(GCIdleTimeHandler::kMaxMarkCompactTimeInMs, time);
}


TEST_F(GCIdleTimeHandlerTest, AfterContextDisposeLargeIdleTime) {
  GCIdleTimeHandler::HeapState heap_state = DefaultHeapState();
  heap_state.contexts_disposed = 1;
  heap_state.incremental_marking_stopped = true;
  size_t speed = heap_state.mark_compact_speed_in_bytes_per_ms;
  int idle_time_ms =
      static_cast<int>((heap_state.size_of_objects + speed - 1) / speed);
  GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
  EXPECT_EQ(DO_FULL_GC, action.type);
}


TEST_F(GCIdleTimeHandlerTest, AfterContextDisposeSmallIdleTime1) {
  GCIdleTimeHandler::HeapState heap_state = DefaultHeapState();
  heap_state.contexts_disposed = 1;
  heap_state.incremental_marking_stopped = true;
  size_t speed = heap_state.mark_compact_speed_in_bytes_per_ms;
  int idle_time_ms = static_cast<int>(heap_state.size_of_objects / speed - 1);
  GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
  EXPECT_EQ(DO_INCREMENTAL_MARKING, action.type);
}


TEST_F(GCIdleTimeHandlerTest, AfterContextDisposeSmallIdleTime2) {
  GCIdleTimeHandler::HeapState heap_state = DefaultHeapState();
  heap_state.contexts_disposed = 1;
  size_t speed = heap_state.mark_compact_speed_in_bytes_per_ms;
  int idle_time_ms = static_cast<int>(heap_state.size_of_objects / speed - 1);
  GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
  EXPECT_EQ(DO_INCREMENTAL_MARKING, action.type);
}


TEST_F(GCIdleTimeHandlerTest, IncrementalMarking1) {
  GCIdleTimeHandler::HeapState heap_state = DefaultHeapState();
  size_t speed = heap_state.incremental_marking_speed_in_bytes_per_ms;
  int idle_time_ms = 10;
  GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
  EXPECT_EQ(DO_INCREMENTAL_MARKING, action.type);
  EXPECT_GT(speed * static_cast<size_t>(idle_time_ms),
            static_cast<size_t>(action.parameter));
  EXPECT_LT(0, action.parameter);
}


TEST_F(GCIdleTimeHandlerTest, IncrementalMarking2) {
  GCIdleTimeHandler::HeapState heap_state = DefaultHeapState();
  heap_state.incremental_marking_stopped = true;
  size_t speed = heap_state.incremental_marking_speed_in_bytes_per_ms;
  int idle_time_ms = 10;
  GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
  EXPECT_EQ(DO_INCREMENTAL_MARKING, action.type);
  EXPECT_GT(speed * static_cast<size_t>(idle_time_ms),
            static_cast<size_t>(action.parameter));
  EXPECT_LT(0, action.parameter);
}


TEST_F(GCIdleTimeHandlerTest, NotEnoughTime) {
  GCIdleTimeHandler::HeapState heap_state = DefaultHeapState();
  heap_state.incremental_marking_stopped = true;
  heap_state.can_start_incremental_marking = false;
  size_t speed = heap_state.mark_compact_speed_in_bytes_per_ms;
  int idle_time_ms = static_cast<int>(heap_state.size_of_objects / speed - 1);
  GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
  EXPECT_EQ(DO_NOTHING, action.type);
}


TEST_F(GCIdleTimeHandlerTest, StopEventually1) {
  GCIdleTimeHandler::HeapState heap_state = DefaultHeapState();
  heap_state.incremental_marking_stopped = true;
  heap_state.can_start_incremental_marking = false;
  size_t speed = heap_state.mark_compact_speed_in_bytes_per_ms;
  int idle_time_ms = static_cast<int>(heap_state.size_of_objects / speed + 1);
  for (int i = 0; i < GCIdleTimeHandler::kMaxMarkCompactsInIdleRound; i++) {
    GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
    EXPECT_EQ(DO_FULL_GC, action.type);
    handler()->NotifyIdleMarkCompact();
  }
  GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
  EXPECT_EQ(DO_NOTHING, action.type);
}


TEST_F(GCIdleTimeHandlerTest, StopEventually2) {
  GCIdleTimeHandler::HeapState heap_state = DefaultHeapState();
  int idle_time_ms = 10;
  for (int i = 0; i < GCIdleTimeHandler::kMaxMarkCompactsInIdleRound; i++) {
    GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
    EXPECT_EQ(DO_INCREMENTAL_MARKING, action.type);
    handler()->NotifyIdleMarkCompact();
  }
  GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
  EXPECT_EQ(DO_NOTHING, action.type);
}


TEST_F(GCIdleTimeHandlerTest, ContinueAfterStop1) {
  GCIdleTimeHandler::HeapState heap_state = DefaultHeapState();
  heap_state.incremental_marking_stopped = true;
  heap_state.can_start_incremental_marking = false;
  size_t speed = heap_state.mark_compact_speed_in_bytes_per_ms;
  int idle_time_ms = static_cast<int>(heap_state.size_of_objects / speed + 1);
  for (int i = 0; i < GCIdleTimeHandler::kMaxMarkCompactsInIdleRound; i++) {
    GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
    EXPECT_EQ(DO_FULL_GC, action.type);
    handler()->NotifyIdleMarkCompact();
  }
  GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
  EXPECT_EQ(DO_NOTHING, action.type);
  // Emulate mutator work.
  for (int i = 0; i < GCIdleTimeHandler::kIdleScavengeThreshold; i++) {
    handler()->NotifyScavenge();
  }
  action = handler()->Compute(idle_time_ms, heap_state);
  EXPECT_EQ(DO_FULL_GC, action.type);
}


TEST_F(GCIdleTimeHandlerTest, ContinueAfterStop2) {
  GCIdleTimeHandler::HeapState heap_state = DefaultHeapState();
  int idle_time_ms = 10;
  for (int i = 0; i < GCIdleTimeHandler::kMaxMarkCompactsInIdleRound; i++) {
    GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
    if (action.type == DO_NOTHING) break;
    EXPECT_EQ(DO_INCREMENTAL_MARKING, action.type);
    handler()->NotifyIdleMarkCompact();
  }
  GCIdleTimeAction action = handler()->Compute(idle_time_ms, heap_state);
  EXPECT_EQ(DO_NOTHING, action.type);
  // Emulate mutator work.
  for (int i = 0; i < GCIdleTimeHandler::kIdleScavengeThreshold; i++) {
    handler()->NotifyScavenge();
  }
  action = handler()->Compute(idle_time_ms, heap_state);
  EXPECT_EQ(DO_INCREMENTAL_MARKING, action.type);
}

}  // namespace internal
}  // namespace v8
