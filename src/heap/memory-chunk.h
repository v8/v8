// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_CHUNK_H_
#define V8_HEAP_MEMORY_CHUNK_H_

#include "src/base/build_config.h"
#include "src/base/hashing.h"
#include "src/init/isolate-group.h"

#if V8_ENABLE_STICKY_MARK_BITS_BOOL
#define UNREACHABLE_WITH_STICKY_MARK_BITS() UNREACHABLE()
#else
#define UNREACHABLE_WITH_STICKY_MARK_BITS()
#endif

namespace v8 {
namespace internal {

namespace debug_helper_internal {
class ReadStringVisitor;
}  // namespace  debug_helper_internal

namespace compiler::turboshaft {
template <typename Next>
class TurboshaftAssemblerOpInterface;
}

class CodeStubAssembler;
class ExternalReference;
class Heap;
class LargePageMetadata;
class MemoryChunkMetadata;
class MutablePageMetadata;
class PageMetadata;
class ReadOnlyPageMetadata;
template <typename T>
class Tagged;
class TestDebugHelper;

// A chunk of memory of any size.
//
// For the purpose of the V8 sandbox the chunk can reside in either trusted or
// untrusted memory. Most information can actually be found on the corresponding
// metadata object that can be retrieved via `Metadata()` and its friends.
class V8_EXPORT_PRIVATE MemoryChunk final {
 public:
  // All possible flags that can be set on a page. While the value of flags
  // doesn't matter in principle, keep flags used in the write barrier together
  // in order to have dense page flag checks in the write barrier.
  enum Flag : uintptr_t {
    NO_FLAGS = 0u,

    // This page belongs to a shared heap.
    IN_WRITABLE_SHARED_SPACE = 1u << 0,

    // These two flags are used in the write barrier to catch "interesting"
    // references.
    POINTERS_TO_HERE_ARE_INTERESTING = 1u << 1,
    POINTERS_FROM_HERE_ARE_INTERESTING = 1u << 2,

    // A page in the from-space or a young large page that was not scavenged
    // yet.
    FROM_PAGE = 1u << 3,
    // A page in the to-space or a young large page that was scavenged.
    TO_PAGE = 1u << 4,

    // |INCREMENTAL_MARKING|: Indicates whether incremental marking is currently
    // enabled.
    INCREMENTAL_MARKING = 1u << 5,

    // The memory chunk belongs to the read-only heap and does not participate
    // in garbage collection. This is used instead of owner for identity
    // checking since read-only chunks have no owner once they are detached.
    READ_ONLY_HEAP = 1u << 6,

    // Used in young generation checks. When sticky mark-bits are enabled and
    // major GC in progress, treat all objects as old.
    IS_MAJOR_GC_IN_PROGRESS = 1u << 7,

    // Used to mark chunks belonging to spaces that do not suppor young gen
    // allocations. Such chunks can never contain any young objects.
    CONTAINS_ONLY_OLD = 1u << 8,

    // Page was allocated during major incremental marking. May only contain old
    // objects.
    BLACK_ALLOCATED = 1u << 9,

    // ----------------------------------------------------------------
    // Values below here are not critical for the heap write barrier.

    LARGE_PAGE = 1u << 10,
    EVACUATION_CANDIDATE = 1u << 11,

    // |COMPACTION_WAS_ABORTED|: Indicates that the compaction in this page
    //   has been aborted and needs special handling by the sweeper.
    COMPACTION_WAS_ABORTED = 1u << 17,

    NEW_SPACE_BELOW_AGE_MARK = 1u << 18,

    // A Page with code objects.
    IS_EXECUTABLE = 1u << 21,

    // The memory chunk belongs to the trusted space. When the sandbox is
    // enabled, the trusted space is located outside of the sandbox and so its
    // content cannot be corrupted by an attacker.
    IS_TRUSTED = 1u << 22,
  };

  using MainThreadFlags = base::Flags<Flag, uintptr_t>;

  static constexpr MainThreadFlags kAllFlagsMask = ~MainThreadFlags(NO_FLAGS);
  static constexpr MainThreadFlags kPointersToHereAreInterestingMask =
      POINTERS_TO_HERE_ARE_INTERESTING;
  static constexpr MainThreadFlags kPointersFromHereAreInterestingMask =
      POINTERS_FROM_HERE_ARE_INTERESTING;
  static constexpr MainThreadFlags kEvacuationCandidateMask =
      EVACUATION_CANDIDATE;
  static constexpr MainThreadFlags kIsInYoungGenerationMask =
      MainThreadFlags(FROM_PAGE) | MainThreadFlags(TO_PAGE);
  static constexpr MainThreadFlags kIsInReadOnlyHeapMask = READ_ONLY_HEAP;
  static constexpr MainThreadFlags kIsLargePageMask = LARGE_PAGE;
  static constexpr MainThreadFlags kInSharedHeap = IN_WRITABLE_SHARED_SPACE;
  static constexpr MainThreadFlags kIncrementalMarking = INCREMENTAL_MARKING;
  static constexpr MainThreadFlags kSkipEvacuationSlotsRecordingMask =
      MainThreadFlags(kEvacuationCandidateMask) |
      MainThreadFlags(kIsInYoungGenerationMask);
  static constexpr MainThreadFlags kIsOnlyOldOrMajorGCInProgressMask =
      MainThreadFlags(CONTAINS_ONLY_OLD) |
      MainThreadFlags(IS_MAJOR_GC_IN_PROGRESS);

  MemoryChunk(MainThreadFlags flags, MemoryChunkMetadata* metadata);

  V8_INLINE Address address() const { return reinterpret_cast<Address>(this); }

  static constexpr Address BaseAddress(Address a) {
    // LINT.IfChange
    // If this changes, we also need to update
    // - CodeStubAssembler::MemoryChunkFromAddress
    // - MacroAssembler::MemoryChunkHeaderFromObject
    // - TurboshaftAssemblerOpInterface::MemoryChunkFromAddress
    return a & ~kAlignmentMask;
    // clang-format off
    // LINT.ThenChange(src/codegen/code-stub-assembler.cc, src/codegen/ia32/macro-assembler-ia32.cc, src/codegen/x64/macro-assembler-x64.cc, src/compiler/turboshaft/assembler.h)
    // clang-format on
  }

  V8_INLINE static MemoryChunk* FromAddress(Address addr) {
    return reinterpret_cast<MemoryChunk*>(BaseAddress(addr));
  }

  template <typename HeapObject>
  V8_INLINE static MemoryChunk* FromHeapObject(Tagged<HeapObject> object) {
    return FromAddress(object.ptr());
  }

  V8_INLINE MemoryChunkMetadata* Metadata(const Isolate* isolate);
  V8_INLINE const MemoryChunkMetadata* Metadata(const Isolate* isolate) const;

  V8_INLINE MemoryChunkMetadata* Metadata();
  V8_INLINE const MemoryChunkMetadata* Metadata() const;

  V8_INLINE MemoryChunkMetadata* MetadataNoIsolateCheck();
  V8_INLINE const MemoryChunkMetadata* MetadataNoIsolateCheck() const;

  V8_INLINE bool IsFlagSet(Flag flag) const {
    return untrusted_main_thread_flags_ & flag;
  }

  V8_INLINE bool IsMarking() const { return IsFlagSet(INCREMENTAL_MARKING); }

  V8_INLINE bool InWritableSharedSpace() const {
    return IsFlagSet(IN_WRITABLE_SHARED_SPACE);
  }

  V8_INLINE bool InYoungGeneration() const {
    UNREACHABLE_WITH_STICKY_MARK_BITS();
    constexpr uintptr_t kYoungGenerationMask = FROM_PAGE | TO_PAGE;
    return GetFlags() & kYoungGenerationMask;
  }

  // Checks whether chunk is either in young gen or shared heap.
  V8_INLINE bool IsYoungOrSharedChunk() const {
    constexpr uintptr_t kYoungOrSharedChunkMask =
        FROM_PAGE | TO_PAGE | IN_WRITABLE_SHARED_SPACE;
    return GetFlags() & kYoungOrSharedChunkMask;
  }

  V8_INLINE MainThreadFlags GetFlags() const {
    return untrusted_main_thread_flags_;
  }

  // Emits a memory barrier. For TSAN builds the other thread needs to perform
  // MemoryChunk::SynchronizedLoad() to simulate the barrier.
  void InitializationMemoryFence();

#ifdef THREAD_SANITIZER
  void SynchronizedLoad() const;
  bool InReadOnlySpace() const;
#else
  V8_INLINE bool InReadOnlySpace() const { return IsFlagSet(READ_ONLY_HEAP); }
#endif

#ifdef V8_ENABLE_SANDBOX
  // Flags are stored in the page header and are not safe to rely on for sandbox
  // checks. This alternative version will check if the page is read-only
  // without relying on the inline flag.
  bool SandboxSafeInReadOnlySpace() const;
#endif

  V8_INLINE bool InCodeSpace() const { return IsFlagSet(IS_EXECUTABLE); }

  V8_INLINE bool InTrustedSpace() const { return IsFlagSet(IS_TRUSTED); }

  V8_INLINE bool IsEvacuationCandidate() const;

  bool ShouldSkipEvacuationSlotRecording() const {
    MainThreadFlags flags = GetFlags();
    return ((flags & kSkipEvacuationSlotsRecordingMask) != 0) &&
           ((flags & COMPACTION_WAS_ABORTED) == 0);
  }

  Executability executable() const {
    return IsFlagSet(IS_EXECUTABLE) ? EXECUTABLE : NOT_EXECUTABLE;
  }

  bool IsFromPage() const {
    UNREACHABLE_WITH_STICKY_MARK_BITS();
    return IsFlagSet(FROM_PAGE);
  }
  bool IsToPage() const {
    UNREACHABLE_WITH_STICKY_MARK_BITS();
    return IsFlagSet(TO_PAGE);
  }
  bool IsLargePage() const { return IsFlagSet(LARGE_PAGE); }
  bool InNewSpace() const { return InYoungGeneration() && !IsLargePage(); }
  bool InNewLargeObjectSpace() const {
    return InYoungGeneration() && IsLargePage();
  }
  bool IsOnlyOldOrMajorMarkingOn() const {
    return GetFlags() & kIsOnlyOldOrMajorGCInProgressMask;
  }

  V8_INLINE static constexpr bool IsAligned(Address address) {
    return (address & kAlignmentMask) == 0;
  }

#ifdef DEBUG
  bool IsTrusted() const;
#else
  bool IsTrusted() const { return IsFlagSet(IS_TRUSTED); }
#endif

  static intptr_t GetAlignmentForAllocation() { return kAlignment; }
  // The macro and code stub assemblers need access to the alignment mask to
  // implement functionality from this class. In particular, this is used to
  // implement the header lookups and to calculate the object offsets in the
  // page.
  static constexpr intptr_t GetAlignmentMaskForAssembler() {
    return kAlignmentMask;
  }

  static constexpr uint32_t AddressToOffset(Address address) {
    return static_cast<uint32_t>(address) & kAlignmentMask;
  }

#ifdef DEBUG
  size_t Offset(Address addr) const;
  // RememberedSetOperations take an offset to an end address that can be behind
  // the allocated memory.
  size_t OffsetMaybeOutOfRange(Address addr) const;
#else
  size_t Offset(Address addr) const { return addr - address(); }
  size_t OffsetMaybeOutOfRange(Address addr) const { return Offset(addr); }
#endif

#ifdef V8_ENABLE_SANDBOX
  static void ClearMetadataPointer(MemoryChunkMetadata* metadata);
  static void ResetMetadataPointer(Isolate* isolate,
                                   MemoryChunkMetadata* metadata);
#endif

 private:
  // Keep offsets and masks private to only expose them with matching friend
  // declarations.
  static constexpr intptr_t FlagsOffset() {
    return offsetof(MemoryChunk, untrusted_main_thread_flags_);
  }

  static constexpr intptr_t kAlignment =
      (static_cast<uintptr_t>(1) << kPageSizeBits);
  static constexpr intptr_t kAlignmentMask = kAlignment - 1;

#ifdef V8_ENABLE_SANDBOX
#ifndef V8_EXTERNAL_CODE_SPACE
#error The global metadata pointer table requires a single external code space.
#endif

  static constexpr intptr_t MetadataIndexOffset() {
    return offsetof(MemoryChunk, metadata_index_);
  }

  static uint32_t MetadataTableIndex(Address chunk_address);

  V8_INLINE static IsolateGroup::MemoryChunkMetadataTableEntry*
  MetadataTableAddress() {
    return IsolateGroup::current()->metadata_pointer_table();
  }

  // For MetadataIndexOffset().
  friend class debug_helper_internal::ReadStringVisitor;
  // For MetadataTableAddress().
  friend class ExternalReference;
  friend class TestDebugHelper;

#else  // !V8_ENABLE_SANDBOX

  static constexpr intptr_t MetadataOffset() {
    return offsetof(MemoryChunk, metadata_);
  }

#endif  // !V8_ENABLE_SANDBOX

  template <bool check_isolate>
  MemoryChunkMetadata* MetadataImpl(const Isolate* isolate);
  template <bool check_isolate>
  const MemoryChunkMetadata* MetadataImpl(const Isolate* isolate) const;

  // Flags that are only mutable from the main thread when no concurrent
  // component (e.g. marker, sweeper, compilation, allocation) is running.
  //
  // For the purpose of the V8 sandbox these flags can generally not be trusted.
  // Only when the chunk is known to live in trusted space the flags are assumed
  // to be safe from modification.
  MainThreadFlags untrusted_main_thread_flags_;

#ifdef V8_ENABLE_SANDBOX
  uint32_t metadata_index_;
#else
  MemoryChunkMetadata* metadata_;
#endif

  // For main_thread_flags_.
  friend class MutablePageMetadata;
  // For kMetadataPointerTableSizeMask, FlagsOffset(), MetadataIndexOffset(),
  // MetadataOffset().
  friend class CodeStubAssembler;
  friend class MacroAssembler;
  template <typename Next>
  friend class compiler::turboshaft::TurboshaftAssemblerOpInterface;
};

DEFINE_OPERATORS_FOR_FLAGS(MemoryChunk::MainThreadFlags)

}  // namespace internal

namespace base {

// Define special hash function for chunk pointers, to be used with std data
// structures, e.g.
// std::unordered_set<MemoryChunk*, base::hash<MemoryChunk*>
// This hash function discards the trailing zero bits (chunk alignment).
// Notice that, when pointer compression is enabled, it also discards the
// cage base.
template <>
struct hash<const i::MemoryChunk*> {
  V8_INLINE size_t operator()(const i::MemoryChunk* chunk) const {
    return static_cast<v8::internal::Tagged_t>(
               reinterpret_cast<uintptr_t>(chunk)) >>
           kPageSizeBits;
  }
};

template <>
struct hash<i::MemoryChunk*> {
  V8_INLINE size_t operator()(i::MemoryChunk* chunk) const {
    return hash<const i::MemoryChunk*>()(chunk);
  }
};

}  // namespace base

}  // namespace v8

#undef UNREACHABLE_WITH_STICKY_MARK_BITS

#endif  // V8_HEAP_MEMORY_CHUNK_H_
