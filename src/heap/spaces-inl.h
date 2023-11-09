// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SPACES_INL_H_
#define V8_HEAP_SPACES_INL_H_

#include "src/base/atomic-utils.h"
#include "src/base/v8-fallthrough.h"
#include "src/common/globals.h"
#include "src/heap/heap-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/large-page.h"
#include "src/heap/large-spaces.h"
#include "src/heap/main-allocator-inl.h"
#include "src/heap/memory-chunk-inl.h"
#include "src/heap/new-spaces.h"
#include "src/heap/paged-spaces.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

template <class PAGE_TYPE>
PageIteratorImpl<PAGE_TYPE>& PageIteratorImpl<PAGE_TYPE>::operator++() {
  p_ = p_->next_page();
  return *this;
}

template <class PAGE_TYPE>
PageIteratorImpl<PAGE_TYPE> PageIteratorImpl<PAGE_TYPE>::operator++(int) {
  PageIteratorImpl<PAGE_TYPE> tmp(*this);
  operator++();
  return tmp;
}

void Space::IncrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                               size_t amount) {
  base::CheckedIncrement(&external_backing_store_bytes_[static_cast<int>(type)],
                         amount);
  heap()->IncrementExternalBackingStoreBytes(type, amount);
}

void Space::DecrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                               size_t amount) {
  base::CheckedDecrement(&external_backing_store_bytes_[static_cast<int>(type)],
                         amount);
  heap()->DecrementExternalBackingStoreBytes(type, amount);
}

void Space::MoveExternalBackingStoreBytes(ExternalBackingStoreType type,
                                          Space* from, Space* to,
                                          size_t amount) {
  if (from == to) return;

  base::CheckedDecrement(
      &(from->external_backing_store_bytes_[static_cast<int>(type)]), amount);
  base::CheckedIncrement(
      &(to->external_backing_store_bytes_[static_cast<int>(type)]), amount);
}

PageRange::PageRange(Page* page) : PageRange(page, page->next_page()) {}
ConstPageRange::ConstPageRange(const Page* page)
    : ConstPageRange(page, page->next_page()) {}

OldGenerationMemoryChunkIterator::OldGenerationMemoryChunkIterator(Heap* heap)
    : heap_(heap),
      state_(kOldSpaceState),
      old_iterator_(heap->old_space()->begin()),
      code_iterator_(heap->code_space()->begin()),
      lo_iterator_(heap->lo_space()->begin()),
      code_lo_iterator_(heap->code_lo_space()->begin()) {}

MemoryChunk* OldGenerationMemoryChunkIterator::next() {
  switch (state_) {
    case kOldSpaceState: {
      if (old_iterator_ != heap_->old_space()->end()) return *(old_iterator_++);
      state_ = kCodeState;
      V8_FALLTHROUGH;
    }
    case kCodeState: {
      if (code_iterator_ != heap_->code_space()->end())
        return *(code_iterator_++);
      state_ = kLargeObjectState;
      V8_FALLTHROUGH;
    }
    case kLargeObjectState: {
      if (lo_iterator_ != heap_->lo_space()->end()) return *(lo_iterator_++);
      state_ = kCodeLargeObjectState;
      V8_FALLTHROUGH;
    }
    case kCodeLargeObjectState: {
      if (code_lo_iterator_ != heap_->code_lo_space()->end())
        return *(code_lo_iterator_++);
      state_ = kFinishedState;
      V8_FALLTHROUGH;
    }
    case kFinishedState:
      return nullptr;
    default:
      break;
  }
  UNREACHABLE();
}

bool MemoryChunkIterator::HasNext() {
  if (current_chunk_) return true;

  while (space_iterator_.HasNext()) {
    Space* space = space_iterator_.Next();
    current_chunk_ = space->first_page();
    if (current_chunk_) return true;
  }

  return false;
}

MemoryChunk* MemoryChunkIterator::Next() {
  MemoryChunk* chunk = current_chunk_;
  current_chunk_ = chunk->list_node().next();
  return chunk;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SPACES_INL_H_
