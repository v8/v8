// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-chunk.h"

#include "src/common/code-memory-access-inl.h"
#include "src/heap/base-space.h"
#include "src/heap/large-page.h"
#include "src/heap/page.h"
#include "src/heap/read-only-spaces.h"

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

void MemoryChunk::InitializationMemoryFence() {
  base::SeqCst_MemoryFence();

#ifdef THREAD_SANITIZER
  // Since TSAN does not process memory fences, we use the following annotation
  // to tell TSAN that there is no data race when emitting a
  // InitializationMemoryFence. Note that the other thread still needs to
  // perform MutablePageMetadata::synchronized_heap().
  metadata_->SynchronizedHeapStore();
  base::Release_Store(reinterpret_cast<base::AtomicWord*>(&metadata_),
                      reinterpret_cast<base::AtomicWord>(metadata_));
#endif
}

#ifdef THREAD_SANITIZER

void MemoryChunk::SynchronizedLoad() const {
  MemoryChunkMetadata* metadata = reinterpret_cast<MemoryChunkMetadata*>(
      base::Acquire_Load(reinterpret_cast<base::AtomicWord*>(
          &(const_cast<MemoryChunk*>(this)->metadata_))));
  metadata->SynchronizedHeapLoad();
}

bool MemoryChunk::InReadOnlySpace() const {
  // This is needed because TSAN does not process the memory fence
  // emitted after page initialization.
  SynchronizedLoad();
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

size_t MemoryChunk::Offset(Address addr) const {
  DCHECK_GE(addr, Metadata()->area_start());
  DCHECK_LE(addr, address() + Metadata()->size());
  return addr - address();
}

size_t MemoryChunk::OffsetMaybeOutOfRange(Address addr) const {
  DCHECK_GE(addr, Metadata()->area_start());
  return addr - address();
}

#endif  // DEBUG

void MemoryChunk::SetFlagSlow(Flag flag) {
  if (executable()) {
    RwxMemoryWriteScope scope("Set a MemoryChunk flag in executable memory.");
    SetFlagUnlocked(flag);
  } else {
    SetFlagNonExecutable(flag);
  }
}

void MemoryChunk::ClearFlagSlow(Flag flag) {
  if (executable()) {
    RwxMemoryWriteScope scope("Clear a MemoryChunk flag in executable memory.");
    ClearFlagUnlocked(flag);
  } else {
    ClearFlagNonExecutable(flag);
  }
}

Heap* MemoryChunk::GetHeap() { return Metadata()->heap(); }

// static
MemoryChunk::MainThreadFlags MemoryChunk::OldGenerationPageFlags(
    MarkingMode marking_mode, bool in_shared_space) {
  MainThreadFlags flags_to_set = NO_FLAGS;

  if (marking_mode == MarkingMode::kMajorMarking) {
    flags_to_set |= MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING |
                    MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING |
                    MemoryChunk::INCREMENTAL_MARKING;
  } else if (in_shared_space) {
    // We need to track pointers into the SHARED_SPACE for OLD_TO_SHARED.
    flags_to_set |= MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING;
  } else {
    flags_to_set |= MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING;
    if (marking_mode == MarkingMode::kMinorMarking) {
      flags_to_set |= MemoryChunk::INCREMENTAL_MARKING;
    }
  }

  return flags_to_set;
}

// static
MemoryChunk::MainThreadFlags MemoryChunk::YoungGenerationPageFlags(
    MarkingMode marking_mode) {
  MainThreadFlags flags = MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING;
  if (marking_mode != MarkingMode::kNoMarking) {
    flags |= MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING;
    flags |= MemoryChunk::INCREMENTAL_MARKING;
  }
  return flags;
}

void MemoryChunk::SetOldGenerationPageFlags(MarkingMode marking_mode,
                                            bool in_shared_space) {
  MainThreadFlags flags_to_set =
      OldGenerationPageFlags(marking_mode, in_shared_space);
  MainThreadFlags flags_to_clear = NO_FLAGS;

  if (marking_mode != MarkingMode::kMajorMarking) {
    if (in_shared_space) {
      // No need to track OLD_TO_NEW or OLD_TO_SHARED within the shared space.
      flags_to_clear |= MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING |
                        MemoryChunk::INCREMENTAL_MARKING;
    } else {
      flags_to_clear |= MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING;
      if (marking_mode != MarkingMode::kMinorMarking) {
        flags_to_clear |= MemoryChunk::INCREMENTAL_MARKING;
      }
    }
  }

  SetFlagsUnlocked(flags_to_set, flags_to_set);
  ClearFlagsUnlocked(flags_to_clear);
}

void MemoryChunk::SetYoungGenerationPageFlags(MarkingMode marking_mode) {
  MainThreadFlags flags_to_set = YoungGenerationPageFlags(marking_mode);
  MainThreadFlags flags_to_clear = NO_FLAGS;

  if (marking_mode == MarkingMode::kNoMarking) {
    flags_to_clear |= MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING;
    flags_to_clear |= MemoryChunk::INCREMENTAL_MARKING;
  }

  SetFlagsNonExecutable(flags_to_set, flags_to_set);
  ClearFlagsNonExecutable(flags_to_clear);
}

}  // namespace internal
}  // namespace v8
