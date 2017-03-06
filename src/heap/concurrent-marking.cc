// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/concurrent-marking.h"

#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/isolate.h"
#include "src/locked-queue-inl.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

class ConcurrentMarking::Task : public CancelableTask {
 public:
  Task(Heap* heap, Queue* queue, base::Semaphore* on_finish)
      : CancelableTask(heap->isolate()),
        heap_(heap),
        queue_(queue),
        on_finish_(on_finish) {}

  virtual ~Task() {}

 private:
  // v8::internal::CancelableTask overrides.
  void RunInternal() override {
    USE(heap_);
    HeapObject* object;
    while (queue_->Dequeue(&object)) {
      // TODO(ulan): Implement actual marking.
    }
    on_finish_->Signal();
  }

  Heap* heap_;
  Queue* queue_;
  base::Semaphore* on_finish_;
  DISALLOW_COPY_AND_ASSIGN(Task);
};

ConcurrentMarking::ConcurrentMarking(Heap* heap)
    : heap_(heap), pending_tasks_(0), number_of_tasks_(0) {}

ConcurrentMarking::~ConcurrentMarking() {}

void ConcurrentMarking::EnqueueObject(HeapObject* object) {
  queue_.Enqueue(object);
}

bool ConcurrentMarking::IsQueueEmpty() { return queue_.IsEmpty(); }

void ConcurrentMarking::StartMarkingTasks(int number_of_tasks) {
  if (!FLAG_concurrent_marking) return;
  DCHECK_EQ(0, number_of_tasks_);

  number_of_tasks_ = number_of_tasks;
  for (int i = 0; i < number_of_tasks; i++) {
    V8::GetCurrentPlatform()->CallOnBackgroundThread(
        new Task(heap_, &queue_, &pending_tasks_),
        v8::Platform::kShortRunningTask);
  }
}

void ConcurrentMarking::WaitForTasksToComplete() {
  if (!FLAG_concurrent_marking) return;

  CancelableTaskManager* cancelable_task_manager =
      heap_->isolate()->cancelable_task_manager();
  for (int i = 0; i < number_of_tasks_; i++) {
    if (cancelable_task_manager->TryAbort(task_ids_[i]) !=
        CancelableTaskManager::kTaskAborted) {
      pending_tasks_.Wait();
    }
  }
  number_of_tasks_ = 0;
}

}  // namespace internal
}  // namespace v8
