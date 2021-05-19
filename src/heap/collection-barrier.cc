// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/collection-barrier.h"

#include "src/base/platform/time.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap.h"
#include "src/heap/parked-scope.h"

namespace v8 {
namespace internal {

bool CollectionBarrier::WasGCRequested() {
  return collection_requested_.load();
}

void CollectionBarrier::RequestGC() {
  ActivateStackGuardAndPostTask();

  base::MutexGuard guard(&mutex_);
  bool already_requested = collection_requested_.exchange(true);
  CHECK(!already_requested);

  CHECK(!timer_.IsStarted());
  timer_.Start();
}

class BackgroundCollectionInterruptTask : public CancelableTask {
 public:
  explicit BackgroundCollectionInterruptTask(Heap* heap)
      : CancelableTask(heap->isolate()), heap_(heap) {}

  ~BackgroundCollectionInterruptTask() override = default;
  BackgroundCollectionInterruptTask(const BackgroundCollectionInterruptTask&) =
      delete;
  BackgroundCollectionInterruptTask& operator=(
      const BackgroundCollectionInterruptTask&) = delete;

 private:
  // v8::internal::CancelableTask overrides.
  void RunInternal() override { heap_->CheckCollectionRequested(); }

  Heap* heap_;
};

void CollectionBarrier::ActivateStackGuardAndPostTask() {
  Isolate* isolate = heap_->isolate();
  ExecutionAccess access(isolate);
  isolate->stack_guard()->RequestGC();

  auto taskrunner = V8::GetCurrentPlatform()->GetForegroundTaskRunner(
      reinterpret_cast<v8::Isolate*>(isolate));
  taskrunner->PostTask(
      std::make_unique<BackgroundCollectionInterruptTask>(heap_));
}

void CollectionBarrier::NotifyShutdownRequested() {
  base::MutexGuard guard(&mutex_);
  if (timer_.IsStarted()) timer_.Stop();
  shutdown_requested_ = true;
  cv_wakeup_.NotifyAll();
}

void CollectionBarrier::ResumeThreadsAwaitingCollection() {
  base::MutexGuard guard(&mutex_);
  cv_wakeup_.NotifyAll();
}

bool CollectionBarrier::AwaitCollectionBackground(LocalHeap* local_heap) {
  ParkedScope scope(local_heap);
  base::MutexGuard guard(&mutex_);

  while (WasGCRequested()) {
    if (shutdown_requested_) return false;
    cv_wakeup_.Wait(&mutex_);
  }

  return true;
}

void CollectionBarrier::StopTimeToCollectionTimer() {
  if (collection_requested_.load()) {
    base::MutexGuard guard(&mutex_);
    // The first background thread that requests the GC, starts the timer first
    // and only then parks itself. Since we are in a safepoint here, the timer
    // is therefore always initialized here already.
    CHECK(timer_.IsStarted());
    base::TimeDelta delta = timer_.Elapsed();
    TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
                         "V8.GC.TimeToCollectionOnBackground",
                         TRACE_EVENT_SCOPE_THREAD, "duration",
                         delta.InMillisecondsF());
    heap_->isolate()
        ->counters()
        ->gc_time_to_collection_on_background()
        ->AddTimedSample(delta);
    timer_.Stop();
    collection_requested_.store(false);
  }
}

}  // namespace internal
}  // namespace v8
