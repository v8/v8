// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARKING_VISITOR_UTILITY_INL_H_
#define V8_HEAP_MARKING_VISITOR_UTILITY_INL_H_

#include "src/heap/heap.h"
#include "src/heap/marking-worklist-inl.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

enum class ObjectVisitationMode {
  kVisitDirectly,
  kPushToWorklist,
};

enum class SlotTreatmentMode {
  kReadOnly,
  kReadWrite,
};

template <ObjectVisitationMode visitation_mode,
          SlotTreatmentMode slot_treatment_mode, typename Visitor,
          typename TSlot>
V8_INLINE bool VisitYoungObjectViaSlot(Visitor* visitor, TSlot slot) {
  typename TSlot::TObject target;
  if constexpr (Visitor::EnableConcurrentVisitation()) {
    target = slot.Relaxed_Load(visitor->cage_base());
  } else {
    target = *slot;
  }
  HeapObject heap_object;
  // Treat weak references as strong.
  if (!target.GetHeapObject(&heap_object)) {
    return false;
  }

#ifdef THREAD_SANITIZER
  if constexpr (Visitor::EnableConcurrentVisitation()) {
    BasicMemoryChunk::FromHeapObject(heap_object)->SynchronizedHeapLoad();
  }
#endif  // THREAD_SANITIZER

  if (!Heap::InYoungGeneration(heap_object)) {
    return false;
  }

  if (slot_treatment_mode == SlotTreatmentMode::kReadWrite &&
      !visitor->ShortCutStrings(reinterpret_cast<HeapObjectSlot&>(slot),
                                &heap_object)) {
    return false;
  }

  if (!visitor->TryMark(heap_object)) return true;

  // Maps won't change in the atomic pause, so the map can be read without
  // atomics.
  Map map;
  if constexpr (Visitor::EnableConcurrentVisitation()) {
    map = heap_object.map(visitor->cage_base());
  } else {
    map = Map::cast(*heap_object->map_slot());
    const VisitorId visitor_id = map->visitor_id();
    // Data-only objects don't require any body descriptor visitation at all and
    // are always visited directly.
    if (Map::ObjectFieldsFrom(visitor_id) == ObjectFields::kDataOnly) {
      const int visited_size = heap_object->SizeFromMap(map);
      visitor->IncrementLiveBytesCached(
          MemoryChunk::cast(BasicMemoryChunk::FromHeapObject(heap_object)),
          ALIGN_TO_ALLOCATION_ALIGNMENT(visited_size));
      return true;
    }
  }
  if constexpr (visitation_mode == ObjectVisitationMode::kVisitDirectly) {
    const int visited_size = visitor->Visit(map, heap_object);
    if (visited_size) {
      visitor->IncrementLiveBytesCached(
          MemoryChunk::cast(BasicMemoryChunk::FromHeapObject(heap_object)),
          ALIGN_TO_ALLOCATION_ALIGNMENT(visited_size));
    }
    return true;
  }
  // Default case: Visit via worklist.
  visitor->worklists_local()->Push(heap_object);

  return true;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARKING_VISITOR_UTILITY_INL_H_
