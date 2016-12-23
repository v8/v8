// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/compiler-dispatcher.h"

#include "include/v8-platform.h"
#include "include/v8.h"
#include "src/base/platform/time.h"
#include "src/cancelable-task.h"
#include "src/compiler-dispatcher/compiler-dispatcher-job.h"
#include "src/compiler-dispatcher/compiler-dispatcher-tracer.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

namespace {

enum class ExceptionHandling { kSwallow, kThrow };

bool DoNextStepOnMainThread(Isolate* isolate, CompilerDispatcherJob* job,
                            ExceptionHandling exception_handling) {
  DCHECK(ThreadId::Current().Equals(isolate->thread_id()));
  v8::TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));
  switch (job->status()) {
    case CompileJobStatus::kInitial:
      job->PrepareToParseOnMainThread();
      break;

    case CompileJobStatus::kReadyToParse:
      job->Parse();
      break;

    case CompileJobStatus::kParsed:
      job->FinalizeParsingOnMainThread();
      break;

    case CompileJobStatus::kReadyToAnalyse:
      job->PrepareToCompileOnMainThread();
      break;

    case CompileJobStatus::kReadyToCompile:
      job->Compile();
      break;

    case CompileJobStatus::kCompiled:
      job->FinalizeCompilingOnMainThread();
      break;

    case CompileJobStatus::kFailed:
    case CompileJobStatus::kDone:
      break;
  }

  if (exception_handling == ExceptionHandling::kThrow &&
      try_catch.HasCaught()) {
    DCHECK(job->status() == CompileJobStatus::kFailed);
    try_catch.ReThrow();
  }
  return job->status() != CompileJobStatus::kFailed;
}

bool IsFinished(CompilerDispatcherJob* job) {
  return job->status() == CompileJobStatus::kDone ||
         job->status() == CompileJobStatus::kFailed;
}

// Theoretically we get 50ms of idle time max, however it's unlikely that
// we'll get all of it so try to be a conservative.
const double kMaxIdleTimeToExpectInMs = 40;

}  // namespace

class CompilerDispatcher::IdleTask : public CancelableIdleTask {
 public:
  IdleTask(Isolate* isolate, CompilerDispatcher* dispatcher);
  ~IdleTask() override;

  // CancelableIdleTask implementation.
  void RunInternal(double deadline_in_seconds) override;

 private:
  CompilerDispatcher* dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(IdleTask);
};

CompilerDispatcher::IdleTask::IdleTask(Isolate* isolate,
                                       CompilerDispatcher* dispatcher)
    : CancelableIdleTask(isolate), dispatcher_(dispatcher) {}

CompilerDispatcher::IdleTask::~IdleTask() {}

void CompilerDispatcher::IdleTask::RunInternal(double deadline_in_seconds) {
  dispatcher_->DoIdleWork(deadline_in_seconds);
}

CompilerDispatcher::CompilerDispatcher(Isolate* isolate, Platform* platform,
                                       size_t max_stack_size)
    : isolate_(isolate),
      platform_(platform),
      max_stack_size_(max_stack_size),
      tracer_(new CompilerDispatcherTracer(isolate_)),
      idle_task_scheduled_(false) {}

CompilerDispatcher::~CompilerDispatcher() {
  // To avoid crashing in unit tests due to unfished jobs.
  AbortAll(BlockingBehavior::kBlock);
}

bool CompilerDispatcher::Enqueue(Handle<SharedFunctionInfo> function) {
  // We only handle functions (no eval / top-level code / wasm) that are
  // attached to a script.
  if (!function->script()->IsScript() || !function->is_function() ||
      function->asm_function() || function->native()) {
    return false;
  }

  if (IsEnqueued(function)) return true;
  std::unique_ptr<CompilerDispatcherJob> job(new CompilerDispatcherJob(
      isolate_, tracer_.get(), function, max_stack_size_));
  std::pair<int, int> key(Script::cast(function->script())->id(),
                          function->function_literal_id());
  jobs_.insert(std::make_pair(key, std::move(job)));
  ScheduleIdleTaskIfNeeded();
  return true;
}

bool CompilerDispatcher::IsEnqueued(Handle<SharedFunctionInfo> function) const {
  return GetJobFor(function) != jobs_.end();
}

bool CompilerDispatcher::FinishNow(Handle<SharedFunctionInfo> function) {
  JobMap::const_iterator job = GetJobFor(function);
  CHECK(job != jobs_.end());

  // TODO(jochen): Check if there's an in-flight background task working on this
  // job.
  while (!IsFinished(job->second.get())) {
    DoNextStepOnMainThread(isolate_, job->second.get(),
                           ExceptionHandling::kThrow);
  }
  bool result = job->second->status() != CompileJobStatus::kFailed;
  job->second->ResetOnMainThread();
  jobs_.erase(job);
  return result;
}

void CompilerDispatcher::Abort(Handle<SharedFunctionInfo> function,
                               BlockingBehavior blocking) {
  USE(blocking);
  JobMap::const_iterator job = GetJobFor(function);
  CHECK(job != jobs_.end());

  // TODO(jochen): Check if there's an in-flight background task working on this
  // job.
  job->second->ResetOnMainThread();
  jobs_.erase(job);
}

void CompilerDispatcher::AbortAll(BlockingBehavior blocking) {
  USE(blocking);
  // TODO(jochen): Check if there's an in-flight background task working on this
  // job.
  for (auto& kv : jobs_) {
    kv.second->ResetOnMainThread();
  }
  jobs_.clear();
}

CompilerDispatcher::JobMap::const_iterator CompilerDispatcher::GetJobFor(
    Handle<SharedFunctionInfo> shared) const {
  if (!shared->script()->IsScript()) return jobs_.end();
  std::pair<int, int> key(Script::cast(shared->script())->id(),
                          shared->function_literal_id());
  auto range = jobs_.equal_range(key);
  for (auto job = range.first; job != range.second; ++job) {
    if (job->second->IsAssociatedWith(shared)) return job;
  }
  return jobs_.end();
}

void CompilerDispatcher::ScheduleIdleTaskIfNeeded() {
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate_);
  if (!platform_->IdleTasksEnabled(v8_isolate)) return;
  if (idle_task_scheduled_) return;
  if (jobs_.empty()) return;
  idle_task_scheduled_ = true;
  platform_->CallIdleOnForegroundThread(v8_isolate,
                                        new IdleTask(isolate_, this));
}

void CompilerDispatcher::DoIdleWork(double deadline_in_seconds) {
  idle_task_scheduled_ = false;

  // Number of jobs that are unlikely to make progress during any idle callback
  // due to their estimated duration.
  size_t too_long_jobs = 0;

  // Iterate over all available jobs & remaining time. For each job, decide
  // whether to 1) skip it (if it would take too long), 2) erase it (if it's
  // finished), or 3) make progress on it.
  double idle_time_in_seconds =
      deadline_in_seconds - platform_->MonotonicallyIncreasingTime();
  for (auto job = jobs_.begin();
       job != jobs_.end() && idle_time_in_seconds > 0.0;
       idle_time_in_seconds =
           deadline_in_seconds - platform_->MonotonicallyIncreasingTime()) {
    double estimate_in_ms = job->second->EstimateRuntimeOfNextStepInMs();
    if (idle_time_in_seconds <
        (estimate_in_ms /
         static_cast<double>(base::Time::kMillisecondsPerSecond))) {
      // If there's not enough time left, try to estimate whether we would
      // have managed to finish the job in a large idle task to assess
      // whether we should ask for another idle callback.
      if (estimate_in_ms > kMaxIdleTimeToExpectInMs) ++too_long_jobs;
      ++job;
    } else if (IsFinished(job->second.get())) {
      job->second->ResetOnMainThread();
      job = jobs_.erase(job);
      break;
    } else {
      // Do one step, and keep processing the job (as we don't advance the
      // iterator).
      DoNextStepOnMainThread(isolate_, job->second.get(),
                             ExceptionHandling::kSwallow);
    }
  }
  if (jobs_.size() > too_long_jobs) ScheduleIdleTaskIfNeeded();
}

}  // namespace internal
}  // namespace v8
