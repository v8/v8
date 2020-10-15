// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_MARKING_WORKLISTS_H_
#define V8_HEAP_CPPGC_MARKING_WORKLISTS_H_

#include <unordered_set>

#include "include/cppgc/visitor.h"
#include "src/base/platform/mutex.h"
#include "src/heap/base/worklist.h"

namespace cppgc {
namespace internal {

class HeapObjectHeader;

class MarkingWorklists {
 public:
  static constexpr int kMutatorThreadId = 0;

  using MarkingItem = cppgc::TraceDescriptor;

  struct WeakCallbackItem {
    cppgc::WeakCallback callback;
    const void* parameter;
  };

  struct ConcurrentMarkingBailoutItem {
    const void* parameter;
    TraceCallback callback;
    size_t bailedout_size;
  };

  struct EphemeronPairItem {
    const void* key;
    TraceDescriptor value_desc;
  };

  // Segment size of 512 entries necessary to avoid throughput regressions.
  // Since the work list is currently a temporary object this is not a problem.
  using MarkingWorklist =
      heap::base::Worklist<MarkingItem, 512 /* local entries */>;
  using PreviouslyNotFullyConstructedWorklist =
      heap::base::Worklist<HeapObjectHeader*, 16 /* local entries */>;
  using WeakCallbackWorklist =
      heap::base::Worklist<WeakCallbackItem, 64 /* local entries */>;
  using WriteBarrierWorklist =
      heap::base::Worklist<HeapObjectHeader*, 64 /*local entries */>;
  using ConcurrentMarkingBailoutWorklist =
      heap::base::Worklist<ConcurrentMarkingBailoutItem,
                           64 /* local entries */>;
  using EphemeronPairsWorklist =
      heap::base::Worklist<EphemeronPairItem, 64 /* local entries */>;

  class V8_EXPORT_PRIVATE NotFullyConstructedWorklist {
   public:
    void Push(HeapObjectHeader*);
    std::unordered_set<HeapObjectHeader*> Extract();
    void Clear();
    bool IsEmpty();

    ~NotFullyConstructedWorklist();

    bool ContainsForTesting(HeapObjectHeader*);

   private:
    void* operator new(size_t) = delete;
    void* operator new[](size_t) = delete;
    void operator delete(void*) = delete;
    void operator delete[](void*) = delete;

    v8::base::Mutex lock_;
    std::unordered_set<HeapObjectHeader*> objects_;
  };

  MarkingWorklist* marking_worklist() { return &marking_worklist_; }
  NotFullyConstructedWorklist* not_fully_constructed_worklist() {
    return &not_fully_constructed_worklist_;
  }
  PreviouslyNotFullyConstructedWorklist*
  previously_not_fully_constructed_worklist() {
    return &previously_not_fully_constructed_worklist_;
  }
  WriteBarrierWorklist* write_barrier_worklist() {
    return &write_barrier_worklist_;
  }
  WeakCallbackWorklist* weak_callback_worklist() {
    return &weak_callback_worklist_;
  }
  ConcurrentMarkingBailoutWorklist* concurrent_marking_bailout_worklist() {
    return &concurrent_marking_bailout_worklist_;
  }
  EphemeronPairsWorklist* discovered_ephemeron_pairs_worklist() {
    return &discovered_ephemeron_pairs_worklist_;
  }
  EphemeronPairsWorklist* ephemeron_pairs_for_processing_worklist() {
    return &ephemeron_pairs_for_processing_worklist_;
  }

  void ClearForTesting();

 private:
  MarkingWorklist marking_worklist_;
  NotFullyConstructedWorklist not_fully_constructed_worklist_;
  PreviouslyNotFullyConstructedWorklist
      previously_not_fully_constructed_worklist_;
  WriteBarrierWorklist write_barrier_worklist_;
  WeakCallbackWorklist weak_callback_worklist_;
  ConcurrentMarkingBailoutWorklist concurrent_marking_bailout_worklist_;
  EphemeronPairsWorklist discovered_ephemeron_pairs_worklist_;
  EphemeronPairsWorklist ephemeron_pairs_for_processing_worklist_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_MARKING_WORKLISTS_H_
