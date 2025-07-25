// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_PRETENURING_HANDLER_INL_H_
#define V8_HEAP_PRETENURING_HANDLER_INL_H_

#include "src/heap/pretenuring-handler.h"
// Include the non-inl header before the rest of the headers.

#include "src/base/sanitizer/msan.h"
#include "src/heap/heap-layout-inl.h"
#include "src/heap/new-spaces.h"
#include "src/heap/page-metadata.h"
#include "src/heap/spaces.h"
#include "src/objects/allocation-site-inl.h"
#include "src/objects/allocation-site.h"

namespace v8::internal {

// static
void PretenuringHandler::UpdateAllocationSite(
    Heap* heap, Tagged<Map> map, Tagged<HeapObject> object, int object_size,
    PretenuringFeedbackMap* pretenuring_feedback) {
  DCHECK_NE(pretenuring_feedback,
            &heap->pretenuring_handler()->global_pretenuring_feedback_);
#ifdef DEBUG
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
  // MemoryChunk::IsToPage() is not available with sticky mark-bits.
  DCHECK_IMPLIES(v8_flags.sticky_mark_bits || chunk->IsToPage(),
                 v8_flags.minor_ms);
  DCHECK_IMPLIES(!v8_flags.minor_ms && !HeapLayout::InYoungGeneration(object),
                 chunk->Metadata(heap->isolate())->will_be_promoted());
#endif
  if (V8_UNLIKELY(!v8_flags.allocation_site_pretenuring) ||
      !AllocationSite::CanTrack(map->instance_type())) {
    return;
  }
  Tagged<AllocationMemento> memento_candidate =
      FindAllocationMemento<kForGC>(heap, map, object, object_size);
  if (memento_candidate.is_null()) {
    return;
  }
  DCHECK(IsJSObjectMap(map));

  // Entering cached feedback is used in the parallel case. We are not allowed
  // to dereference the allocation site and rather have to postpone all checks
  // till actually merging the data.
  Address key = memento_candidate->GetAllocationSiteUnchecked();
  (*pretenuring_feedback)[UncheckedCast<AllocationSite>(Tagged<Object>(key))]++;
}

// static
void PretenuringHandler::UpdateAllocationSite(
    Heap* heap, Tagged<Map> map, Tagged<HeapObject> object,
    SafeHeapObjectSize object_size,
    PretenuringFeedbackMap* pretenuring_feedback) {
  // TODO(425150995): We should have uint versions for allocation to avoid
  // introducing OOBs via sign-extended ints along the way.
  UpdateAllocationSite(heap, map, object, object_size.value(),
                       pretenuring_feedback);
}

// static
template <PretenuringHandler::FindMementoMode mode>
Tagged<AllocationMemento> PretenuringHandler::FindAllocationMemento(
    Heap* heap, Tagged<Map> map, Tagged<HeapObject> object) {
  return FindAllocationMemento<mode>(heap, map, object,
                                     object->SizeFromMap(map));
}

// static
template <PretenuringHandler::FindMementoMode mode>
Tagged<AllocationMemento> PretenuringHandler::FindAllocationMemento(
    Heap* heap, Tagged<Map> map, Tagged<HeapObject> object, int object_size) {
  // For uses from within the GC, the size here may actually change when e.g.
  // updating mementos during marking in the young generation collector. This is
  // not an issue with Scavenger that stops the mutator.
  DCHECK_IMPLIES(mode != FindMementoMode::kForGC || !v8_flags.minor_ms,
                 object_size == object->SizeFromMap(map));
  // For configurations where object size changes, we can check that it only
  // shinks in case the sizes are not matching.
  DCHECK_IMPLIES(mode == FindMementoMode::kForGC && v8_flags.minor_ms,
                 object_size >= object->SizeFromMap(map));
  Address object_address = object.address();
  Address memento_address =
      object_address + ALIGN_TO_ALLOCATION_ALIGNMENT(object_size);
  Address last_memento_word_address = memento_address + kTaggedSize;
  // If the memento would be on another page, bail out immediately.
  if (!PageMetadata::OnSamePage(object_address, last_memento_word_address)) {
    return {};
  }

  // If the page is being swept, treat it as if the memento was already swept
  // and bail out.
  if constexpr (mode != FindMementoMode::kForGC) {
    MemoryChunk* object_chunk = MemoryChunk::FromAddress(object_address);
    PageMetadata* object_page = PageMetadata::cast(object_chunk->Metadata());
    if (!object_page->SweepingDone()) {
      return {};
    }
  }

  Tagged<HeapObject> candidate = HeapObject::FromAddress(memento_address);
  ObjectSlot candidate_map_slot = candidate->map_slot();
  // This fast check may peek at an uninitialized word. However, the slow check
  // below (memento_address == top) ensures that this is safe. Mark the word as
  // initialized to silence MemorySanitizer warnings.
  MSAN_MEMORY_IS_INITIALIZED(candidate_map_slot.address(), kTaggedSize);
  if (!candidate_map_slot.Relaxed_ContainsMapValue(
          ReadOnlyRoots(heap).allocation_memento_map().ptr())) {
    return {};
  }

  // Bailout if memento is below the age mark. This is only possible for pinned
  // pages in the scavenger. Full GC has page promotion but clears the
  // NEW_SPACE_BELOW_AGE_MARK page flags before checking mementos.
  if (!v8_flags.minor_ms &&
      heap->semi_space_new_space()->IsAddressBelowAgeMark(object_address)) {
    return {};
  }

  Tagged<AllocationMemento> memento_candidate =
      Cast<AllocationMemento>(candidate);

  // Depending on what the memento is used for, we might need to perform
  // additional checks.
  Address top;
  switch (mode) {
    case kForGC:
      return memento_candidate;
    case kForRuntime:
      if (memento_candidate.is_null()) return {};
      // Either the object is the last object in the new space, or there is
      // another object of at least word size (the header map word) following
      // it, so suffices to compare ptr and top here.
      top = heap->NewSpaceTop();
      DCHECK(memento_address >= heap->NewSpaceLimit() ||
             memento_address +
                     ALIGN_TO_ALLOCATION_ALIGNMENT(sizeof(AllocationMemento)) <=
                 top);
      if ((memento_address != top) && memento_candidate->IsValid()) {
        return memento_candidate;
      }
      return {};
    default:
      UNREACHABLE();
  }
  UNREACHABLE();
}

}  // namespace v8::internal

#endif  // V8_HEAP_PRETENURING_HANDLER_INL_H_
