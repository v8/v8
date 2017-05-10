// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/concurrent-marking.h"

#include <stack>
#include <unordered_map>

#include "src/heap/concurrent-marking-deque.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/marking.h"
#include "src/heap/objects-visiting-inl.h"
#include "src/heap/objects-visiting.h"
#include "src/isolate.h"
#include "src/locked-queue-inl.h"
#include "src/utils-inl.h"
#include "src/utils.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

class ConcurrentMarkingVisitor final
    : public HeapVisitor<int, ConcurrentMarkingVisitor> {
 public:
  using BaseClass = HeapVisitor<int, ConcurrentMarkingVisitor>;

  explicit ConcurrentMarkingVisitor(ConcurrentMarkingDeque* deque)
      : deque_(deque) {}

  bool ShouldVisit(HeapObject* object) override {
    return ObjectMarking::GreyToBlack<MarkBit::AccessMode::ATOMIC>(
        object, marking_state(object));
  }

  void VisitPointers(HeapObject* host, Object** start, Object** end) override {
    for (Object** p = start; p < end; p++) {
      Object* object = reinterpret_cast<Object*>(
          base::NoBarrier_Load(reinterpret_cast<const base::AtomicWord*>(p)));
      if (!object->IsHeapObject()) continue;
      MarkObject(HeapObject::cast(object));
    }
  }

  // ===========================================================================
  // JS object =================================================================
  // ===========================================================================

  int VisitJSObject(Map* map, JSObject* object) override {
    // TODO(ulan): impement snapshot iteration.
    return BaseClass::VisitJSObject(map, object);
  }

  int VisitJSObjectFast(Map* map, JSObject* object) override {
    return VisitJSObject(map, object);
  }

  int VisitJSApiObject(Map* map, JSObject* object) override {
    return VisitJSObject(map, object);
  }

  // ===========================================================================
  // Fixed array object ========================================================
  // ===========================================================================

  int VisitFixedArray(Map* map, FixedArray* object) override {
    // TODO(ulan): implement iteration with prefetched length.
    return BaseClass::VisitFixedArray(map, object);
  }

  // ===========================================================================
  // Code object ===============================================================
  // ===========================================================================

  int VisitCode(Map* map, Code* object) override {
    deque_->Push(object, MarkingThread::kConcurrent, TargetDeque::kBailout);
    return 0;
  }

  // ===========================================================================
  // Objects with weak fields and/or side-effectiful visitation.
  // ===========================================================================

  int VisitBytecodeArray(Map* map, BytecodeArray* object) override {
    // TODO(ulan): implement iteration of strong fields.
    deque_->Push(object, MarkingThread::kConcurrent, TargetDeque::kBailout);
    return 0;
  }

  int VisitJSFunction(Map* map, JSFunction* object) override {
    // TODO(ulan): implement iteration of strong fields.
    deque_->Push(object, MarkingThread::kConcurrent, TargetDeque::kBailout);
    return 0;
  }

  int VisitMap(Map* map, Map* object) override {
    // TODO(ulan): implement iteration of strong fields.
    deque_->Push(object, MarkingThread::kConcurrent, TargetDeque::kBailout);
    return 0;
  }

  int VisitNativeContext(Map* map, Context* object) override {
    // TODO(ulan): implement iteration of strong fields.
    deque_->Push(object, MarkingThread::kConcurrent, TargetDeque::kBailout);
    return 0;
  }

  int VisitSharedFunctionInfo(Map* map, SharedFunctionInfo* object) override {
    // TODO(ulan): implement iteration of strong fields.
    deque_->Push(object, MarkingThread::kConcurrent, TargetDeque::kBailout);
    return 0;
  }

  int VisitTransitionArray(Map* map, TransitionArray* object) override {
    // TODO(ulan): implement iteration of strong fields.
    deque_->Push(object, MarkingThread::kConcurrent, TargetDeque::kBailout);
    return 0;
  }

  int VisitWeakCell(Map* map, WeakCell* object) override {
    // TODO(ulan): implement iteration of strong fields.
    deque_->Push(object, MarkingThread::kConcurrent, TargetDeque::kBailout);
    return 0;
  }

  int VisitJSWeakCollection(Map* map, JSWeakCollection* object) override {
    // TODO(ulan): implement iteration of strong fields.
    deque_->Push(object, MarkingThread::kConcurrent, TargetDeque::kBailout);
    return 0;
  }

  void MarkObject(HeapObject* object) {
    if (ObjectMarking::WhiteToGrey<MarkBit::AccessMode::ATOMIC>(
            object, marking_state(object))) {
      deque_->Push(object, MarkingThread::kConcurrent, TargetDeque::kShared);
    }
  }

 private:
  MarkingState marking_state(HeapObject* object) const {
    return MarkingState::Internal(object);
  }

  ConcurrentMarkingDeque* deque_;
};

class ConcurrentMarking::Task : public CancelableTask {
 public:
  Task(Isolate* isolate, ConcurrentMarking* concurrent_marking,
       base::Semaphore* on_finish)
      : CancelableTask(isolate),
        concurrent_marking_(concurrent_marking),
        on_finish_(on_finish) {}

  virtual ~Task() {}

 private:
  // v8::internal::CancelableTask overrides.
  void RunInternal() override {
    concurrent_marking_->Run();
    on_finish_->Signal();
  }

  ConcurrentMarking* concurrent_marking_;
  base::Semaphore* on_finish_;
  DISALLOW_COPY_AND_ASSIGN(Task);
};

ConcurrentMarking::ConcurrentMarking(Heap* heap, ConcurrentMarkingDeque* deque)
    : heap_(heap),
      pending_task_semaphore_(0),
      deque_(deque),
      visitor_(new ConcurrentMarkingVisitor(deque_)),
      is_task_pending_(false) {
  // Concurrent marking does not work with double unboxing.
  STATIC_ASSERT(!(V8_CONCURRENT_MARKING && V8_DOUBLE_FIELDS_UNBOXING));
  // The runtime flag should be set only if the compile time flag was set.
  CHECK(!FLAG_concurrent_marking || V8_CONCURRENT_MARKING);
}

ConcurrentMarking::~ConcurrentMarking() { delete visitor_; }

void ConcurrentMarking::Run() {
  double time_ms = heap_->MonotonicallyIncreasingTimeInMs();
  size_t bytes_marked = 0;
  base::Mutex* relocation_mutex = heap_->relocation_mutex();
  {
    TimedScope scope(&time_ms);
    HeapObject* object;
    while ((object = deque_->Pop(MarkingThread::kConcurrent)) != nullptr) {
      base::LockGuard<base::Mutex> guard(relocation_mutex);
      bytes_marked += visitor_->Visit(object);
    }
  }
  if (FLAG_trace_concurrent_marking) {
    heap_->isolate()->PrintWithTimestamp("concurrently marked %dKB in %.2fms\n",
                                         static_cast<int>(bytes_marked / KB),
                                         time_ms);
  }
}

void ConcurrentMarking::StartTask() {
  if (!FLAG_concurrent_marking) return;
  is_task_pending_ = true;
  V8::GetCurrentPlatform()->CallOnBackgroundThread(
      new Task(heap_->isolate(), this, &pending_task_semaphore_),
      v8::Platform::kShortRunningTask);
}

void ConcurrentMarking::WaitForTaskToComplete() {
  if (!FLAG_concurrent_marking) return;
  pending_task_semaphore_.Wait();
  is_task_pending_ = false;
}

void ConcurrentMarking::EnsureTaskCompleted() {
  if (IsTaskPending()) {
    WaitForTaskToComplete();
  }
}

}  // namespace internal
}  // namespace v8
