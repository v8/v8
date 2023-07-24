// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MINOR_MARK_SWEEP_INL_H_
#define V8_HEAP_MINOR_MARK_SWEEP_INL_H_

#include <atomic>

#include "src/base/build_config.h"
#include "src/common/globals.h"
#include "src/heap/marking-visitor-utility-inl.h"
#include "src/heap/memory-chunk.h"
#include "src/heap/minor-mark-sweep.h"
#include "src/heap/remembered-set-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/map.h"
#include "src/objects/string.h"
#include "src/roots/static-roots.h"

namespace v8 {
namespace internal {

V8_INLINE bool YoungGenerationMainMarkingVisitor::ShortCutStrings(
    HeapObjectSlot slot, HeapObject* heap_object) {
  if (shortcut_strings_) {
    DCHECK(V8_STATIC_ROOTS_BOOL);
#if V8_STATIC_ROOTS_BOOL
    ObjectSlot map_slot = heap_object->map_slot();
    Address map_address = map_slot.load_map().ptr();
    if (map_address == StaticReadOnlyRoot::kThinOneByteStringMap ||
        map_address == StaticReadOnlyRoot::kThinTwoByteStringMap) {
      DCHECK_EQ(heap_object->map(ObjectVisitorWithCageBases::cage_base())
                    ->visitor_id(),
                VisitorId::kVisitThinString);
      *heap_object = ThinString::cast(*heap_object)->actual();
      // ThinStrings always refer to internalized strings, which are always
      // in old space.
      DCHECK(!Heap::InYoungGeneration(*heap_object));
      slot.StoreHeapObject(*heap_object);
      return false;
    } else if (map_address == StaticReadOnlyRoot::kConsOneByteStringMap ||
               map_address == StaticReadOnlyRoot::kConsTwoByteStringMap) {
      // Not all ConsString are short cut candidates.
      const VisitorId visitor_id =
          heap_object->map(ObjectVisitorWithCageBases::cage_base())
              ->visitor_id();
      if (visitor_id == VisitorId::kVisitShortcutCandidate) {
        ConsString string = ConsString::cast(*heap_object);
        if (static_cast<Tagged_t>(string->second().ptr()) ==
            StaticReadOnlyRoot::kempty_string) {
          *heap_object = string->first();
          slot.StoreHeapObject(*heap_object);
          if (!Heap::InYoungGeneration(*heap_object)) {
            return false;
          }
        }
      }
    }
#endif  // V8_STATIC_ROOTS_BOOL
  }
  return true;
}

template <typename TSlot>
void YoungGenerationMainMarkingVisitor::VisitPointersImpl(HeapObject host,
                                                          TSlot start,
                                                          TSlot end) {
  for (TSlot slot = start; slot < end; ++slot) {
    VisitYoungObjectViaSlot<ObjectVisitationMode::kPushToWorklist,
                            SlotTreatmentMode::kReadWrite>(this, slot);
  }
}

template <typename TSlot>
V8_INLINE bool
YoungGenerationMainMarkingVisitor::VisitObjectViaSlotInRemeberedSet(
    TSlot slot) {
  return VisitYoungObjectViaSlot<ObjectVisitationMode::kVisitDirectly,
                                 SlotTreatmentMode::kReadWrite>(this, slot);
}

V8_INLINE void YoungGenerationMainMarkingVisitor::IncrementLiveBytesCached(
    MemoryChunk* chunk, intptr_t by) {
  DCHECK_IMPLIES(V8_COMPRESS_POINTERS_8GB_BOOL,
                 IsAligned(by, kObjectAlignment8GbHeap));
  const size_t hash =
      (reinterpret_cast<size_t>(chunk) >> kPageSizeBits) & kEntriesMask;
  auto& entry = live_bytes_data_[hash];
  if (entry.first && entry.first != chunk) {
    entry.first->IncrementLiveBytesAtomically(entry.second);
    entry.first = chunk;
    entry.second = 0;
  } else {
    entry.first = chunk;
  }
  entry.second += by;
}

void YoungGenerationRootMarkingVisitor::VisitRootPointer(
    Root root, const char* description, FullObjectSlot p) {
  VisitPointersImpl(root, p, p + 1);
}

void YoungGenerationRootMarkingVisitor::VisitRootPointers(
    Root root, const char* description, FullObjectSlot start,
    FullObjectSlot end) {
  VisitPointersImpl(root, start, end);
}

template <typename TSlot>
void YoungGenerationRootMarkingVisitor::VisitPointersImpl(Root root,
                                                          TSlot start,
                                                          TSlot end) {
  if (root == Root::kStackRoots) {
    for (TSlot slot = start; slot < end; ++slot) {
      VisitYoungObjectViaSlot<ObjectVisitationMode::kPushToWorklist,
                              SlotTreatmentMode::kReadOnly>(
          main_marking_visitor_, slot);
    }
  } else {
    for (TSlot slot = start; slot < end; ++slot) {
      VisitYoungObjectViaSlot<ObjectVisitationMode::kPushToWorklist,
                              SlotTreatmentMode::kReadWrite>(
          main_marking_visitor_, slot);
    }
  }
}

template <typename Visitor>
bool YoungGenerationRememberedSetsMarkingWorklist::ProcessNextItem(
    Visitor* visitor, base::Optional<size_t>& index) {
  if (remaining_remembered_sets_marking_items_.load(
          std::memory_order_relaxed) == 0) {
    return false;
  }
  while (true) {
    if (index && (index < remembered_sets_marking_items_.size())) {
      auto& work_item = remembered_sets_marking_items_[*index];
      if (work_item.TryAcquire()) {
        remaining_remembered_sets_marking_items_.fetch_sub(
            1, std::memory_order_relaxed);
        work_item.Process(visitor);
        (*index)++;
        return true;
      }
    }
    index = remembered_sets_marking_index_generator_.GetNext();
    if (!index) return false;
  }
}

template <typename Visitor>
void YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::Process(
    Visitor* visitor) {
  CodePageHeaderModificationScope header_modification_scope(
      "Marking modifies the remembered sets in the page header");
  if (slots_type_ == SlotsType::kRegularSlots) {
    MarkUntypedPointers(visitor);
  } else {
    MarkTypedPointers(visitor);
  }
}

void YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::
    CheckOldToNewSlotForSharedUntyped(MemoryChunk* chunk, Address slot_address,
                                      MaybeObject object) {
  HeapObject heap_object;
  if (!object.GetHeapObject(&heap_object)) return;
#ifdef THREAD_SANITIZER
  BasicMemoryChunk::FromHeapObject(heap_object)->SynchronizedHeapLoad();
#endif  // THREAD_SANITIZER
  if (heap_object.InWritableSharedSpace()) {
    RememberedSet<OLD_TO_SHARED>::Insert<AccessMode::ATOMIC>(chunk,
                                                             slot_address);
  }
}

void YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::
    CheckOldToNewSlotForSharedTyped(MemoryChunk* chunk, SlotType slot_type,
                                    Address slot_address,
                                    MaybeObject new_target) {
  HeapObject heap_object;
  if (!new_target.GetHeapObject(&heap_object)) return;
#ifdef THREAD_SANITIZER
  BasicMemoryChunk::FromHeapObject(heap_object)->SynchronizedHeapLoad();
#endif  // THREAD_SANITIZER
  if (heap_object.InWritableSharedSpace()) {
    const uintptr_t offset = slot_address - chunk->address();
    DCHECK_LT(offset, static_cast<uintptr_t>(TypedSlotSet::kMaxOffset));
    RememberedSet<OLD_TO_SHARED>::InsertTyped(chunk, slot_type,
                                              static_cast<uint32_t>(offset));
  }
}

template <typename Visitor>
void YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::
    MarkUntypedPointers(Visitor* visitor) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
               "MarkingItem::MarkUntypedPointers");
  const bool record_old_to_shared_slots =
      chunk_->heap()->isolate()->has_shared_space();
  auto callback = [this, visitor,
                   record_old_to_shared_slots](MaybeObjectSlot slot) {
    SlotCallbackResult result = CheckAndMarkObject(visitor, slot);
    if (result == REMOVE_SLOT && record_old_to_shared_slots) {
      MaybeObject object;
      if constexpr (Visitor::EnableConcurrentVisitation()) {
        object = slot.Relaxed_Load(visitor->cage_base());
      } else {
        object = *slot;
      }
      CheckOldToNewSlotForSharedUntyped(chunk_, slot.address(), object);
    }
    return result;
  };
  if (slot_set_) {
    const auto slot_count =
        RememberedSet<OLD_TO_NEW>::template Iterate<AccessMode::NON_ATOMIC>(
            slot_set_, chunk_, callback, SlotSet::FREE_EMPTY_BUCKETS);
    if (slot_count == 0) {
      SlotSet::Delete(slot_set_, chunk_->buckets());
      slot_set_ = nullptr;
    }
  }
  if (background_slot_set_) {
    const auto slot_count =
        RememberedSet<OLD_TO_NEW_BACKGROUND>::template Iterate<
            AccessMode::NON_ATOMIC>(background_slot_set_, chunk_, callback,
                                    SlotSet::FREE_EMPTY_BUCKETS);
    if (slot_count == 0) {
      SlotSet::Delete(background_slot_set_, chunk_->buckets());
      background_slot_set_ = nullptr;
    }
  }
}

template <typename Visitor>
void YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::
    MarkTypedPointers(Visitor* visitor) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
               "MarkingItem::MarkTypedPointers");
  const bool record_old_to_shared_slots =
      chunk_->heap()->isolate()->has_shared_space();
  DCHECK_NULL(background_slot_set_);
  DCHECK_NOT_NULL(typed_slot_set_);
  const auto slot_count = RememberedSet<OLD_TO_NEW>::IterateTyped(
      typed_slot_set_, [this, visitor, record_old_to_shared_slots](
                           SlotType slot_type, Address slot_address) {
        return UpdateTypedSlotHelper::UpdateTypedSlot(
            heap(), slot_type, slot_address,
            [this, visitor, record_old_to_shared_slots, slot_address,
             slot_type](FullMaybeObjectSlot slot) {
              SlotCallbackResult result = CheckAndMarkObject(visitor, slot);
              if (result == REMOVE_SLOT && record_old_to_shared_slots) {
                MaybeObject object;
                if constexpr (Visitor::EnableConcurrentVisitation()) {
                  object = slot.Relaxed_Load(visitor->cage_base());
                } else {
                  object = *slot;
                }
                CheckOldToNewSlotForSharedTyped(chunk_, slot_type, slot_address,
                                                object);
              }
              return result;
            });
      });
  if (slot_count == 0) {
    delete typed_slot_set_;
    typed_slot_set_ = nullptr;
  }
}

template <typename Visitor, typename TSlot>
V8_INLINE SlotCallbackResult
YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::CheckAndMarkObject(
    Visitor* visitor, TSlot slot) {
  static_assert(
      std::is_same<TSlot, FullMaybeObjectSlot>::value ||
          std::is_same<TSlot, MaybeObjectSlot>::value,
      "Only FullMaybeObjectSlot and MaybeObjectSlot are expected here");
  return visitor->VisitObjectViaSlotInRemeberedSet(slot) ? KEEP_SLOT
                                                         : REMOVE_SLOT;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MINOR_MARK_SWEEP_INL_H_
