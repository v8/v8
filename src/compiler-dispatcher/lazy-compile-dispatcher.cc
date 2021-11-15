// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/lazy-compile-dispatcher.h"

#include <atomic>

#include "include/v8-platform.h"
#include "src/ast/ast.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/codegen/compiler.h"
#include "src/flags/flags.h"
#include "src/handles/global-handles-inl.h"
#include "src/logging/counters.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser.h"
#include "src/tasks/cancelable-task.h"
#include "src/tasks/task-utils.h"
#include "src/zone/zone-list-inl.h"  // crbug.com/v8/8816

namespace v8 {
namespace internal {

class LazyCompileDispatcher::JobTask : public v8::JobTask {
 public:
  explicit JobTask(LazyCompileDispatcher* lazy_compile_dispatcher)
      : lazy_compile_dispatcher_(lazy_compile_dispatcher) {}

  void Run(JobDelegate* delegate) final {
    lazy_compile_dispatcher_->DoBackgroundWork(delegate);
  }

  size_t GetMaxConcurrency(size_t worker_count) const final {
    return lazy_compile_dispatcher_->num_jobs_for_background_.load(
        std::memory_order_relaxed);
  }

 private:
  LazyCompileDispatcher* lazy_compile_dispatcher_;
};

LazyCompileDispatcher::Job::Job(std::unique_ptr<BackgroundCompileTask> task)
    : task(std::move(task)), state(Job::State::kPending) {}

LazyCompileDispatcher::Job::~Job() = default;

LazyCompileDispatcher::LazyCompileDispatcher(Isolate* isolate,
                                             Platform* platform,
                                             size_t max_stack_size)
    : isolate_(isolate),
      worker_thread_runtime_call_stats_(
          isolate->counters()->worker_thread_runtime_call_stats()),
      background_compile_timer_(
          isolate->counters()->compile_function_on_background()),
      taskrunner_(platform->GetForegroundTaskRunner(
          reinterpret_cast<v8::Isolate*>(isolate))),
      platform_(platform),
      max_stack_size_(max_stack_size),
      trace_compiler_dispatcher_(FLAG_trace_compiler_dispatcher),
      idle_task_manager_(new CancelableTaskManager()),
      shared_to_unoptimized_job_(isolate->heap()),
      idle_task_scheduled_(false),
      num_jobs_for_background_(0),
      main_thread_blocking_on_job_(nullptr),
      block_for_testing_(false),
      semaphore_for_testing_(0) {
  job_handle_ = platform_->PostJob(TaskPriority::kUserVisible,
                                   std::make_unique<JobTask>(this));
}

LazyCompileDispatcher::~LazyCompileDispatcher() {
  // AbortAll must be called before LazyCompileDispatcher is destroyed.
  CHECK(!job_handle_->IsValid());
}

void LazyCompileDispatcher::Enqueue(
    LocalIsolate* isolate, Handle<SharedFunctionInfo> shared_info,
    std::unique_ptr<Utf16CharacterStream> character_stream,
    ProducedPreparseData* preparse_data) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.LazyCompilerDispatcherEnqueue");
  RCS_SCOPE(isolate, RuntimeCallCounterId::kCompileEnqueueOnDispatcher);

  std::unique_ptr<Job> job =
      std::make_unique<Job>(std::make_unique<BackgroundCompileTask>(
          isolate_, shared_info, std::move(character_stream), preparse_data,
          worker_thread_runtime_call_stats_, background_compile_timer_,
          static_cast<int>(max_stack_size_)));

  // Post a a background worker task to perform the compilation on the worker
  // thread.
  {
    base::MutexGuard lock(&mutex_);
    if (trace_compiler_dispatcher_) {
      PrintF("LazyCompileDispatcher: enqueued job for  ");
      shared_info->ShortPrint();
      PrintF("\n");
    }

    pending_background_jobs_.insert(job.get());
    // Transfer ownership of the job to the IdentityMap
    shared_to_unoptimized_job_.Insert(shared_info, job.release());

    num_jobs_for_background_ += 1;
    VerifyBackgroundTaskCount(lock);
  }
  job_handle_->NotifyConcurrencyIncrease();
}

bool LazyCompileDispatcher::IsEnqueued(
    Handle<SharedFunctionInfo> function) const {
  base::MutexGuard lock(&mutex_);
  return shared_to_unoptimized_job_.Find(function) != nullptr;
}

void LazyCompileDispatcher::WaitForJobIfRunningOnBackground(
    Job* job, const base::MutexGuard& lock) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.LazyCompilerDispatcherWaitForBackgroundJob");
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kCompileWaitForDispatcher);

  if (!job->is_running_on_background()) {
    num_jobs_for_background_ -= pending_background_jobs_.erase(job);
    if (job->state == Job::State::kPending) {
      job->state = Job::State::kPendingToRunOnForeground;
    } else {
      DCHECK_EQ(job->state, Job::State::kReadyToFinalize);
    }
    VerifyBackgroundTaskCount(lock);
    return;
  }
  DCHECK_NULL(main_thread_blocking_on_job_);
  main_thread_blocking_on_job_ = job;
  while (main_thread_blocking_on_job_ != nullptr) {
    main_thread_blocking_signal_.Wait(&mutex_);
  }
  DCHECK(pending_background_jobs_.find(job) == pending_background_jobs_.end());
}

bool LazyCompileDispatcher::FinishNow(Handle<SharedFunctionInfo> function) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.LazyCompilerDispatcherFinishNow");
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kCompileFinishNowOnDispatcher);
  if (trace_compiler_dispatcher_) {
    PrintF("LazyCompileDispatcher: finishing ");
    function->ShortPrint();
    PrintF(" now\n");
  }

  Job* job;

  {
    base::MutexGuard lock(&mutex_);
    job = GetJobFor(function, lock);
    WaitForJobIfRunningOnBackground(job, lock);
    shared_to_unoptimized_job_.Delete(function, &job);
  }

  if (job->state == Job::State::kPendingToRunOnForeground) {
    job->task->Run();
    job->state = Job::State::kReadyToFinalize;
  }

  DCHECK_EQ(job->state, Job::State::kReadyToFinalize);
  bool success = Compiler::FinalizeBackgroundCompileTask(
      job->task.get(), isolate_, Compiler::KEEP_EXCEPTION);

  DCHECK_NE(success, isolate_->has_pending_exception());
  delete job;
  return success;
}

void LazyCompileDispatcher::AbortJob(Handle<SharedFunctionInfo> shared_info) {
  if (trace_compiler_dispatcher_) {
    PrintF("LazyCompileDispatcher: aborting job for ");
    shared_info->ShortPrint();
    PrintF("\n");
  }
  base::LockGuard<base::Mutex> lock(&mutex_);

  Job* job = GetJobFor(shared_info, lock);
  num_jobs_for_background_ -= pending_background_jobs_.erase(job);
  if (job->is_running_on_background()) {
    // Job is currently running on the background thread, wait until it's done
    // and remove job then.
    job->state = Job::State::kAbortRequested;
  } else {
    shared_to_unoptimized_job_.Delete(shared_info, &job);
    delete job;
  }
  VerifyBackgroundTaskCount(lock);
}

void LazyCompileDispatcher::AbortAll() {
  idle_task_manager_->TryAbortAll();
  job_handle_->Cancel();

  {
    base::MutexGuard lock(&mutex_);
    pending_background_jobs_.clear();
  }

  idle_task_manager_->CancelAndWait();

  {
    base::MutexGuard lock(&mutex_);
    SharedToJobMap::IteratableScope iteratable_scope(
        &shared_to_unoptimized_job_);
    for (Job** job_entry : iteratable_scope) {
      Job* job = *job_entry;
      DCHECK_NE(job->state, Job::State::kRunning);
      DCHECK_NE(job->state, Job::State::kAbortRequested);
      delete job;
    }
  }
  shared_to_unoptimized_job_.Clear();
}

LazyCompileDispatcher::Job* LazyCompileDispatcher::GetJobFor(
    Handle<SharedFunctionInfo> shared, const base::MutexGuard&) const {
  return *shared_to_unoptimized_job_.Find(shared);
}

void LazyCompileDispatcher::ScheduleIdleTaskFromAnyThread(
    const base::MutexGuard&) {
  if (!taskrunner_->IdleTasksEnabled()) return;
  if (idle_task_scheduled_) return;

  idle_task_scheduled_ = true;
  // TODO(leszeks): Using a full task manager for a single cancellable task is
  // overkill, we could probably do the cancelling ourselves.
  taskrunner_->PostIdleTask(MakeCancelableIdleTask(
      idle_task_manager_.get(),
      [this](double deadline_in_seconds) { DoIdleWork(deadline_in_seconds); }));
}

void LazyCompileDispatcher::DoBackgroundWork(JobDelegate* delegate) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.LazyCompileDispatcherDoBackgroundWork");
  while (!delegate->ShouldYield()) {
    Job* job = nullptr;
    {
      base::MutexGuard lock(&mutex_);
      if (pending_background_jobs_.empty()) break;
      auto it = pending_background_jobs_.begin();
      job = *it;
      pending_background_jobs_.erase(it);
      DCHECK_EQ(job->state, Job::State::kPending);
      job->state = Job::State::kRunning;
      VerifyBackgroundTaskCount(lock);
    }

    if (V8_UNLIKELY(block_for_testing_.Value())) {
      block_for_testing_.SetValue(false);
      semaphore_for_testing_.Wait();
    }

    if (trace_compiler_dispatcher_) {
      PrintF("LazyCompileDispatcher: doing background work\n");
    }

    job->task->Run();

    {
      base::MutexGuard lock(&mutex_);
      if (job->state == Job::State::kRunning) {
        job->state = Job::State::kReadyToFinalize;
        // Schedule an idle task to finalize the compilation on the main thread
        // if the job has a shared function info registered.
      } else {
        DCHECK_EQ(job->state, Job::State::kAbortRequested);
        job->state = Job::State::kAborted;
      }
      num_jobs_for_background_--;
      VerifyBackgroundTaskCount(lock);

      if (main_thread_blocking_on_job_ == job) {
        main_thread_blocking_on_job_ = nullptr;
        main_thread_blocking_signal_.NotifyOne();
      } else {
        ScheduleIdleTaskFromAnyThread(lock);
      }
    }
  }

  // Don't touch |this| anymore after this point, as it might have been
  // deleted.
}

void LazyCompileDispatcher::DoIdleWork(double deadline_in_seconds) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.LazyCompilerDispatcherDoIdleWork");
  {
    base::MutexGuard lock(&mutex_);
    idle_task_scheduled_ = false;
  }

  if (trace_compiler_dispatcher_) {
    PrintF("LazyCompileDispatcher: received %0.1lfms of idle time\n",
           (deadline_in_seconds - platform_->MonotonicallyIncreasingTime()) *
               static_cast<double>(base::Time::kMillisecondsPerSecond));
  }
  while (deadline_in_seconds > platform_->MonotonicallyIncreasingTime()) {
    // Find a job which is pending finalization and has a shared function info

    SharedFunctionInfo function;
    Job* job;
    {
      base::MutexGuard lock(&mutex_);
      SharedToJobMap::IteratableScope iteratable_scope(
          &shared_to_unoptimized_job_);

      auto it = iteratable_scope.begin();
      auto end = iteratable_scope.end();
      for (; it != end; ++it) {
        job = *it.entry();
        if (job->state == Job::State::kReadyToFinalize ||
            job->state == Job::State::kAborted) {
          function = SharedFunctionInfo::cast(it.key());
          break;
        }
      }
      // Since we hold the lock here, we can be sure no jobs have become ready
      // for finalization while we looped through the list.
      if (it == end) return;

      DCHECK_EQ(pending_background_jobs_.find(job),
                pending_background_jobs_.end());
    }
    shared_to_unoptimized_job_.Delete(function, &job);

    if (job->state == Job::State::kReadyToFinalize) {
      HandleScope scope(isolate_);
      Compiler::FinalizeBackgroundCompileTask(job->task.get(), isolate_,
                                              Compiler::CLEAR_EXCEPTION);
    } else {
      DCHECK_EQ(job->state, Job::State::kAborted);
    }
    delete job;
  }

  // We didn't return above so there still might be jobs to finalize.
  {
    base::MutexGuard lock(&mutex_);
    ScheduleIdleTaskFromAnyThread(lock);
  }
}

#ifdef DEBUG
void LazyCompileDispatcher::VerifyBackgroundTaskCount(const base::MutexGuard&) {
  int running_jobs = 0;
  int pending_jobs = 0;

  SharedToJobMap::IteratableScope iteratable_scope(&shared_to_unoptimized_job_);
  auto it = iteratable_scope.begin();
  auto end = iteratable_scope.end();
  for (; it != end; ++it) {
    Job* job = *it.entry();
    if (job->state == Job::State::kRunning ||
        job->state == Job::State::kAbortRequested) {
      running_jobs++;
    } else if (job->state == Job::State::kPending) {
      pending_jobs++;
    }
  }

  CHECK_EQ(pending_jobs, pending_background_jobs_.size());
  CHECK_EQ(num_jobs_for_background_.load(), running_jobs + pending_jobs);
}
#endif

}  // namespace internal
}  // namespace v8
