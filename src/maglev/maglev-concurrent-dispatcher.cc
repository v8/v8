// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-concurrent-dispatcher.h"

#include "src/flags/flags.h"
#include "src/objects/js-function.h"
#include "src/utils/locked-queue-inl.h"

namespace v8 {
namespace internal {
namespace maglev {

class MaglevConcurrentDispatcher::Job final {
 public:
  explicit Job(Handle<JSFunction> function) : function_(function) {}

  void Install(Isolate* isolate) { UNIMPLEMENTED(); }

 private:
  const Handle<JSFunction> function_;
};

class MaglevConcurrentDispatcher::JobTask final : public v8::JobTask {
 public:
  explicit JobTask(MaglevConcurrentDispatcher* dispatcher)
      : dispatcher_(dispatcher) {}

  void Run(JobDelegate* delegate) override {}

  size_t GetMaxConcurrency(size_t) const override {
    return incoming_queue()->size();
  }

 private:
  QueueT* incoming_queue() const { return &dispatcher_->incoming_queue_; }
  QueueT* outgoing_queue() const { return &dispatcher_->outgoing_queue_; }

  MaglevConcurrentDispatcher* const dispatcher_;
  const Handle<JSFunction> function_;
};

MaglevConcurrentDispatcher::MaglevConcurrentDispatcher(Isolate* isolate)
    : isolate_(isolate) {
  if (FLAG_concurrent_recompilation && FLAG_maglev) {
    job_handle_ = V8::GetCurrentPlatform()->PostJob(
        TaskPriority::kUserVisible, std::make_unique<JobTask>(this));
    DCHECK(is_enabled());
  } else {
    DCHECK(!is_enabled());
  }
}

MaglevConcurrentDispatcher::~MaglevConcurrentDispatcher() {
  if (is_enabled() && job_handle_->IsValid()) {
    // Wait for the job handle to complete, so that we know the queue
    // pointers are safe.
    job_handle_->Cancel();
  }
}

void MaglevConcurrentDispatcher::EnqueueJob(Handle<JSFunction> function) {
  DCHECK(is_enabled());
  // TODO(v8:7700): RCS.
  // RCS_SCOPE(isolate_, RuntimeCallCounterId::kCompileMaglev);
  incoming_queue_.Enqueue(std::make_unique<Job>(function));
  job_handle_->NotifyConcurrencyIncrease();
}

void MaglevConcurrentDispatcher::ProcessFinishedJobs() {
  while (!outgoing_queue_.IsEmpty()) {
    std::unique_ptr<Job> job;
    outgoing_queue_.Dequeue(&job);
    job->Install(isolate_);
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
