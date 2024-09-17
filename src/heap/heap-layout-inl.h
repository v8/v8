// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_LAYOUT_INL_H_
#define V8_HEAP_HEAP_LAYOUT_INL_H_

#include "src/heap/heap-layout.h"
#include "src/heap/memory-chunk.h"

namespace v8::internal {

// static
bool HeapLayout::InReadOnlySpace(Tagged<HeapObject> object) {
  return MemoryChunk::FromHeapObject(object)->InReadOnlySpace();
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

}  // namespace v8::internal

#endif  // V8_HEAP_HEAP_LAYOUT_INL_H_
