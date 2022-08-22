// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/incremental-marking-job.h"

#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/execution/isolate.h"
#include "src/execution/vm-state-inl.h"
#include "src/heap/embedder-tracing.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/incremental-marking.h"
#include "src/init/v8.h"

namespace v8 {
namespace internal {

class IncrementalMarkingJob::Task : public CancelableTask {
 public:
  Task(Isolate* isolate, IncrementalMarkingJob* job,
       EmbedderHeapTracer::EmbedderStackState stack_state, TaskType task_type)
      : CancelableTask(isolate),
        isolate_(isolate),
        job_(job),
        stack_state_(stack_state),
        task_type_(task_type) {}

  // CancelableTask overrides.
  void RunInternal() override;

  Isolate* isolate() const { return isolate_; }

 private:
  Isolate* const isolate_;
  IncrementalMarkingJob* const job_;
  const EmbedderHeapTracer::EmbedderStackState stack_state_;
  const TaskType task_type_;
};

void IncrementalMarkingJob::Start(Heap* heap) {
  DCHECK(!heap->incremental_marking()->IsStopped());
  ScheduleTask(heap);
}

void IncrementalMarkingJob::ScheduleTask(Heap* heap, TaskType task_type) {
  base::MutexGuard guard(&mutex_);

  if (IsTaskPending(task_type) || heap->IsTearingDown() ||
      !FLAG_incremental_marking_task) {
    return;
  }

  v8::Isolate* isolate = reinterpret_cast<v8::Isolate*>(heap->isolate());
  SetTaskPending(task_type, true);
  auto taskrunner = V8::GetCurrentPlatform()->GetForegroundTaskRunner(isolate);

  const EmbedderHeapTracer::EmbedderStackState stack_state =
      taskrunner->NonNestableTasksEnabled()
          ? EmbedderHeapTracer::EmbedderStackState::kNoHeapPointers
          : EmbedderHeapTracer::EmbedderStackState::kMayContainHeapPointers;

  auto task =
      std::make_unique<Task>(heap->isolate(), this, stack_state, task_type);

  if (task_type == TaskType::kNormal) {
    scheduled_time_ = heap->MonotonicallyIncreasingTimeInMs();

    if (taskrunner->NonNestableTasksEnabled()) {
      taskrunner->PostNonNestableTask(std::move(task));
    } else {
      taskrunner->PostTask(std::move(task));
    }
  } else {
    if (taskrunner->NonNestableDelayedTasksEnabled()) {
      taskrunner->PostNonNestableDelayedTask(std::move(task), kDelayInSeconds);
    } else {
      taskrunner->PostDelayedTask(std::move(task), kDelayInSeconds);
    }
  }
}

void IncrementalMarkingJob::Task::RunInternal() {
  VMState<GC> state(isolate());
  TRACE_EVENT_CALL_STATS_SCOPED(isolate(), "v8", "V8.Task");

  Heap* heap = isolate()->heap();
  EmbedderStackStateScope scope(
      heap, EmbedderStackStateScope::kImplicitThroughTask, stack_state_);
  if (task_type_ == TaskType::kNormal) {
    heap->tracer()->RecordTimeToIncrementalMarkingTask(
        heap->MonotonicallyIncreasingTimeInMs() - job_->scheduled_time_);
    job_->scheduled_time_ = 0.0;
  }

  IncrementalMarking* incremental_marking = heap->incremental_marking();
  if (incremental_marking->IsStopped()) {
    if (heap->IncrementalMarkingLimitReached() !=
        Heap::IncrementalMarkingLimit::kNoLimit) {
      heap->StartIncrementalMarking(heap->GCFlagsForIncrementalMarking(),
                                    GarbageCollectionReason::kTask,
                                    kGCCallbackScheduleIdleGarbageCollection);
    }
  }

  // Clear this flag after StartIncrementalMarking call to avoid
  // scheduling a new task when starting incremental marking.
  {
    base::MutexGuard guard(&job_->mutex_);
    job_->SetTaskPending(task_type_, false);
  }

  if (incremental_marking->IsRunning()) {
    // All objects are initialized at that point.
    heap->new_space()->MarkLabStartInitialized();
    heap->new_lo_space()->ResetPendingObject();

    heap->incremental_marking()->AdvanceAndFinalizeIfComplete();

    if (incremental_marking->IsRunning()) {
      // TODO(v8:12775): It is quite suprising that we schedule the task
      // immediately here. This was introduced since delayed task were
      // unreliable at some point. Investigate whether this is still the case
      // and whether this could be improved.
      job_->ScheduleTask(heap, TaskType::kNormal);
    }
  }
}

double IncrementalMarkingJob::CurrentTimeToTask(Heap* heap) const {
  if (scheduled_time_ == 0.0) return 0.0;

  return heap->MonotonicallyIncreasingTimeInMs() - scheduled_time_;
}

}  // namespace internal
}  // namespace v8
