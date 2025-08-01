// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_PAGE_METADATA_INL_H_
#define V8_HEAP_PAGE_METADATA_INL_H_

#include "src/heap/page-metadata.h"
// Include the non-inl header before the rest of the headers.

#include "src/heap/memory-chunk-inl.h"
#include "src/heap/paged-spaces.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

// static
PageMetadata* PageMetadata::FromAddress(Address addr) {
  return reinterpret_cast<PageMetadata*>(
      MemoryChunk::FromAddress(addr)->Metadata());
}

// static
PageMetadata* PageMetadata::FromAddress(const Isolate* isolate, Address addr) {
  return reinterpret_cast<PageMetadata*>(
      MemoryChunk::FromAddress(addr)->Metadata(isolate));
}

// static
PageMetadata* PageMetadata::FromHeapObject(Tagged<HeapObject> o) {
  return FromAddress(o.ptr());
}

// static
PageMetadata* PageMetadata::FromAllocationAreaAddress(Address address) {
  return PageMetadata::FromAddress(address - kTaggedSize);
}

template <typename Callback>
void PageMetadata::ForAllFreeListCategories(Callback callback) {
  for (int i = kFirstCategory; i < owner()->free_list()->number_of_categories();
       i++) {
    callback(categories_[i]);
  }
}

void PageMetadata::MarkEvacuationCandidate() {
  DCHECK(!never_evacuate());
  DCHECK_NULL(slot_set<OLD_TO_OLD>());
  DCHECK_NULL(typed_slot_set<OLD_TO_OLD>());
  set_is_evacuation_candidate(true);
  SetFlagMaybeExecutable(MemoryChunk::EVACUATION_CANDIDATE);
  reinterpret_cast<PagedSpace*>(owner())->free_list()->EvictFreeListItems(this);
}

void PageMetadata::ClearEvacuationCandidate() {
  MemoryChunk* chunk = Chunk();
  if (!chunk->IsFlagSet(MemoryChunk::COMPACTION_WAS_ABORTED)) {
    DCHECK_NULL(slot_set<OLD_TO_OLD>());
    DCHECK_NULL(typed_slot_set<OLD_TO_OLD>());
  }
  ClearFlagMaybeExecutable(MemoryChunk::EVACUATION_CANDIDATE);
  set_is_evacuation_candidate(false);
  InitializeFreeListCategories();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_PAGE_METADATA_INL_H_
