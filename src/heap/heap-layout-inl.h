// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_LAYOUT_INL_H_
#define V8_HEAP_HEAP_LAYOUT_INL_H_

#include "src/flags/flags.h"
#include "src/heap/heap-layout.h"
#include "src/heap/memory-chunk.h"
#include "src/objects/casting.h"
#include "src/objects/objects.h"

namespace v8::internal {

// static
bool HeapLayout::InReadOnlySpace(Tagged<HeapObject> object) {
  return MemoryChunk::FromHeapObject(object)->InReadOnlySpace();
}

// static
bool HeapLayout::InYoungGeneration(const MemoryChunk* chunk,
                                   Tagged<HeapObject> object) {
  if constexpr (v8_flags.single_generation.value()) {
    return false;
  }
  if constexpr (v8_flags.sticky_mark_bits.value()) {
    return InYoungGenerationForStickyMarkbits(chunk, object);
  }
  return chunk->InYoungGeneration();
}

// static
bool HeapLayout::InYoungGeneration(Tagged<Object> object) {
  if (object.IsSmi()) {
    return false;
  }
  return InYoungGeneration(Cast<HeapObject>(object));
}

// static
bool HeapLayout::InYoungGeneration(Tagged<HeapObject> object) {
  return InYoungGeneration(MemoryChunk::FromHeapObject(object), object);
}

// static
bool HeapLayout::InYoungGeneration(const HeapObjectLayout* object) {
  return InYoungGeneration(Tagged<HeapObject>(object));
}

// static
bool HeapLayout::InWritableSharedSpace(Tagged<HeapObject> object) {
  return MemoryChunk::FromHeapObject(object)->InWritableSharedSpace();
}

// static
bool HeapLayout::InAnySharedSpace(Tagged<HeapObject> object) {
#ifdef V8_SHARED_RO_HEAP
  if (HeapLayout::InReadOnlySpace(object)) {
    return V8_SHARED_RO_HEAP_BOOL;
  }
#endif  // V8_SHARED_RO_HEAP
  return HeapLayout::InWritableSharedSpace(object);
}

// static
bool HeapLayout::InCodeSpace(Tagged<HeapObject> object) {
  return MemoryChunk::FromHeapObject(object)->InCodeSpace();
}

// static
bool HeapLayout::InTrustedSpace(Tagged<HeapObject> object) {
  return MemoryChunk::FromHeapObject(object)->InTrustedSpace();
}

bool HeapLayout::InBlackAllocatedPage(Tagged<HeapObject> object) {
  DCHECK(v8_flags.black_allocated_pages);
  return MemoryChunk::FromHeapObject(object)->GetFlags() &
         MemoryChunk::BLACK_ALLOCATED;
}

}  // namespace v8::internal

#endif  // V8_HEAP_HEAP_LAYOUT_INL_H_
