// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/prefinalizer-handler.h"

#include <algorithm>
#include <memory>

#include "src/base/platform/platform.h"
#include "src/base/pointer-with-payload.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/liveness-broker.h"
#include "src/heap/cppgc/stats-collector.h"

namespace cppgc {
namespace internal {

PrefinalizerRegistration::PrefinalizerRegistration(
    void* object, const void* base_object_payload, Callback callback) {
  auto* page = BasePage::FromPayload(object);
  DCHECK(!page->space().is_compactable());
  page->heap().prefinalizer_handler()->RegisterPrefinalizer(
      PreFinalizer(object, base_object_payload, callback));
}

bool PreFinalizer::operator==(const PreFinalizer& other) const {
  return
#if defined(CPPGC_CAGED_HEAP)
      (object_offset == other.object_offset)
#else   // !defined(CPPGC_CAGED_HEAP)
      (object_and_offset == other.object_and_offset)
#endif  // !defined(CPPGC_CAGED_HEAP)
      && (callback == other.callback);
}

PreFinalizer::PreFinalizer(void* object, const void* base_object_payload,
                           Callback cb)
    :
#if defined(CPPGC_CAGED_HEAP)
      object_offset(CagedHeap::OffsetFromAddress<uint32_t>(object)),
      base_object_payload_offset(
          CagedHeap::OffsetFromAddress<uint32_t>(base_object_payload)),
#else   // !defined(CPPGC_CAGED_HEAP)
      object_and_offset(object, (object == base_object_payload)
                                    ? PointerType::kAtBase
                                    : PointerType::kInnerPointer),
#endif  // !defined(CPPGC_CAGED_HEAP)
      callback(cb) {
}

PreFinalizerHandler::PreFinalizerHandler(HeapBase& heap)
    : current_ordered_pre_finalizers_(&ordered_pre_finalizers_),
      heap_(heap)
#ifdef DEBUG
      ,
      creation_thread_id_(v8::base::OS::GetCurrentThreadId())
#endif  // DEBUG
{
}

void PreFinalizerHandler::RegisterPrefinalizer(PreFinalizer pre_finalizer) {
  DCHECK(CurrentThreadIsCreationThread());
  DCHECK_EQ(ordered_pre_finalizers_.end(),
            std::find(ordered_pre_finalizers_.begin(),
                      ordered_pre_finalizers_.end(), pre_finalizer));
  DCHECK_EQ(current_ordered_pre_finalizers_->end(),
            std::find(current_ordered_pre_finalizers_->begin(),
                      current_ordered_pre_finalizers_->end(), pre_finalizer));
  current_ordered_pre_finalizers_->push_back(pre_finalizer);
}

namespace {

// Returns true in case the prefinalizer was invoked.
V8_INLINE bool InvokeUnmarkedPrefinalizers(void* cage_base,
                                           const PreFinalizer& pf) {
#if defined(CPPGC_CAGED_HEAP)
  void* object = CagedHeap::AddressFromOffset(cage_base, pf.object_offset);
  void* base_object_payload =
      CagedHeap::AddressFromOffset(cage_base, pf.base_object_payload_offset);
#else   // !defined(CPPGC_CAGED_HEAP)
  void* object = pf.object_and_offset.GetPointer();
  void* base_object_payload =
      pf.object_and_offset.GetPayload() == PreFinalizer::PointerType::kAtBase
          ? object
          : reinterpret_cast<void*>(BasePage::FromPayload(object)
                                        ->ObjectHeaderFromInnerAddress(object)
                                        .ObjectStart());
#endif  // !defined(CPPGC_CAGED_HEAP)

  if (HeapObjectHeader::FromObject(base_object_payload).IsMarked())
    return false;
  pf.callback(object);
  return true;
}

}  // namespace

void PreFinalizerHandler::InvokePreFinalizers() {
  StatsCollector::EnabledScope stats_scope(heap_.stats_collector(),
                                           StatsCollector::kAtomicSweep);
  StatsCollector::EnabledScope nested_stats_scope(
      heap_.stats_collector(), StatsCollector::kSweepInvokePreFinalizers);

  DCHECK(CurrentThreadIsCreationThread());
  is_invoking_ = true;
  DCHECK_EQ(0u, bytes_allocated_in_prefinalizers);
  // Reset all LABs to force allocations to the slow path for black allocation.
  heap_.object_allocator().ResetLinearAllocationBuffers();

  void* cage_base = nullptr;
#if defined(CPPGC_CAGED_HEAP)
  cage_base = heap_.caged_heap().base();
#endif  // defined(CPPGC_CAGED_HEAP)
  // Prefinalizers can allocate other objects with prefinalizers, which will
  // modify ordered_pre_finalizers_ and break iterators.
  std::vector<PreFinalizer> new_ordered_pre_finalizers;
  current_ordered_pre_finalizers_ = &new_ordered_pre_finalizers;
  ordered_pre_finalizers_.erase(
      ordered_pre_finalizers_.begin(),
      std::remove_if(ordered_pre_finalizers_.rbegin(),
                     ordered_pre_finalizers_.rend(),
                     [cage_base](const PreFinalizer& pf) {
                       return InvokeUnmarkedPrefinalizers(cage_base, pf);
                     })
          .base());
  // Newly added objects with prefinalizers will always survive the current GC
  // cycle, so it's safe to add them after clearing out the older prefinalizers.
  ordered_pre_finalizers_.insert(ordered_pre_finalizers_.end(),
                                 new_ordered_pre_finalizers.begin(),
                                 new_ordered_pre_finalizers.end());
  current_ordered_pre_finalizers_ = &ordered_pre_finalizers_;
  is_invoking_ = false;
  ordered_pre_finalizers_.shrink_to_fit();
}

bool PreFinalizerHandler::CurrentThreadIsCreationThread() {
#ifdef DEBUG
  return creation_thread_id_ == v8::base::OS::GetCurrentThreadId();
#else
  return true;
#endif
}

void PreFinalizerHandler::NotifyAllocationInPrefinalizer(size_t size) {
  DCHECK_GT(bytes_allocated_in_prefinalizers + size,
            bytes_allocated_in_prefinalizers);
  bytes_allocated_in_prefinalizers += size;
}

}  // namespace internal
}  // namespace cppgc
