// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/store-buffer.h"

#include <algorithm>

#include "src/counters.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/store-buffer-inl.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

StoreBuffer::StoreBuffer(Heap* heap)
    : heap_(heap), start_(nullptr), limit_(nullptr), virtual_memory_(nullptr) {}

void StoreBuffer::SetUp() {
  // Allocate 3x the buffer size, so that we can start the new store buffer
  // aligned to 2x the size.  This lets us use a bit test to detect the end of
  // the area.
  virtual_memory_ = new base::VirtualMemory(kStoreBufferSize * 3);
  uintptr_t start_as_int =
      reinterpret_cast<uintptr_t>(virtual_memory_->address());
  start_ =
      reinterpret_cast<Address*>(RoundUp(start_as_int, kStoreBufferSize * 2));
  limit_ = start_ + (kStoreBufferSize / kPointerSize);

  DCHECK(reinterpret_cast<Address>(start_) >= virtual_memory_->address());
  DCHECK(reinterpret_cast<Address>(limit_) >= virtual_memory_->address());
  Address* vm_limit = reinterpret_cast<Address*>(
      reinterpret_cast<char*>(virtual_memory_->address()) +
      virtual_memory_->size());
  DCHECK(start_ <= vm_limit);
  DCHECK(limit_ <= vm_limit);
  USE(vm_limit);
  DCHECK((reinterpret_cast<uintptr_t>(limit_) & kStoreBufferOverflowBit) != 0);
  DCHECK((reinterpret_cast<uintptr_t>(limit_ - 1) & kStoreBufferOverflowBit) ==
         0);

  if (!virtual_memory_->Commit(reinterpret_cast<Address>(start_),
                               kStoreBufferSize,
                               false)) {  // Not executable.
    V8::FatalProcessOutOfMemory("StoreBuffer::SetUp");
  }
  heap_->set_store_buffer_top(reinterpret_cast<Smi*>(start_));
}


void StoreBuffer::TearDown() {
  delete virtual_memory_;
  start_ = limit_ = NULL;
  heap_->set_store_buffer_top(reinterpret_cast<Smi*>(start_));
}


void StoreBuffer::StoreBufferOverflow(Isolate* isolate) {
  isolate->heap()->store_buffer()->InsertEntriesFromBuffer();
  isolate->counters()->store_buffer_overflows()->Increment();
}

void StoreBuffer::Remove(Address addr) {
  InsertEntriesFromBuffer();
  MemoryChunk* chunk = MemoryChunk::FromAddress(addr);
  DCHECK_EQ(chunk->owner()->identity(), OLD_SPACE);
  uintptr_t offset = addr - chunk->address();
  DCHECK_LT(offset, static_cast<uintptr_t>(Page::kPageSize));
  if (chunk->old_to_new_slots() == nullptr) return;
  chunk->old_to_new_slots()->Remove(static_cast<uint32_t>(offset));
}

#ifdef VERIFY_HEAP
void StoreBuffer::VerifyPointers(LargeObjectSpace* space) {
  LargeObjectIterator it(space);
  for (HeapObject* object = it.Next(); object != NULL; object = it.Next()) {
    if (object->IsFixedArray()) {
      Address slot_address = object->address();
      Address end = object->address() + object->Size();
      while (slot_address < end) {
        HeapObject** slot = reinterpret_cast<HeapObject**>(slot_address);
        // When we are not in GC the Heap::InNewSpace() predicate
        // checks that pointers which satisfy predicate point into
        // the active semispace.
        Object* object = *slot;
        heap_->InNewSpace(object);
        slot_address += kPointerSize;
      }
    }
  }
}
#endif


void StoreBuffer::Verify() {
#ifdef VERIFY_HEAP
  VerifyPointers(heap_->lo_space());
#endif
}

void StoreBuffer::InsertEntriesFromBuffer() {
  Address* top = reinterpret_cast<Address*>(heap_->store_buffer_top());
  if (top == start_) return;
  // There's no check of the limit in the loop below so we check here for
  // the worst case (compaction doesn't eliminate any pointers).
  DCHECK(top <= limit_);
  heap_->set_store_buffer_top(reinterpret_cast<Smi*>(start_));
  Page* last_page = nullptr;
  SlotSet* last_slot_set = nullptr;
  for (Address* current = start_; current < top; current++) {
    DCHECK(!heap_->code_space()->Contains(*current));
    Address addr = *current;
    Page* page = Page::FromAddress(addr);
    SlotSet* slot_set;
    uint32_t offset;
    if (page == last_page) {
      slot_set = last_slot_set;
      offset = static_cast<uint32_t>(addr - page->address());
    } else {
      offset = AddressToSlotSetAndOffset(addr, &slot_set);
      last_page = page;
      last_slot_set = slot_set;
    }
    slot_set->Insert(offset);
  }
}

static SlotSet::CallbackResult ProcessOldToNewSlot(
    Heap* heap, Address slot_address, ObjectSlotCallback slot_callback) {
  Object** slot = reinterpret_cast<Object**>(slot_address);
  Object* object = *slot;
  if (heap->InFromSpace(object)) {
    HeapObject* heap_object = reinterpret_cast<HeapObject*>(object);
    DCHECK(heap_object->IsHeapObject());
    slot_callback(reinterpret_cast<HeapObject**>(slot), heap_object);
    object = *slot;
    // If the object was in from space before and is after executing the
    // callback in to space, the object is still live.
    // Unfortunately, we do not know about the slot. It could be in a
    // just freed free space object.
    if (heap->InToSpace(object)) {
      return SlotSet::KEEP_SLOT;
    }
  } else {
    DCHECK(!heap->InNewSpace(object));
  }
  return SlotSet::REMOVE_SLOT;
}

void StoreBuffer::IteratePointersToNewSpace(ObjectSlotCallback slot_callback) {
  Heap* heap = heap_;
  Iterate([heap, slot_callback](Address addr) {
    return ProcessOldToNewSlot(heap, addr, slot_callback);
  });
}

template <typename Callback>
void StoreBuffer::Iterate(Callback callback) {
  InsertEntriesFromBuffer();
  PointerChunkIterator it(heap_);
  MemoryChunk* chunk;
  while ((chunk = it.next()) != nullptr) {
    if (chunk->old_to_new_slots() != nullptr) {
      SlotSet* slots = chunk->old_to_new_slots();
      size_t pages = (chunk->size() + Page::kPageSize - 1) / Page::kPageSize;
      for (size_t page = 0; page < pages; page++) {
        slots[page].Iterate(callback);
      }
    }
  }
}


void StoreBuffer::ClearInvalidStoreBufferEntries() {
  InsertEntriesFromBuffer();

  Heap* heap = heap_;
  PageIterator it(heap->old_space());
  MemoryChunk* chunk;
  while (it.has_next()) {
    chunk = it.next();
    if (chunk->old_to_new_slots() != nullptr) {
      SlotSet* slots = chunk->old_to_new_slots();
      size_t pages = (chunk->size() + Page::kPageSize - 1) / Page::kPageSize;
      if (pages > 1) {
        // Large pages were processed above.
        continue;
      }
      slots->Iterate([heap](Address addr) {
        Object** slot = reinterpret_cast<Object**>(addr);
        Object* object = *slot;
        if (heap->InNewSpace(object)) {
          DCHECK(object->IsHeapObject());
          // If the target object is not black, the source slot must be part
          // of a non-black (dead) object.
          HeapObject* heap_object = HeapObject::cast(object);
          bool live = Marking::IsBlack(Marking::MarkBitFrom(heap_object)) &&
                      heap->mark_compact_collector()->IsSlotInLiveObject(addr);
          return live ? SlotSet::KEEP_SLOT : SlotSet::REMOVE_SLOT;
        }
        return SlotSet::REMOVE_SLOT;
      });
    }
  }
}


void StoreBuffer::VerifyValidStoreBufferEntries() {
  Heap* heap = heap_;
  Iterate([heap](Address addr) {
    Object** slot = reinterpret_cast<Object**>(addr);
    Object* object = *slot;
    if (Page::FromAddress(addr)->owner() != nullptr &&
        Page::FromAddress(addr)->owner()->identity() == OLD_SPACE) {
      CHECK(object->IsHeapObject());
      CHECK(heap->InNewSpace(object));
      heap->mark_compact_collector()->VerifyIsSlotInLiveObject(
          reinterpret_cast<Address>(slot), HeapObject::cast(object));
    }
    return SlotSet::KEEP_SLOT;
  });
}

}  // namespace internal
}  // namespace v8
