// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/compiler-dispatcher.h"

#include "src/compiler-dispatcher/compiler-dispatcher-job.h"
#include "src/compiler-dispatcher/compiler-dispatcher-tracer.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

namespace {

bool DoNextStepOnMainThread(CompilerDispatcherJob* job) {
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

  return job->status() != CompileJobStatus::kFailed;
}

bool IsFinished(CompilerDispatcherJob* job) {
  return job->status() == CompileJobStatus::kDone ||
         job->status() == CompileJobStatus::kFailed;
}

}  // namespace

CompilerDispatcher::CompilerDispatcher(Isolate* isolate, size_t max_stack_size)
    : isolate_(isolate),
      max_stack_size_(max_stack_size),
      tracer_(new CompilerDispatcherTracer(isolate_)) {}

CompilerDispatcher::~CompilerDispatcher() {}

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
    DoNextStepOnMainThread(job->second.get());
  }
  bool result = job->second->status() != CompileJobStatus::kFailed;
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
  jobs_.erase(job);
}

void CompilerDispatcher::AbortAll(BlockingBehavior blocking) {
  USE(blocking);
  // TODO(jochen): Check if there's an in-flight background task working on this
  // job.
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

}  // namespace internal
}  // namespace v8
