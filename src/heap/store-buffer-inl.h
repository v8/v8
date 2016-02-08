// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_STORE_BUFFER_INL_H_
#define V8_STORE_BUFFER_INL_H_

#include "src/heap/heap.h"
#include "src/heap/spaces-inl.h"
#include "src/heap/store-buffer.h"

namespace v8 {
namespace internal {

uint32_t StoreBuffer::AddressToSlotSetAndOffset(Address addr, SlotSet** slots) {
  MemoryChunk* chunk = MemoryChunk::FromAddress(addr);
  uintptr_t offset = addr - chunk->address();
  if (offset < MemoryChunk::kHeaderSize || chunk->owner() == nullptr) {
    chunk = heap_->lo_space()->FindPage(addr);
    offset = addr - chunk->address();
  }
  if (chunk->old_to_new_slots() == nullptr) {
    chunk->AllocateOldToNewSlots();
  }
  if (offset < Page::kPageSize) {
    *slots = chunk->old_to_new_slots();
  } else {
    *slots = &chunk->old_to_new_slots()[offset / Page::kPageSize];
    offset = offset % Page::kPageSize;
  }
  return static_cast<uint32_t>(offset);
}


void LocalStoreBuffer::Record(Address addr) {
  if (top_->is_full()) top_ = new Node(top_);
  top_->buffer[top_->count++] = addr;
}

void LocalStoreBuffer::Process(StoreBuffer* store_buffer) {
  Node* current = top_;
  while (current != nullptr) {
    for (int i = 0; i < current->count; i++) {
      store_buffer->Mark(current->buffer[i]);
    }
    current = current->next;
  }
}

void StoreBuffer::Mark(Address addr) {
  SlotSet* slots;
  uint32_t offset;
  offset = AddressToSlotSetAndOffset(addr, &slots);
  slots->Insert(offset);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_STORE_BUFFER_INL_H_
