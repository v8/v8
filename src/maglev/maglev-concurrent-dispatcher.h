// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_CONCURRENT_DISPATCHER_H_
#define V8_MAGLEV_MAGLEV_CONCURRENT_DISPATCHER_H_

#ifdef V8_ENABLE_MAGLEV

#include <memory>

#include "src/utils/locked-queue.h"

namespace v8 {
namespace internal {

class Isolate;

namespace maglev {

class MaglevConcurrentDispatcher final {
  class Job;
  class JobTask;

  // TODO(jgruber): There's no reason to use locking queues here, we only use
  // them for simplicity - consider replacing with lock-free data structures.
  using QueueT = LockedQueue<std::unique_ptr<Job>>;

 public:
  explicit MaglevConcurrentDispatcher(Isolate* isolate);
  ~MaglevConcurrentDispatcher();

  // Called from the main thread.
  void EnqueueJob(Handle<JSFunction> function);

  // Called from the main thread.
  void ProcessFinishedJobs();

  bool is_enabled() const { return static_cast<bool>(job_handle_); }

 private:
  Isolate* const isolate_;
  std::unique_ptr<JobHandle> job_handle_;
  QueueT incoming_queue_;
  QueueT outgoing_queue_;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_MAGLEV

#endif  // V8_MAGLEV_MAGLEV_CONCURRENT_DISPATCHER_H_
