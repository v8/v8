// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_CHUNK_INL_H_
#define V8_HEAP_MEMORY_CHUNK_INL_H_

#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/memory-chunk.h"

namespace v8 {
namespace internal {

V8_INLINE MemoryChunkMetadata* MemoryChunk::Metadata() {
  // If this changes, we also need to update
  // CodeStubAssembler::PageMetadataFromMemoryChunk
  return metadata_;
}

const MemoryChunkMetadata* MemoryChunk::Metadata() const {
  return const_cast<MemoryChunk*>(this)->Metadata();
}

Heap* MemoryChunk::GetHeap() { return Metadata()->heap(); }

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_CHUNK_INL_H_
