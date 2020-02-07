// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_FINALIZATION_GROUP_CLEANUP_TASK_H_
#define V8_HEAP_FINALIZATION_GROUP_CLEANUP_TASK_H_

#include "src/objects/js-weak-refs.h"
#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

// The GC schedules a cleanup task when there the dirty FinalizationGroup list
// is non-empty. The task itself schedules another cleanup task if there are
// remaining dirty FinalizationGroups on the list.
class FinalizationGroupCleanupTask : public CancelableTask {
 public:
  explicit FinalizationGroupCleanupTask(Heap* heap);
  ~FinalizationGroupCleanupTask() override = default;

 private:
  FinalizationGroupCleanupTask(const FinalizationGroupCleanupTask&) = delete;
  void operator=(const FinalizationGroupCleanupTask&) = delete;

  void SlowAssertNoActiveJavaScript();
  void RunInternal() override;

  Heap* heap_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_FINALIZATION_GROUP_CLEANUP_TASK_H_
