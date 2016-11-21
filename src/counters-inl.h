// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COUNTERS_INL_H_
#define V8_COUNTERS_INL_H_

#include "src/counters.h"

namespace v8 {
namespace internal {

void RuntimeCallTimer::Start(RuntimeCallCounter* counter,
                             RuntimeCallTimer* parent) {
  counter_ = counter;
  parent_.SetValue(parent);
  if (FLAG_runtime_stats !=
      v8::tracing::TracingCategoryObserver::ENABLED_BY_SAMPLING) {
    timer_.Start();
  }
}

RuntimeCallTimer* RuntimeCallTimer::Stop() {
  if (!timer_.IsStarted()) return parent();
  base::TimeDelta delta = timer_.Elapsed();
  timer_.Stop();
  counter_->count++;
  counter_->time += delta;
  if (parent()) {
    // Adjust parent timer so that it does not include sub timer's time.
    parent()->Subtract(delta);
  }
  return parent();
}

void RuntimeCallTimer::Subtract(base::TimeDelta delta) {
  // Adjust the current timer instead of directly subtracting the sub-timers
  // from the current counter. This way we can easily change the counter of an
  // active timer scope. Otherwise we would end up subtracting the time from the
  // previous counter and add the own time to the newly changed counter.
  timer_.Subtract(delta);
}

void RuntimeCallTimer::Snapshot() {
  base::TimeTicks now = base::TimeTicks::HighResolutionNow();
  RuntimeCallTimer* timer = this;
  base::TimeDelta delta = base::TimeDelta::FromMicroseconds(0);
  // Walk up the timer chain until the the timer doesn't have a parent.
  while (timer != nullptr) {
    // Iteration 1:   subtract 0 from the current timer (this).
    // Iteration n+1: subtract subtimer's time (delta) from the the current
    //                timer.
    timer->Subtract(delta);
    delta = timer->timer_.Restart(now);
    timer->counter_->time += delta;
    timer = timer->parent();
  }
}

RuntimeCallTimerScope::RuntimeCallTimerScope(
    Isolate* isolate, RuntimeCallStats::CounterId counter_id) {
  if (V8_UNLIKELY(FLAG_runtime_stats)) {
    Initialize(isolate->counters()->runtime_call_stats(), counter_id);
  }
}

RuntimeCallTimerScope::RuntimeCallTimerScope(
    HeapObject* heap_object, RuntimeCallStats::CounterId counter_id) {
  RuntimeCallTimerScope(heap_object->GetIsolate(), counter_id);
}

RuntimeCallTimerScope::RuntimeCallTimerScope(
    RuntimeCallStats* stats, RuntimeCallStats::CounterId counter_id) {
  if (V8_UNLIKELY(FLAG_runtime_stats)) {
    Initialize(stats, counter_id);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COUNTERS_INL_H_
