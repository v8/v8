// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/collection-barrier.h"

#include "src/heap/heap-inl.h"

namespace v8 {
namespace internal {

void CollectionBarrier::ResumeThreadsAwaitingCollection() {
  base::MutexGuard guard(&mutex_);
  ClearCollectionRequested();
  cond_.NotifyAll();
}

void CollectionBarrier::ShutdownRequested() {
  base::MutexGuard guard(&mutex_);
  time_to_collection_scope_.reset();
  state_.store(RequestState::kShutdown);
  cond_.NotifyAll();
}

class BackgroundCollectionInterruptTask : public CancelableTask {
 public:
  explicit BackgroundCollectionInterruptTask(Heap* heap)
      : CancelableTask(heap->isolate()), heap_(heap) {}

  ~BackgroundCollectionInterruptTask() override = default;

 private:
  // v8::internal::CancelableTask overrides.
  void RunInternal() override { heap_->CheckCollectionRequested(); }

  Heap* heap_;
  DISALLOW_COPY_AND_ASSIGN(BackgroundCollectionInterruptTask);
};

void CollectionBarrier::AwaitCollectionBackground() {
  if (FirstCollectionRequest()) {
    // This is the first background thread requesting collection, ask the main
    // thread for GC.
    ActivateStackGuardAndPostTask();
  }

  BlockUntilCollected();
}

void CollectionBarrier::StopTimeToCollectionTimer() {
  base::MutexGuard guard(&mutex_);
  time_to_collection_scope_.reset();
}

void CollectionBarrier::ActivateStackGuardAndPostTask() {
  Isolate* isolate = heap_->isolate();
  ExecutionAccess access(isolate);
  isolate->stack_guard()->RequestGC();
  auto taskrunner = V8::GetCurrentPlatform()->GetForegroundTaskRunner(
      reinterpret_cast<v8::Isolate*>(isolate));
  taskrunner->PostTask(
      std::make_unique<BackgroundCollectionInterruptTask>(heap_));
  base::MutexGuard guard(&mutex_);
  time_to_collection_scope_.emplace(isolate->counters()->time_to_collection());
}

void CollectionBarrier::BlockUntilCollected() {
  base::MutexGuard guard(&mutex_);

  while (CollectionRequested()) {
    cond_.Wait(&mutex_);
  }
}

}  // namespace internal
}  // namespace v8
