// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-chunk.h"

#include "src/heap/base-space.h"
#include "src/heap/memory-chunk-metadata.h"

namespace v8 {
namespace internal {

// static
constexpr MemoryChunk::MainThreadFlags MemoryChunk::kAllFlagsMask;
// static
constexpr MemoryChunk::MainThreadFlags
    MemoryChunk::kPointersToHereAreInterestingMask;
// static
constexpr MemoryChunk::MainThreadFlags
    MemoryChunk::kPointersFromHereAreInterestingMask;
// static
constexpr MemoryChunk::MainThreadFlags MemoryChunk::kEvacuationCandidateMask;
// static
constexpr MemoryChunk::MainThreadFlags MemoryChunk::kIsInYoungGenerationMask;
// static
constexpr MemoryChunk::MainThreadFlags MemoryChunk::kIsLargePageMask;
// static
constexpr MemoryChunk::MainThreadFlags
    MemoryChunk::kSkipEvacuationSlotsRecordingMask;

// static
constexpr MemoryChunk::MainThreadFlags MemoryChunk::kCopyOnFlipFlagsMask;

#ifdef THREAD_SANITIZER

bool MemoryChunk::InReadOnlySpace() const {
  // This is needed because TSAN does not process the memory fence
  // emitted after page initialization.
  Metadata()->SynchronizedHeapLoad();
  return IsFlagSet(READ_ONLY_HEAP);
}

#endif  // THREAD_SANITIZER

#ifdef DEBUG

bool MemoryChunk::IsTrusted() const {
  bool is_trusted = IsFlagSet(IS_TRUSTED);
  DCHECK_EQ(is_trusted,
            Metadata()->owner()->identity() == TRUSTED_SPACE ||
                Metadata()->owner()->identity() == TRUSTED_LO_SPACE);
  return is_trusted;
}

#endif  // DEBUG

Heap* MemoryChunk::GetHeap() { return Metadata()->heap(); }

}  // namespace internal
}  // namespace v8
