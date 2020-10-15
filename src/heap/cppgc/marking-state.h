// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_MARKING_STATE_H_
#define V8_HEAP_CPPGC_MARKING_STATE_H_

#include "include/cppgc/trace-trait.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/liveness-broker.h"
#include "src/heap/cppgc/marking-worklists.h"

namespace cppgc {
namespace internal {

// C++ marking implementation.
class MarkingStateBase {
 public:
  inline MarkingStateBase(HeapBase& heap, MarkingWorklists&);

  MarkingStateBase(const MarkingStateBase&) = delete;
  MarkingStateBase& operator=(const MarkingStateBase&) = delete;

  inline void MarkAndPush(const void*, TraceDescriptor);
  inline void MarkAndPush(HeapObjectHeader&);

  inline void RegisterWeakReferenceIfNeeded(const void*, TraceDescriptor,
                                            WeakCallback, const void*);
  inline void RegisterWeakCallback(WeakCallback, const void*);

  inline void ProcessEphemeron(const void*, TraceDescriptor);

  inline void AccountMarkedBytes(const HeapObjectHeader&);
  inline void AccountMarkedBytes(size_t);
  size_t marked_bytes() const { return marked_bytes_; }

  void Publish() {
    marking_worklist_.Publish();
    previously_not_fully_constructed_worklist_.Publish();
    weak_callback_worklist_.Publish();
    write_barrier_worklist_.Publish();
    concurrent_marking_bailout_worklist_.Publish();
    discovered_ephemeron_pairs_worklist_.Publish();
    ephemeron_pairs_for_processing_worklist_.Publish();
  }

  MarkingWorklists::MarkingWorklist::Local& marking_worklist() {
    return marking_worklist_;
  }
  MarkingWorklists::NotFullyConstructedWorklist&
  not_fully_constructed_worklist() {
    return not_fully_constructed_worklist_;
  }
  MarkingWorklists::PreviouslyNotFullyConstructedWorklist::Local&
  previously_not_fully_constructed_worklist() {
    return previously_not_fully_constructed_worklist_;
  }
  MarkingWorklists::WeakCallbackWorklist::Local& weak_callback_worklist() {
    return weak_callback_worklist_;
  }
  MarkingWorklists::WriteBarrierWorklist::Local& write_barrier_worklist() {
    return write_barrier_worklist_;
  }
  MarkingWorklists::ConcurrentMarkingBailoutWorklist::Local&
  concurrent_marking_bailout_worklist() {
    return concurrent_marking_bailout_worklist_;
  }
  MarkingWorklists::EphemeronPairsWorklist::Local&
  discovered_ephemeron_pairs_worklist() {
    return discovered_ephemeron_pairs_worklist_;
  }
  MarkingWorklists::EphemeronPairsWorklist::Local&
  ephemeron_pairs_for_processing_worklist() {
    return ephemeron_pairs_for_processing_worklist_;
  }

 protected:
  inline void MarkAndPush(HeapObjectHeader&, TraceDescriptor);

  inline bool MarkNoPush(HeapObjectHeader&);

#ifdef DEBUG
  HeapBase& heap_;
#endif  // DEBUG

  MarkingWorklists::MarkingWorklist::Local marking_worklist_;
  MarkingWorklists::NotFullyConstructedWorklist&
      not_fully_constructed_worklist_;
  MarkingWorklists::PreviouslyNotFullyConstructedWorklist::Local
      previously_not_fully_constructed_worklist_;
  MarkingWorklists::WeakCallbackWorklist::Local weak_callback_worklist_;
  MarkingWorklists::WriteBarrierWorklist::Local write_barrier_worklist_;
  MarkingWorklists::ConcurrentMarkingBailoutWorklist::Local
      concurrent_marking_bailout_worklist_;
  MarkingWorklists::EphemeronPairsWorklist::Local
      discovered_ephemeron_pairs_worklist_;
  MarkingWorklists::EphemeronPairsWorklist::Local
      ephemeron_pairs_for_processing_worklist_;

  size_t marked_bytes_ = 0;
};

MarkingStateBase::MarkingStateBase(HeapBase& heap,
                                   MarkingWorklists& marking_worklists)
    :
#ifdef DEBUG
      heap_(heap),
#endif  // DEBUG
      marking_worklist_(marking_worklists.marking_worklist()),
      not_fully_constructed_worklist_(
          *marking_worklists.not_fully_constructed_worklist()),
      previously_not_fully_constructed_worklist_(
          marking_worklists.previously_not_fully_constructed_worklist()),
      weak_callback_worklist_(marking_worklists.weak_callback_worklist()),
      write_barrier_worklist_(marking_worklists.write_barrier_worklist()),
      concurrent_marking_bailout_worklist_(
          marking_worklists.concurrent_marking_bailout_worklist()),
      discovered_ephemeron_pairs_worklist_(
          marking_worklists.discovered_ephemeron_pairs_worklist()),
      ephemeron_pairs_for_processing_worklist_(
          marking_worklists.ephemeron_pairs_for_processing_worklist()) {
}

void MarkingStateBase::MarkAndPush(const void* object, TraceDescriptor desc) {
  DCHECK_NOT_NULL(object);
  MarkAndPush(HeapObjectHeader::FromPayload(
                  const_cast<void*>(desc.base_object_payload)),
              desc);
}

void MarkingStateBase::MarkAndPush(HeapObjectHeader& header,
                                   TraceDescriptor desc) {
  DCHECK_NOT_NULL(desc.callback);

  if (header.IsInConstruction<HeapObjectHeader::AccessMode::kAtomic>()) {
    not_fully_constructed_worklist_.Push(&header);
  } else if (MarkNoPush(header)) {
    marking_worklist_.Push(desc);
  }
}

bool MarkingStateBase::MarkNoPush(HeapObjectHeader& header) {
  // A GC should only mark the objects that belong in its heap.
  DCHECK_EQ(&heap_, BasePage::FromPayload(&header)->heap());
  // Never mark free space objects. This would e.g. hint to marking a promptly
  // freed backing store.
  DCHECK(!header.IsFree<HeapObjectHeader::AccessMode::kAtomic>());
  return header.TryMarkAtomic();
}

void MarkingStateBase::MarkAndPush(HeapObjectHeader& header) {
  MarkAndPush(
      header,
      {header.Payload(),
       GlobalGCInfoTable::GCInfoFromIndex(header.GetGCInfoIndex()).trace});
}

void MarkingStateBase::RegisterWeakReferenceIfNeeded(const void* object,
                                                     TraceDescriptor desc,
                                                     WeakCallback weak_callback,
                                                     const void* parameter) {
  // Filter out already marked values. The write barrier for WeakMember
  // ensures that any newly set value after this point is kept alive and does
  // not require the callback.
  if (HeapObjectHeader::FromPayload(desc.base_object_payload)
          .IsMarked<HeapObjectHeader::AccessMode::kAtomic>())
    return;
  RegisterWeakCallback(weak_callback, parameter);
}

void MarkingStateBase::RegisterWeakCallback(WeakCallback callback,
                                            const void* object) {
  weak_callback_worklist_.Push({callback, object});
}

void MarkingStateBase::ProcessEphemeron(const void* key,
                                        TraceDescriptor value_desc) {
  // Filter out already marked keys. The write barrier for WeakMember
  // ensures that any newly set value after this point is kept alive and does
  // not require the callback.
  if (HeapObjectHeader::FromPayload(key)
          .IsMarked<HeapObjectHeader::AccessMode::kAtomic>()) {
    MarkAndPush(value_desc.base_object_payload, value_desc);
    return;
  }
  discovered_ephemeron_pairs_worklist_.Push({key, value_desc});
}

void MarkingStateBase::AccountMarkedBytes(const HeapObjectHeader& header) {
  AccountMarkedBytes(
      header.IsLargeObject<HeapObjectHeader::AccessMode::kAtomic>()
          ? reinterpret_cast<const LargePage*>(BasePage::FromPayload(&header))
                ->PayloadSize()
          : header.GetSize<HeapObjectHeader::AccessMode::kAtomic>());
}

void MarkingStateBase::AccountMarkedBytes(size_t marked_bytes) {
  marked_bytes_ += marked_bytes;
}

class MutatorMarkingState : public MarkingStateBase {
 public:
  MutatorMarkingState(HeapBase& heap, MarkingWorklists& marking_worklists)
      : MarkingStateBase(heap, marking_worklists) {}

  inline bool MarkNoPush(HeapObjectHeader& header) {
    return MutatorMarkingState::MarkingStateBase::MarkNoPush(header);
  }

  inline void DynamicallyMarkAddress(ConstAddress);

  // Moves objects in not_fully_constructed_worklist_ to
  // previously_not_full_constructed_worklists_.
  void FlushNotFullyConstructedObjects();

  // Moves ephemeron pairs in discovered_ephemeron_pairs_worklist_ to
  // ephemeron_pairs_for_processing_worklist_.
  void FlushDiscoveredEphemeronPairs();

  inline void InvokeWeakRootsCallbackIfNeeded(const void*, TraceDescriptor,
                                              WeakCallback, const void*);
};

void MutatorMarkingState::DynamicallyMarkAddress(ConstAddress address) {
  HeapObjectHeader& header =
      BasePage::FromPayload(address)->ObjectHeaderFromInnerAddress(
          const_cast<Address>(address));
  DCHECK(!header.IsInConstruction());
  if (MarkNoPush(header)) {
    marking_worklist_.Push(
        {reinterpret_cast<void*>(header.Payload()),
         GlobalGCInfoTable::GCInfoFromIndex(header.GetGCInfoIndex()).trace});
  }
}

void MutatorMarkingState::InvokeWeakRootsCallbackIfNeeded(
    const void* object, TraceDescriptor desc, WeakCallback weak_callback,
    const void* parameter) {
  // Since weak roots are only traced at the end of marking, we can execute
  // the callback instead of registering it.
#if DEBUG
  const HeapObjectHeader& header =
      HeapObjectHeader::FromPayload(desc.base_object_payload);
  DCHECK_IMPLIES(header.IsInConstruction(), header.IsMarked());
#endif  // DEBUG
  weak_callback(LivenessBrokerFactory::Create(), parameter);
}

class ConcurrentMarkingState : public MarkingStateBase {
 public:
  ConcurrentMarkingState(HeapBase& heap, MarkingWorklists& marking_worklists)
      : MarkingStateBase(heap, marking_worklists) {}

  ~ConcurrentMarkingState() { DCHECK_EQ(last_marked_bytes_, marked_bytes_); }

  size_t RecentlyMarkedBytes() {
    return marked_bytes_ - std::exchange(last_marked_bytes_, marked_bytes_);
  }

  inline void AccountDeferredMarkedBytes(size_t deferred_bytes) {
    // AccountDeferredMarkedBytes is called from Trace methods, which are always
    // called after AccountMarkedBytes, so there should be no underflow here.
    DCHECK_LE(deferred_bytes, marked_bytes_);
    marked_bytes_ -= deferred_bytes;
  }

 private:
  size_t last_marked_bytes_ = 0;
};

template <size_t deadline_check_interval, typename WorklistLocal,
          typename Callback, typename Predicate>
bool DrainWorklistWithPredicate(Predicate should_yield,
                                WorklistLocal& worklist_local,
                                Callback callback) {
  if (worklist_local.IsLocalAndGlobalEmpty()) return true;
  // For concurrent markers, should_yield also reports marked bytes.
  if (should_yield()) return false;
  size_t processed_callback_count = deadline_check_interval;
  typename WorklistLocal::ItemType item;
  while (worklist_local.Pop(&item)) {
    callback(item);
    if (--processed_callback_count == 0) {
      if (should_yield()) {
        return false;
      }
      processed_callback_count = deadline_check_interval;
    }
  }
  return true;
}

template <HeapObjectHeader::AccessMode mode>
void DynamicallyTraceMarkedObject(Visitor& visitor,
                                  const HeapObjectHeader& header) {
  DCHECK(!header.IsInConstruction<mode>());
  DCHECK(header.IsMarked<mode>());
  const GCInfo& gcinfo =
      GlobalGCInfoTable::GCInfoFromIndex(header.GetGCInfoIndex<mode>());
  gcinfo.trace(&visitor, header.Payload());
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_MARKING_STATE_H_
