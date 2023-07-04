// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/default-worker-threads-task-runner.h"

#include "src/base/platform/time.h"
#include "src/libplatform/delayed-task-queue.h"

namespace v8 {
namespace platform {

DefaultWorkerThreadsTaskRunner::DefaultWorkerThreadsTaskRunner(
    uint32_t thread_pool_size, TimeFunction time_function)
    : queue_(time_function), time_function_(time_function) {
  for (uint32_t i = 0; i < thread_pool_size; ++i) {
    thread_pool_.push_back(std::make_unique<WorkerThread>(this));
  }
}

DefaultWorkerThreadsTaskRunner::~DefaultWorkerThreadsTaskRunner() = default;

double DefaultWorkerThreadsTaskRunner::MonotonicallyIncreasingTime() {
  return time_function_();
}

void DefaultWorkerThreadsTaskRunner::Terminate() {
  {
    base::MutexGuard guard(&lock_);
    terminated_ = true;
    queue_.Terminate();
  }
  condition_var_.NotifyAll();
  // Clearing the thread pool lets all worker threads join.
  thread_pool_.clear();
}

void DefaultWorkerThreadsTaskRunner::PostTask(std::unique_ptr<Task> task) {
  {
    base::MutexGuard guard(&lock_);
    if (terminated_) return;
    queue_.Append(std::move(task));
  }
  condition_var_.NotifyOne();
}

void DefaultWorkerThreadsTaskRunner::PostDelayedTask(std::unique_ptr<Task> task,
                                                     double delay_in_seconds) {
  {
    base::MutexGuard guard(&lock_);
    if (terminated_) return;
    queue_.AppendDelayed(std::move(task), delay_in_seconds);
  }
  condition_var_.NotifyOne();
}

void DefaultWorkerThreadsTaskRunner::PostIdleTask(
    std::unique_ptr<IdleTask> task) {
  // There are no idle worker tasks.
  UNREACHABLE();
}

bool DefaultWorkerThreadsTaskRunner::IdleTasksEnabled() {
  // There are no idle worker tasks.
  return false;
}

std::unique_ptr<Task> DefaultWorkerThreadsTaskRunner::GetNext() {
  base::MutexGuard guard(&lock_);
  while (true) {
    DelayedTaskQueue::MaybeNextTask next_task = queue_.TryGetNext();
    switch (next_task.state) {
      case DelayedTaskQueue::MaybeNextTask::kTask:
        return std::move(next_task.task);
      case DelayedTaskQueue::MaybeNextTask::kTerminated:
        return {};
      case DelayedTaskQueue::MaybeNextTask::kWaitIndefinite:
        condition_var_.Wait(&lock_);
        continue;
      case DelayedTaskQueue::MaybeNextTask::kWaitDelayed:
        // WaitFor unfortunately doesn't care about our fake time and will wait
        // the 'real' amount of time, based on whatever clock the system call
        // uses.
        bool notified = condition_var_.WaitFor(&lock_, next_task.wait_time);
        USE(notified);
        continue;
    }
  }
}

DefaultWorkerThreadsTaskRunner::WorkerThread::WorkerThread(
    DefaultWorkerThreadsTaskRunner* runner)
    : Thread(Options("V8 DefaultWorkerThreadsTaskRunner WorkerThread")),
      runner_(runner) {
  CHECK(Start());
}

DefaultWorkerThreadsTaskRunner::WorkerThread::~WorkerThread() { Join(); }

void DefaultWorkerThreadsTaskRunner::WorkerThread::Run() {
  while (std::unique_ptr<Task> task = runner_->GetNext()) {
    task->Run();
  }
}

}  // namespace platform
}  // namespace v8
