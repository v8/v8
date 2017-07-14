// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CONCURRENT_MARKING_
#define V8_HEAP_CONCURRENT_MARKING_

#include "src/allocation.h"
#include "src/cancelable-task.h"
#include "src/heap/worklist.h"
#include "src/utils.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

class Heap;
class Isolate;

class ConcurrentMarking {
 public:
  // When the scope is entered, the concurrent marking tasks
  // are paused and are not looking at the heap objects.
  class PauseScope {
   public:
    explicit PauseScope(ConcurrentMarking* concurrent_marking);
    ~PauseScope();

   private:
    ConcurrentMarking* concurrent_marking_;
  };

  static const int kTasks = 4;
  using MarkingWorklist = Worklist<HeapObject*, 64 /* segment size */>;

  ConcurrentMarking(Heap* heap, MarkingWorklist* shared_,
                    MarkingWorklist* bailout_);

  void Start();
  bool IsRunning() { return pending_task_count_ > 0; }
  void EnsureCompleted();
  // Wake up waiting tasks if the shared global pool is not empty.
  void NotifyWaitingTasks();
  // Set task exit request flag and wake up waiting tasks.
  void RequestTaskExit();
  // Returns true if all tasks are waiting. For testing only.
  bool AllTasksWaitingForTesting();

 private:
  struct TaskLock {
    base::Mutex lock;
    char cache_line_padding[64];
  };
  class Task;
  void Run(int task_id, base::Mutex* lock);
  Heap* heap_;
  MarkingWorklist* shared_;
  MarkingWorklist* bailout_;
  TaskLock task_lock_[kTasks];
  // Used by the main thread to wait for tasks to exit.
  base::Semaphore pending_task_semaphore_;
  int pending_task_count_;
  // Used by the tasks to wait for
  // - more work from the main thread
  // - or for the exit request.
  base::Mutex wait_lock_;
  base::ConditionVariable wait_condition_;
  int waiting_task_count_;
  bool task_exit_requested_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_PAGE_PARALLEL_JOB_
