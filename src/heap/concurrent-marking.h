// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CONCURRENT_MARKING_
#define V8_HEAP_CONCURRENT_MARKING_

#include "src/allocation.h"
#include "src/cancelable-task.h"
#include "src/utils.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

class Heap;
class Isolate;
template <typename EntryType, int SEGMENT_SIZE>
class Worklist;

class ConcurrentMarking {
 public:
  using MarkingWorklist = Worklist<HeapObject*, 64 /* segment size */>;

  ConcurrentMarking(Heap* heap, MarkingWorklist* shared_,
                    MarkingWorklist* bailout_);

  void StartTask();
  void WaitForTaskToComplete();
  bool IsTaskPending() { return is_task_pending_; }
  void EnsureTaskCompleted();

 private:
  class Task;
  void Run(int task_id);
  Heap* heap_;
  base::Semaphore pending_task_semaphore_;
  MarkingWorklist* shared_;
  MarkingWorklist* bailout_;
  bool is_task_pending_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_PAGE_PARALLEL_JOB_
