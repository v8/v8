// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARKING_WORKLIST_H_
#define V8_HEAP_MARKING_WORKLIST_H_

#include "src/heap/marking.h"
#include "src/heap/worklist.h"
#include "src/objects/heap-object.h"

namespace v8 {
namespace internal {

using MarkingWorklist = Worklist<HeapObject, 64>;
using EmbedderTracingWorklist = Worklist<HeapObject, 16>;

// The index of the main thread task used by concurrent/parallel GC.
const int kMainThreadTask = 0;

// A helper class that owns all marking worklists.
class MarkingWorklistsHolder {
 public:
  // Calls the specified callback on each element of the deques and replaces
  // the element with the result of the callback. If the callback returns
  // nullptr then the element is removed from the deque.
  // The callback must accept HeapObject and return HeapObject.
  template <typename Callback>
  void Update(Callback callback) {
    shared_.Update(callback);
    on_hold_.Update(callback);
    embedder_.Update(callback);
  }

  MarkingWorklist* shared() { return &shared_; }
  MarkingWorklist* on_hold() { return &on_hold_; }
  EmbedderTracingWorklist* embedder() { return &embedder_; }

  void Clear();
  void Print();

 private:
  // Prints the stats about the global pool of the worklist.
  void PrintWorklist(const char* worklist_name, MarkingWorklist* worklist);

  // Worklist used for most objects.
  MarkingWorklist shared_;

  // Concurrent marking uses this worklist to bail out of marking objects
  // in new space's linear allocation area. Used to avoid black allocation
  // for new space. This allow the compiler to remove write barriers
  // for freshly allocatd objects.
  MarkingWorklist on_hold_;

  // Worklist for objects that potentially require embedder tracing, i.e.,
  // these objects need to be handed over to the embedder to find the full
  // transitive closure.
  EmbedderTracingWorklist embedder_;
};

// A thread-local view of the marking worklists.
class V8_EXPORT_PRIVATE MarkingWorklists {
 public:
  MarkingWorklists(int task_id, MarkingWorklistsHolder* holder);

  void Push(HeapObject object) {
    bool success = shared_->Push(task_id_, object);
    USE(success);
    DCHECK(success);
  }

  bool Pop(HeapObject* object) { return shared_->Pop(task_id_, object); }

  void PushOnHold(HeapObject object) {
    DCHECK_NE(kMainThreadTask, task_id_);
    bool success = on_hold_->Push(task_id_, object);
    USE(success);
    DCHECK(success);
  }

  bool PopOnHold(HeapObject* object) {
    DCHECK_EQ(kMainThreadTask, task_id_);
    return on_hold_->Pop(task_id_, object);
  }

  void PushEmbedder(HeapObject object) {
    bool success = embedder_->Push(task_id_, object);
    USE(success);
    DCHECK(success);
  }

  bool PopEmbedder(HeapObject* object) {
    return embedder_->Pop(task_id_, object);
  }

  void FlushToGlobal();
  bool IsEmpty();
  bool IsEmbedderEmpty();
  void MergeOnHold();
  void ShareWorkIfGlobalPoolIsEmpty();

 private:
  MarkingWorklist* shared_;
  MarkingWorklist* on_hold_;
  EmbedderTracingWorklist* embedder_;
  int task_id_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARKING_WORKLIST_H_
