// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/mark-compact-base.h"

#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/heap/large-spaces.h"
#include "src/heap/new-spaces.h"
#include "src/objects/objects-inl.h"
#include "src/objects/string-forwarding-table-inl.h"
#include "src/objects/visitors-inl.h"

namespace v8 {
namespace internal {

// The following has to hold in order for {MarkingState::MarkBitFrom} to not
// produce invalid {kImpossibleBitPattern} in the marking bitmap by overlapping.
static_assert(Heap::kMinObjectSizeInTaggedWords >= 2);

// =============================================================================
// Verifiers
// =============================================================================

#ifdef VERIFY_HEAP
MarkingVerifier::MarkingVerifier(Heap* heap)
    : ObjectVisitorWithCageBases(heap), heap_(heap) {}

void MarkingVerifier::VisitMapPointer(HeapObject object) {
  VerifyMap(object.map(cage_base()));
}

void MarkingVerifier::VerifyRoots() {
  heap_->IterateRootsIncludingClients(
      this, base::EnumSet<SkipRoot>{SkipRoot::kWeak, SkipRoot::kTopOfStack});
}

void MarkingVerifier::VerifyMarkingOnPage(const Page* page, Address start,
                                          Address end) {
  Address next_object_must_be_here_or_later = start;

  for (auto [object, size] : LiveObjectRange(page)) {
    Address current = object.address();
    if (current < start) continue;
    if (current >= end) break;
    CHECK(IsMarked(object));
    CHECK(current >= next_object_must_be_here_or_later);
    object.Iterate(cage_base(), this);
    next_object_must_be_here_or_later = current + size;
    // The object is either part of a black area of black allocation or a
    // regular black object
    CHECK(bitmap(page)->AllBitsSetInRange(
              MarkingBitmap::AddressToIndex(current),
              MarkingBitmap::LimitAddressToIndex(
                  next_object_must_be_here_or_later)) ||
          bitmap(page)->AllBitsClearInRange(
              MarkingBitmap::AddressToIndex(current) + 1,
              MarkingBitmap::LimitAddressToIndex(
                  next_object_must_be_here_or_later)));
    current = next_object_must_be_here_or_later;
  }
}

void MarkingVerifier::VerifyMarking(NewSpace* space) {
  if (!space) return;
  if (v8_flags.minor_ms) {
    VerifyMarking(PagedNewSpace::From(space)->paged_space());
    return;
  }
  Address end = space->top();
  // The bottom position is at the start of its page. Allows us to use
  // page->area_start() as start of range on all pages.
  CHECK_EQ(space->first_allocatable_address(),
           space->first_page()->area_start());

  PageRange range(space->first_allocatable_address(), end);
  for (auto it = range.begin(); it != range.end();) {
    Page* page = *(it++);
    Address limit = it != range.end() ? page->area_end() : end;
    CHECK(limit == end || !page->Contains(end));
    VerifyMarkingOnPage(page, page->area_start(), limit);
  }
}

void MarkingVerifier::VerifyMarking(PagedSpaceBase* space) {
  for (Page* p : *space) {
    VerifyMarkingOnPage(p, p->area_start(), p->area_end());
  }
}

void MarkingVerifier::VerifyMarking(LargeObjectSpace* lo_space) {
  if (!lo_space) return;
  LargeObjectSpaceObjectIterator it(lo_space);
  for (HeapObject obj = it.Next(); !obj.is_null(); obj = it.Next()) {
    if (IsMarked(obj)) {
      obj.Iterate(cage_base(), this);
    }
  }
}
#endif  // VERIFY_HEAP

// =============================================================================
// MarkCompactCollectorBase
// =============================================================================

StringForwardingTableCleanerBase::StringForwardingTableCleanerBase(Heap* heap)
    : isolate_(heap->isolate()),
      marking_state_(heap->non_atomic_marking_state()) {}

void StringForwardingTableCleanerBase::DisposeExternalResource(
    StringForwardingTable::Record* record) {
  Address resource = record->ExternalResourceAddress();
  if (resource != kNullAddress && disposed_resources_.count(resource) == 0) {
    record->DisposeExternalResource();
    disposed_resources_.insert(resource);
  }
}

MarkCompactCollectorBase::MarkCompactCollectorBase(Heap* heap,
                                                   GarbageCollector collector)
    : heap_(heap),
      marking_state_(heap_->marking_state()),
      non_atomic_marking_state_(heap_->non_atomic_marking_state()),
      garbage_collector_(collector) {
  DCHECK_NE(GarbageCollector::SCAVENGER, garbage_collector_);
}

bool MarkCompactCollectorBase::IsCppHeapMarkingFinished() const {
  const auto* cpp_heap = CppHeap::From(heap_->cpp_heap());
  if (!cpp_heap) return true;

  return cpp_heap->IsTracingDone() &&
         local_marking_worklists()->IsWrapperEmpty();
}

#if DEBUG
void MarkCompactCollectorBase::VerifyRememberedSetsAfterEvacuation() {
  // Old-to-old slot sets must be empty after evacuation.
  bool new_space_is_empty =
      !heap_->new_space() || heap_->new_space()->Size() == 0;
  DCHECK_IMPLIES(garbage_collector_ == GarbageCollector::MARK_COMPACTOR,
                 new_space_is_empty);

  MemoryChunkIterator chunk_iterator(heap_);

  while (chunk_iterator.HasNext()) {
    MemoryChunk* chunk = chunk_iterator.Next();

    // Old-to-old slot sets must be empty after evacuation.
    DCHECK_NULL((chunk->slot_set<OLD_TO_OLD, AccessMode::ATOMIC>()));
    DCHECK_NULL((chunk->typed_slot_set<OLD_TO_OLD, AccessMode::ATOMIC>()));

    if (new_space_is_empty &&
        (garbage_collector_ == GarbageCollector::MARK_COMPACTOR)) {
      // Old-to-new slot sets must be empty after evacuation.
      DCHECK_NULL((chunk->slot_set<OLD_TO_NEW, AccessMode::ATOMIC>()));
      DCHECK_NULL((chunk->typed_slot_set<OLD_TO_NEW, AccessMode::ATOMIC>()));
      DCHECK_NULL(
          (chunk->slot_set<OLD_TO_NEW_BACKGROUND, AccessMode::ATOMIC>()));
      DCHECK_NULL(
          (chunk->typed_slot_set<OLD_TO_NEW_BACKGROUND, AccessMode::ATOMIC>()));
    }

    // Old-to-shared slots may survive GC but there should never be any slots in
    // new or shared spaces.
    AllocationSpace id = chunk->owner_identity();
    if (id == SHARED_SPACE || id == SHARED_LO_SPACE || id == NEW_SPACE ||
        id == NEW_LO_SPACE) {
      DCHECK_NULL((chunk->slot_set<OLD_TO_SHARED, AccessMode::ATOMIC>()));
      DCHECK_NULL((chunk->typed_slot_set<OLD_TO_SHARED, AccessMode::ATOMIC>()));
    }
  }
}
#endif  // DEBUG

}  // namespace internal
}  // namespace v8
