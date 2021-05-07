// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/internal/pointer-policies.h"

#include "include/cppgc/internal/caged-heap-local-data.h"
#include "include/cppgc/internal/persistent-node.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/platform.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap.h"

namespace cppgc {
namespace internal {

namespace {

// Gets the state (HeapBase) for on-heap slots.
void* TryGetStateFromSlot(void* slot) {
#ifdef CPPGC_CAGED_HEAP
  if (v8::base::Stack::GetCurrentStackPosition() <= slot &&
      slot < v8::base::Stack::GetStackStart())
    return nullptr;

  // `slot` may reside in a regular or large object. Get to the heap using the
  // cage.
  return reinterpret_cast<CagedHeapLocalData*>(
             reinterpret_cast<uintptr_t>(slot) &
             ~(api_constants::kCagedHeapReservationAlignment - 1))
      ->heap_base;
#else   // !CPPGC_CAGED_HEAP
  return nullptr;
#endif  // !CPPGC_CAGED_HEAP
}

}  // namespace

// We know that Member is only allowed on heap and on-stack in rare cases. Use
// this information to eagerly populate a verification state already on policy
// creation.
EnabledMemberCheckingPolicy::EnabledMemberCheckingPolicy()
    : EnabledCheckingPolicyBase(TryGetStateFromSlot(this)) {}

void EnabledCheckingPolicyBase::CheckPointerImpl(const void* ptr,
                                                 bool points_to_payload) {
  auto* base_page = BasePage::FromPayload(ptr);
  // Large objects do not support mixins. This also means that `base_page` is
  // valid for large objects.
  DCHECK_IMPLIES(base_page->is_large(), points_to_payload);

  if (!state_) {
    state_ = base_page->heap();
    // Member references are used from within objects that cannot change their
    // heap association which means that state is immutable once it is set.
    //
    // TODO(chromium:1056170): Binding state late allows for getting the initial
    // state wrong which requires a check that `this` is contained in heap that
    // is itself expensive. Investigate options on non-caged builds to improve
    // coverage.
  }

  HeapBase* heap = static_cast<HeapBase*>(state_);
  if (!heap) return;

  // Member references should never mix heaps.
  DCHECK_EQ(heap, base_page->heap());

  // Header checks.
  const HeapObjectHeader* header = nullptr;
  if (points_to_payload) {
    header = &HeapObjectHeader::FromPayload(ptr);
  } else if (!heap->sweeper().IsSweepingInProgress()) {
    // Mixin case.
    header = &base_page->ObjectHeaderFromInnerAddress(ptr);
    DCHECK_LE(header->Payload(), ptr);
    DCHECK_GT(header->PayloadEnd(), ptr);
  }
  if (header) {
    DCHECK(!header->IsFree());
  }

  // TODO(v8:11749): Check mark bits when during pre-finalizer phase.
}

PersistentRegion& StrongPersistentPolicy::GetPersistentRegion(
    const void* object) {
  auto* heap = BasePage::FromPayload(object)->heap();
  return heap->GetStrongPersistentRegion();
}

PersistentRegion& WeakPersistentPolicy::GetPersistentRegion(
    const void* object) {
  auto* heap = BasePage::FromPayload(object)->heap();
  return heap->GetWeakPersistentRegion();
}

CrossThreadPersistentRegion&
StrongCrossThreadPersistentPolicy::GetPersistentRegion(const void* object) {
  auto* heap = BasePage::FromPayload(object)->heap();
  return heap->GetStrongCrossThreadPersistentRegion();
}

CrossThreadPersistentRegion&
WeakCrossThreadPersistentPolicy::GetPersistentRegion(const void* object) {
  auto* heap = BasePage::FromPayload(object)->heap();
  return heap->GetWeakCrossThreadPersistentRegion();
}

}  // namespace internal
}  // namespace cppgc
