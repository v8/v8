// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_MARKER_H_
#define V8_HEAP_CPPGC_MARKER_H_

#include <memory>

#include "include/cppgc/heap.h"
#include "include/cppgc/trace-trait.h"
#include "include/cppgc/visitor.h"
#include "src/base/platform/time.h"
#include "src/heap/cppgc/worklist.h"

namespace cppgc {
namespace internal {

class Heap;
class HeapObjectHeader;
class MutatorThreadMarkingVisitor;

class V8_EXPORT_PRIVATE Marker {
  static constexpr int kNumConcurrentMarkers = 0;
  static constexpr int kNumMarkers = 1 + kNumConcurrentMarkers;

 public:
  static constexpr int kMutatorThreadId = 0;

  using MarkingItem = cppgc::TraceDescriptor;
  using NotFullyConstructedItem = const void*;
  struct WeakCallbackItem {
    cppgc::WeakCallback callback;
    const void* parameter;
  };

  // Segment size of 512 entries necessary to avoid throughput regressions.
  // Since the work list is currently a temporary object this is not a problem.
  using MarkingWorklist =
      Worklist<MarkingItem, 512 /* local entries */, kNumMarkers>;
  using NotFullyConstructedWorklist =
      Worklist<NotFullyConstructedItem, 16 /* local entries */, kNumMarkers>;
  using WeakCallbackWorklist =
      Worklist<WeakCallbackItem, 64 /* local entries */, kNumMarkers>;
  using WriteBarrierWorklist =
      Worklist<HeapObjectHeader*, 64 /*local entries */, kNumMarkers>;

  struct MarkingConfig {
    using StackState = cppgc::Heap::StackState;
    enum MarkingType : uint8_t {
      kAtomic,
      kIncremental,
      kIncrementalAndConcurrent
    };

    static constexpr MarkingConfig Default() { return {}; }

    StackState stack_state = StackState::kMayContainHeapPointers;
    MarkingType marking_type = MarkingType::kAtomic;
  };

  explicit Marker(Heap* heap);
  virtual ~Marker();

  Marker(const Marker&) = delete;
  Marker& operator=(const Marker&) = delete;

  // Initialize marking according to the given config. This method will
  // trigger incremental/concurrent marking if needed.
  void StartMarking(MarkingConfig config);
  // Finalize marking. This method stops incremental/concurrent marking
  // if exists and performs atomic pause marking. FinishMarking may
  // update the MarkingConfig, e.g. if the stack state has changed.
  void FinishMarking(MarkingConfig config);

  void ProcessWeakness();

  Heap* heap() { return heap_; }
  MarkingWorklist* marking_worklist() { return &marking_worklist_; }
  NotFullyConstructedWorklist* not_fully_constructed_worklist() {
    return &not_fully_constructed_worklist_;
  }
  WriteBarrierWorklist* write_barrier_worklist() {
    return &write_barrier_worklist_;
  }
  WeakCallbackWorklist* weak_callback_worklist() {
    return &weak_callback_worklist_;
  }

  void ClearAllWorklistsForTesting();

  MutatorThreadMarkingVisitor* GetMarkingVisitorForTesting() {
    return marking_visitor_.get();
  }

 protected:
  virtual std::unique_ptr<MutatorThreadMarkingVisitor>
  CreateMutatorThreadMarkingVisitor();

 private:
  void VisitRoots();

  bool AdvanceMarkingWithDeadline(v8::base::TimeDelta);
  void FlushNotFullyConstructedObjects();
  void MarkNotFullyConstructedObjects();

  Heap* const heap_;
  MarkingConfig config_ = MarkingConfig::Default();

  std::unique_ptr<MutatorThreadMarkingVisitor> marking_visitor_;

  MarkingWorklist marking_worklist_;
  NotFullyConstructedWorklist not_fully_constructed_worklist_;
  NotFullyConstructedWorklist previously_not_fully_constructed_worklist_;
  WriteBarrierWorklist write_barrier_worklist_;
  WeakCallbackWorklist weak_callback_worklist_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_MARKER_H_
