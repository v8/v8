// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CONCURRENT_MARKING_
#define V8_HEAP_CONCURRENT_MARKING_

#include "src/allocation.h"
#include "src/cancelable-task.h"
#include "src/locked-queue.h"
#include "src/utils.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

class Heap;
class Isolate;

class ConcurrentMarking {
 public:
  static const int kMaxNumberOfTasks = 10;

  explicit ConcurrentMarking(Heap* heap);
  ~ConcurrentMarking();

  void EnqueueObject(HeapObject* object);
  bool IsQueueEmpty();

  void StartMarkingTasks(int number_of_tasks);
  void WaitForTasksToComplete();

 private:
  class Task;
  // TODO(ulan): Replace with faster queue.
  typedef LockedQueue<HeapObject*> Queue;

  Heap* heap_;
  base::Semaphore pending_tasks_;
  Queue queue_;
  int number_of_tasks_;
  uint32_t task_ids_[kMaxNumberOfTasks];
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_PAGE_PARALLEL_JOB_
