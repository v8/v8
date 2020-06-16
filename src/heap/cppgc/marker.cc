// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/marker.h"

#include "include/cppgc/internal/process-heap.h"
#include "src/heap/cppgc/heap-object-header-inl.h"
#include "src/heap/cppgc/heap-page-inl.h"
#include "src/heap/cppgc/heap-visitor.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/marking-visitor.h"
#include "src/heap/cppgc/stats-collector.h"

#if defined(CPPGC_CAGED_HEAP)
#include "include/cppgc/internal/caged-heap-local-data.h"
#endif

namespace cppgc {
namespace internal {

namespace {

void EnterIncrementalMarkingIfNeeded(
    Marker::MarkingConfig config,
    HeapBase& heap) {  // NOLINT(runtime/references)
  if (config.marking_type == Marker::MarkingConfig::MarkingType::kIncremental ||
      config.marking_type ==
          Marker::MarkingConfig::MarkingType::kIncrementalAndConcurrent) {
    ProcessHeap::EnterIncrementalOrConcurrentMarking();
  }
#if defined(CPPGC_CAGED_HEAP)
  heap.caged_heap().local_data().is_marking_in_progress = true;
#endif
}

void ExitIncrementalMarkingIfNeeded(
    Marker::MarkingConfig config,
    HeapBase& heap) {  // NOLINT(runtime/references)
  if (config.marking_type == Marker::MarkingConfig::MarkingType::kIncremental ||
      config.marking_type ==
          Marker::MarkingConfig::MarkingType::kIncrementalAndConcurrent) {
    ProcessHeap::ExitIncrementalOrConcurrentMarking();
  }
#if defined(CPPGC_CAGED_HEAP)
  heap.caged_heap().local_data().is_marking_in_progress = false;
#endif
}

template <typename Worklist, typename Callback>
bool DrainWorklistWithDeadline(v8::base::TimeTicks deadline, Worklist* worklist,
                               Callback callback, int task_id) {
  const size_t kDeadlineCheckInterval = 1250;

  size_t processed_callback_count = 0;
  typename Worklist::View view(worklist, task_id);
  typename Worklist::EntryType item;
  while (view.Pop(&item)) {
    callback(item);
    if (++processed_callback_count == kDeadlineCheckInterval) {
      if (deadline <= v8::base::TimeTicks::Now()) {
        return false;
      }
      processed_callback_count = 0;
    }
  }
  return true;
}
}  // namespace

constexpr int Marker::kMutatorThreadId;

Marker::Marker(HeapBase& heap)
    : heap_(heap), marking_visitor_(CreateMutatorThreadMarkingVisitor()) {}

Marker::~Marker() {
  // The fixed point iteration may have found not-fully-constructed objects.
  // Such objects should have already been found through the stack scan though
  // and should thus already be marked.
  if (!not_fully_constructed_worklist_.IsEmpty()) {
#if DEBUG
    DCHECK_NE(MarkingConfig::StackState::kNoHeapPointers, config_.stack_state);
    NotFullyConstructedItem item;
    NotFullyConstructedWorklist::View view(&not_fully_constructed_worklist_,
                                           kMutatorThreadId);
    while (view.Pop(&item)) {
      const HeapObjectHeader& header =
          BasePage::FromPayload(item)->ObjectHeaderFromInnerAddress(
              static_cast<ConstAddress>(item));
      DCHECK(header.IsMarked());
    }
#else
    not_fully_constructed_worklist_.Clear();
#endif
  }
}

void Marker::StartMarking(MarkingConfig config) {
  heap().stats_collector()->NotifyMarkingStarted();

  config_ = config;
  VisitRoots();
  EnterIncrementalMarkingIfNeeded(config, heap());
}

void Marker::EnterAtomicPause(MarkingConfig config) {
  ExitIncrementalMarkingIfNeeded(config_, heap());
  config_ = config;

  // Reset LABs before trying to conservatively mark in-construction objects.
  // This is also needed in preparation for sweeping.
  heap().object_allocator().ResetLinearAllocationBuffers();
  if (config_.stack_state == MarkingConfig::StackState::kNoHeapPointers) {
    FlushNotFullyConstructedObjects();
  } else {
    MarkNotFullyConstructedObjects();
  }
}

void Marker::LeaveAtomicPause() {
  heap().stats_collector()->NotifyMarkingCompleted(
      marking_visitor_->marked_bytes());
}

void Marker::FinishMarking(MarkingConfig config) {
  EnterAtomicPause(config);
  AdvanceMarkingWithDeadline(v8::base::TimeDelta::Max());
  LeaveAtomicPause();
}

void Marker::ProcessWeakness() {
  heap().GetWeakPersistentRegion().Trace(marking_visitor_.get());

  // Call weak callbacks on objects that may now be pointing to dead objects.
  WeakCallbackItem item;
  LivenessBroker broker = LivenessBrokerFactory::Create();
  WeakCallbackWorklist::View view(&weak_callback_worklist_, kMutatorThreadId);
  while (view.Pop(&item)) {
    item.callback(broker, item.parameter);
  }
  // Weak callbacks should not add any new objects for marking.
  DCHECK(marking_worklist_.IsEmpty());
}

void Marker::VisitRoots() {
  // Reset LABs before scanning roots. LABs are cleared to allow
  // ObjectStartBitmap handling without considering LABs.
  heap().object_allocator().ResetLinearAllocationBuffers();

  heap().GetStrongPersistentRegion().Trace(marking_visitor_.get());
  if (config_.stack_state != MarkingConfig::StackState::kNoHeapPointers) {
    heap().stack()->IteratePointers(marking_visitor_.get());
  }
}

std::unique_ptr<MutatorThreadMarkingVisitor>
Marker::CreateMutatorThreadMarkingVisitor() {
  return std::make_unique<MutatorThreadMarkingVisitor>(this);
}

bool Marker::AdvanceMarkingWithDeadline(v8::base::TimeDelta duration) {
  MutatorThreadMarkingVisitor* visitor = marking_visitor_.get();
  v8::base::TimeTicks deadline = v8::base::TimeTicks::Now() + duration;

  do {
    // Convert |previously_not_fully_constructed_worklist_| to
    // |marking_worklist_|. This merely re-adds items with the proper
    // callbacks.
    if (!DrainWorklistWithDeadline(
            deadline, &previously_not_fully_constructed_worklist_,
            [visitor](NotFullyConstructedItem& item) {
              visitor->DynamicallyMarkAddress(
                  reinterpret_cast<ConstAddress>(item));
            },
            kMutatorThreadId))
      return false;

    if (!DrainWorklistWithDeadline(
            deadline, &marking_worklist_,
            [visitor](const MarkingItem& item) {
              const HeapObjectHeader& header =
                  HeapObjectHeader::FromPayload(item.base_object_payload);
              DCHECK(!MutatorThreadMarkingVisitor::IsInConstruction(header));
              item.callback(visitor, item.base_object_payload);
              visitor->AccountMarkedBytes(header);
            },
            kMutatorThreadId))
      return false;

    if (!DrainWorklistWithDeadline(
            deadline, &write_barrier_worklist_,
            [visitor](HeapObjectHeader* header) {
              DCHECK(header);
              DCHECK(!MutatorThreadMarkingVisitor::IsInConstruction(*header));
              const GCInfo& gcinfo =
                  GlobalGCInfoTable::GCInfoFromIndex(header->GetGCInfoIndex());
              gcinfo.trace(visitor, header->Payload());
              visitor->AccountMarkedBytes(*header);
            },
            kMutatorThreadId))
      return false;
  } while (!marking_worklist_.IsLocalViewEmpty(kMutatorThreadId));

  return true;
}

void Marker::FlushNotFullyConstructedObjects() {
  if (!not_fully_constructed_worklist_.IsLocalViewEmpty(kMutatorThreadId)) {
    not_fully_constructed_worklist_.FlushToGlobal(kMutatorThreadId);
    previously_not_fully_constructed_worklist_.MergeGlobalPool(
        &not_fully_constructed_worklist_);
  }
  DCHECK(not_fully_constructed_worklist_.IsLocalViewEmpty(kMutatorThreadId));
}

void Marker::MarkNotFullyConstructedObjects() {
  NotFullyConstructedItem item;
  NotFullyConstructedWorklist::View view(&not_fully_constructed_worklist_,
                                         kMutatorThreadId);
  while (view.Pop(&item)) {
    marking_visitor_->TraceConservativelyIfNeeded(item);
  }
}

void Marker::ClearAllWorklistsForTesting() {
  marking_worklist_.Clear();
  not_fully_constructed_worklist_.Clear();
  previously_not_fully_constructed_worklist_.Clear();
  write_barrier_worklist_.Clear();
  weak_callback_worklist_.Clear();
}

}  // namespace internal
}  // namespace cppgc
