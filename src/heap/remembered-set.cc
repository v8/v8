// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/remembered-set.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/mark-compact.h"
#include "src/heap/slot-set.h"
#include "src/heap/spaces.h"
#include "src/heap/store-buffer.h"
#include "src/macro-assembler.h"

namespace v8 {
namespace internal {

template <PointerDirection direction>
void RememberedSet<direction>::ClearInvalidTypedSlots(Heap* heap,
                                                      MemoryChunk* chunk) {
  STATIC_ASSERT(direction == OLD_TO_NEW);
  DCHECK(chunk->owner()->identity() == CODE_SPACE);
  TypedSlotSet* slots = GetTypedSlotSet(chunk);
  if (slots != nullptr) {
    slots->Iterate(
        [heap, chunk](SlotType type, Address host_addr, Address addr) {
          if (Marking::IsBlack(ObjectMarking::MarkBitFrom(host_addr))) {
            return KEEP_SLOT;
          } else {
            return REMOVE_SLOT;
          }
        },
        TypedSlotSet::KEEP_EMPTY_CHUNKS);
  }
}

template <PointerDirection direction>
bool RememberedSet<direction>::IsValidSlot(Heap* heap, MemoryChunk* chunk,
                                           Object** slot) {
  STATIC_ASSERT(direction == OLD_TO_NEW);
  // If the target object is not black, the source slot must be part
  // of a non-black (dead) object.
  return heap->mark_compact_collector()->IsSlotInBlackObject(
      chunk, reinterpret_cast<Address>(slot));
}

template void RememberedSet<OLD_TO_NEW>::ClearInvalidTypedSlots(
    Heap* heap, MemoryChunk* chunk);

}  // namespace internal
}  // namespace v8
