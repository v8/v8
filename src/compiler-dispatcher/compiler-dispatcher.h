// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_DISPATCHER_COMPILER_DISPATCHER_H_
#define V8_COMPILER_DISPATCHER_COMPILER_DISPATCHER_H_

#include <map>
#include <memory>
#include <utility>

#include "src/base/macros.h"
#include "src/globals.h"
#include "testing/gtest/include/gtest/gtest_prod.h"

namespace v8 {

class Platform;

namespace internal {

class CompilerDispatcherJob;
class CompilerDispatcherTracer;
class Isolate;
class SharedFunctionInfo;

template <typename T>
class Handle;

// The CompilerDispatcher uses a combination of idle tasks and background tasks
// to parse and compile lazily parsed functions.
class V8_EXPORT_PRIVATE CompilerDispatcher {
 public:
  enum class BlockingBehavior { kBlock, kDontBlock };

  CompilerDispatcher(Isolate* isolate, Platform* platform,
                     size_t max_stack_size);
  ~CompilerDispatcher();

  // Returns true if a job was enqueued.
  bool Enqueue(Handle<SharedFunctionInfo> function);

  // Returns true if there is a pending job for the given function.
  bool IsEnqueued(Handle<SharedFunctionInfo> function) const;

  // Blocks until the given function is compiled (and does so as fast as
  // possible). Returns true if the compile job was succesful.
  bool FinishNow(Handle<SharedFunctionInfo> function);

  // Aborts a given job. Blocks if requested.
  void Abort(Handle<SharedFunctionInfo> function, BlockingBehavior blocking);

  // Aborts all jobs. Blocks if requested.
  void AbortAll(BlockingBehavior blocking);

 private:
  FRIEND_TEST(CompilerDispatcherTest, IdleTaskSmallIdleTime);

  typedef std::multimap<std::pair<int, int>,
                        std::unique_ptr<CompilerDispatcherJob>>
      JobMap;
  class IdleTask;

  bool IsEnabled() const;
  JobMap::const_iterator GetJobFor(Handle<SharedFunctionInfo> shared) const;
  void ScheduleIdleTaskIfNeeded();
  void DoIdleWork(double deadline_in_seconds);

  Isolate* isolate_;
  Platform* platform_;
  size_t max_stack_size_;
  std::unique_ptr<CompilerDispatcherTracer> tracer_;

  bool idle_task_scheduled_;

  // Mapping from (script id, function literal id) to job. We use a multimap,
  // as script id is not necessarily unique.
  JobMap jobs_;

  DISALLOW_COPY_AND_ASSIGN(CompilerDispatcher);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_DISPATCHER_COMPILER_DISPATCHER_H_
