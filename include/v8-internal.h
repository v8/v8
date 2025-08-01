// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_INTERNAL_H_
#define INCLUDE_V8_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <atomic>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>

#include "v8config.h"  // NOLINT(build/include_directory)

// TODO(pkasting): Use <compare>/spaceship unconditionally after dropping
// support for old libstdc++ versions.
#if __has_include(<version>)
#include <version>
#endif
#if defined(__cpp_lib_three_way_comparison) &&   \
    __cpp_lib_three_way_comparison >= 201711L && \
    defined(__cpp_lib_concepts) && __cpp_lib_concepts >= 202002L
#include <compare>
#include <concepts>

#define V8_HAVE_SPACESHIP_OPERATOR 1
#else
#define V8_HAVE_SPACESHIP_OPERATOR 0
#endif

namespace v8 {

class Array;
class Context;
class Data;
class Isolate;

namespace internal {

class Heap;
class LocalHeap;
class Isolate;
class IsolateGroup;
class LocalIsolate;

typedef uintptr_t Address;
static constexpr Address kNullAddress = 0;

constexpr int KB = 1024;
constexpr int MB = KB * 1024;
constexpr int GB = MB * 1024;
#ifdef V8_TARGET_ARCH_X64
constexpr size_t TB = size_t{GB} * 1024;
#endif

/**
 * Configuration of tagging scheme.
 */
const int kApiSystemPointerSize = sizeof(void*);
const int kApiDoubleSize = sizeof(double);
const int kApiInt32Size = sizeof(int32_t);
const int kApiInt64Size = sizeof(int64_t);
const int kApiSizetSize = sizeof(size_t);

// Tag information for HeapObject.
const int kHeapObjectTag = 1;
const int kWeakHeapObjectTag = 3;
const int kHeapObjectTagSize = 2;
const intptr_t kHeapObjectTagMask = (1 << kHeapObjectTagSize) - 1;
const intptr_t kHeapObjectReferenceTagMask = 1 << (kHeapObjectTagSize - 1);

// Tag information for fowarding pointers stored in object headers.
// 0b00 at the lowest 2 bits in the header indicates that the map word is a
// forwarding pointer.
const int kForwardingTag = 0;
const int kForwardingTagSize = 2;
const intptr_t kForwardingTagMask = (1 << kForwardingTagSize) - 1;

// Tag information for Smi.
const int kSmiTag = 0;
const int kSmiTagSize = 1;
const intptr_t kSmiTagMask = (1 << kSmiTagSize) - 1;

template <size_t tagged_ptr_size>
struct SmiTagging;

constexpr intptr_t kIntptrAllBitsSet = intptr_t{-1};
constexpr uintptr_t kUintptrAllBitsSet =
    static_cast<uintptr_t>(kIntptrAllBitsSet);

// Smi constants for systems where tagged pointer is a 32-bit value.
template <>
struct SmiTagging<4> {
  enum { kSmiShiftSize = 0, kSmiValueSize = 31 };

  static constexpr intptr_t kSmiMinValue =
      static_cast<intptr_t>(kUintptrAllBitsSet << (kSmiValueSize - 1));
  static constexpr intptr_t kSmiMaxValue = -(kSmiMinValue + 1);

  V8_INLINE static constexpr int SmiToInt(Address value) {
    int shift_bits = kSmiTagSize + kSmiShiftSize;
    // Truncate and shift down (requires >> to be sign extending).
    return static_cast<int32_t>(static_cast<uint32_t>(value)) >> shift_bits;
  }

  template <class T, typename std::enable_if_t<std::is_integral_v<T> &&
                                               std::is_signed_v<T>>* = nullptr>
  V8_INLINE static constexpr bool IsValidSmi(T value) {
    // Is value in range [kSmiMinValue, kSmiMaxValue].
    // Use unsigned operations in order to avoid undefined behaviour in case of
    // signed integer overflow.
    return (static_cast<uintptr_t>(value) -
            static_cast<uintptr_t>(kSmiMinValue)) <=
           (static_cast<uintptr_t>(kSmiMaxValue) -
            static_cast<uintptr_t>(kSmiMinValue));
  }

  template <class T,
            typename std::enable_if_t<std::is_integral_v<T> &&
                                      std::is_unsigned_v<T>>* = nullptr>
  V8_INLINE static constexpr bool IsValidSmi(T value) {
    static_assert(kSmiMaxValue <= std::numeric_limits<uintptr_t>::max());
    return value <= static_cast<uintptr_t>(kSmiMaxValue);
  }

  // Same as the `intptr_t` version but works with int64_t on 32-bit builds
  // without slowing down anything else.
  V8_INLINE static constexpr bool IsValidSmi(int64_t value) {
    return (static_cast<uint64_t>(value) -
            static_cast<uint64_t>(kSmiMinValue)) <=
           (static_cast<uint64_t>(kSmiMaxValue) -
            static_cast<uint64_t>(kSmiMinValue));
  }

  V8_INLINE static constexpr bool IsValidSmi(uint64_t value) {
    static_assert(kSmiMaxValue <= std::numeric_limits<uint64_t>::max());
    return value <= static_cast<uint64_t>(kSmiMaxValue);
  }
};

// Smi constants for systems where tagged pointer is a 64-bit value.
template <>
struct SmiTagging<8> {
  enum { kSmiShiftSize = 31, kSmiValueSize = 32 };

  static constexpr intptr_t kSmiMinValue =
      static_cast<intptr_t>(kUintptrAllBitsSet << (kSmiValueSize - 1));
  static constexpr intptr_t kSmiMaxValue = -(kSmiMinValue + 1);

  V8_INLINE static constexpr int SmiToInt(Address value) {
    int shift_bits = kSmiTagSize + kSmiShiftSize;
    // Shift down and throw away top 32 bits.
    return static_cast<int>(static_cast<intptr_t>(value) >> shift_bits);
  }

  template <class T, typename std::enable_if_t<std::is_integral_v<T> &&
                                               std::is_signed_v<T>>* = nullptr>
  V8_INLINE static constexpr bool IsValidSmi(T value) {
    // To be representable as a long smi, the value must be a 32-bit integer.
    return std::numeric_limits<int32_t>::min() <= value &&
           value <= std::numeric_limits<int32_t>::max();
  }

  template <class T,
            typename std::enable_if_t<std::is_integral_v<T> &&
                                      std::is_unsigned_v<T>>* = nullptr>
  V8_INLINE static constexpr bool IsValidSmi(T value) {
    return value <= std::numeric_limits<int32_t>::max();
  }
};

#ifdef V8_COMPRESS_POINTERS
// See v8:7703 or src/common/ptr-compr-inl.h for details about pointer
// compression.
constexpr size_t kPtrComprCageReservationSize = size_t{1} << 32;
constexpr size_t kPtrComprCageBaseAlignment = size_t{1} << 32;

static_assert(
    kApiSystemPointerSize == kApiInt64Size,
    "Pointer compression can be enabled only for 64-bit architectures");
const int kApiTaggedSize = kApiInt32Size;
#else
const int kApiTaggedSize = kApiSystemPointerSize;
#endif

constexpr bool PointerCompressionIsEnabled() {
  return kApiTaggedSize != kApiSystemPointerSize;
}

#ifdef V8_31BIT_SMIS_ON_64BIT_ARCH
using PlatformSmiTagging = SmiTagging<kApiInt32Size>;
#else
using PlatformSmiTagging = SmiTagging<kApiTaggedSize>;
#endif

// TODO(ishell): Consinder adding kSmiShiftBits = kSmiShiftSize + kSmiTagSize
// since it's used much more often than the inividual constants.
const int kSmiShiftSize = PlatformSmiTagging::kSmiShiftSize;
const int kSmiValueSize = PlatformSmiTagging::kSmiValueSize;
const int kSmiMinValue = static_cast<int>(PlatformSmiTagging::kSmiMinValue);
const int kSmiMaxValue = static_cast<int>(PlatformSmiTagging::kSmiMaxValue);
constexpr bool SmiValuesAre31Bits() { return kSmiValueSize == 31; }
constexpr bool SmiValuesAre32Bits() { return kSmiValueSize == 32; }
constexpr bool Is64() { return kApiSystemPointerSize == sizeof(int64_t); }

V8_INLINE static constexpr Address IntToSmi(int value) {
  return (static_cast<Address>(value) << (kSmiTagSize + kSmiShiftSize)) |
         kSmiTag;
}

/*
 * Sandbox related types, constants, and functions.
 */
constexpr bool SandboxIsEnabled() {
#ifdef V8_ENABLE_SANDBOX
  return true;
#else
  return false;
#endif
}

// SandboxedPointers are guaranteed to point into the sandbox. This is achieved
// for example by storing them as offset rather than as raw pointers.
using SandboxedPointer_t = Address;

#ifdef V8_ENABLE_SANDBOX

// Size of the sandbox, excluding the guard regions surrounding it.
#if defined(V8_TARGET_OS_ANDROID)
// On Android, most 64-bit devices seem to be configured with only 39 bits of
// virtual address space for userspace. As such, limit the sandbox to 128GB (a
// quarter of the total available address space).
constexpr size_t kSandboxSizeLog2 = 37;  // 128 GB
#elif defined(V8_TARGET_OS_IOS)
// On iOS, we only get 64 GB of usable virtual address space even with the
// "jumbo" extended virtual addressing entitlement. Limit the sandbox size to
// 16 GB so that the base address + size for the emulated virtual address space
// lies within the 64 GB total virtual address space.
constexpr size_t kSandboxSizeLog2 = 34;  // 16 GB
#else
// Everywhere else use a 1TB sandbox.
constexpr size_t kSandboxSizeLog2 = 40;  // 1 TB
#endif  // V8_TARGET_OS_ANDROID
constexpr size_t kSandboxSize = 1ULL << kSandboxSizeLog2;

// Required alignment of the sandbox. For simplicity, we require the
// size of the guard regions to be a multiple of this, so that this specifies
// the alignment of the sandbox including and excluding surrounding guard
// regions. The alignment requirement is due to the pointer compression cage
// being located at the start of the sandbox.
constexpr size_t kSandboxAlignment = kPtrComprCageBaseAlignment;

// Sandboxed pointers are stored inside the heap as offset from the sandbox
// base shifted to the left. This way, it is guaranteed that the offset is
// smaller than the sandbox size after shifting it to the right again. This
// constant specifies the shift amount.
constexpr uint64_t kSandboxedPointerShift = 64 - kSandboxSizeLog2;

// Size of the guard regions surrounding the sandbox. This assumes a worst-case
// scenario of a 32-bit unsigned index used to access an array of 64-bit values
// with an additional offset of up to 32GB.
// In particular, accesses to TypedArrays are effectively computed as
// `entry_pointer = array->base + array->offset + index * array->element_size`.
// (although in that case, the offset can only be up to 4GB large).
// See also https://crbug.com/40070746 for more details.
constexpr size_t kSandboxGuardRegionSize = 64ULL * GB;

static_assert((kSandboxGuardRegionSize % kSandboxAlignment) == 0,
              "The size of the guard regions around the sandbox must be a "
              "multiple of its required alignment.");

// On OSes where reserving virtual memory is too expensive to reserve the
// entire address space backing the sandbox, notably Windows pre 8.1, we create
// a partially reserved sandbox that doesn't actually reserve most of the
// memory, and so doesn't have the desired security properties as unrelated
// memory allocations could end up inside of it, but which still ensures that
// objects that should be located inside the sandbox are allocated within
// kSandboxSize bytes from the start of the sandbox. The minimum size of the
// region that is actually reserved for such a sandbox is specified by this
// constant and should be big enough to contain the pointer compression cage as
// well as the ArrayBuffer partition.
constexpr size_t kSandboxMinimumReservationSize = 8ULL * GB;

static_assert(kSandboxMinimumReservationSize > kPtrComprCageReservationSize,
              "The minimum reservation size for a sandbox must be larger than "
              "the pointer compression cage contained within it.");

// The maximum buffer size allowed inside the sandbox. This is mostly dependent
// on the size of the guard regions around the sandbox: an attacker must not be
// able to construct a buffer that appears larger than the guard regions and
// thereby "reach out of" the sandbox.
constexpr size_t kMaxSafeBufferSizeForSandbox = 32ULL * GB - 1;
static_assert(kMaxSafeBufferSizeForSandbox <= kSandboxGuardRegionSize,
              "The maximum allowed buffer size must not be larger than the "
              "sandbox's guard regions");

constexpr size_t kBoundedSizeShift = 29;
static_assert(1ULL << (64 - kBoundedSizeShift) ==
                  kMaxSafeBufferSizeForSandbox + 1,
              "The maximum size of a BoundedSize must be synchronized with the "
              "kMaxSafeBufferSizeForSandbox");

#endif  // V8_ENABLE_SANDBOX

#ifdef V8_COMPRESS_POINTERS

#ifdef V8_TARGET_OS_ANDROID
// The size of the virtual memory reservation for an external pointer table.
// This determines the maximum number of entries in a table. Using a maximum
// size allows omitting bounds checks on table accesses if the indices are
// guaranteed (e.g. through shifting) to be below the maximum index. This
// value must be a power of two.
constexpr size_t kExternalPointerTableReservationSize = 256 * MB;

// The external pointer table indices stored in HeapObjects as external
// pointers are shifted to the left by this amount to guarantee that they are
// smaller than the maximum table size even after the C++ compiler multiplies
// them by 8 to be used as indexes into a table of 64 bit pointers.
constexpr uint32_t kExternalPointerIndexShift = 7;
#else
constexpr size_t kExternalPointerTableReservationSize = 512 * MB;
constexpr uint32_t kExternalPointerIndexShift = 6;
#endif  // V8_TARGET_OS_ANDROID

// The maximum number of entries in an external pointer table.
constexpr int kExternalPointerTableEntrySize = 8;
constexpr int kExternalPointerTableEntrySizeLog2 = 3;
constexpr size_t kMaxExternalPointers =
    kExternalPointerTableReservationSize / kExternalPointerTableEntrySize;
static_assert((1 << (32 - kExternalPointerIndexShift)) == kMaxExternalPointers,
              "kExternalPointerTableReservationSize and "
              "kExternalPointerIndexShift don't match");

#else  // !V8_COMPRESS_POINTERS

// Needed for the V8.SandboxedExternalPointersCount histogram.
constexpr size_t kMaxExternalPointers = 0;

#endif  // V8_COMPRESS_POINTERS

constexpr uint64_t kExternalPointerMarkBit = 1ULL << 48;
constexpr uint64_t kExternalPointerTagShift = 49;
constexpr uint64_t kExternalPointerTagMask = 0x00fe000000000000ULL;
constexpr uint64_t kExternalPointerShiftedTagMask =
    kExternalPointerTagMask >> kExternalPointerTagShift;
static_assert(kExternalPointerShiftedTagMask << kExternalPointerTagShift ==
              kExternalPointerTagMask);
constexpr uint64_t kExternalPointerTagAndMarkbitMask = 0x00ff000000000000ULL;
constexpr uint64_t kExternalPointerPayloadMask = 0xff00ffffffffffffULL;

// A ExternalPointerHandle represents a (opaque) reference to an external
// pointer that can be stored inside the sandbox. A ExternalPointerHandle has
// meaning only in combination with an (active) Isolate as it references an
// external pointer stored in the currently active Isolate's
// ExternalPointerTable. Internally, an ExternalPointerHandles is simply an
// index into an ExternalPointerTable that is shifted to the left to guarantee
// that it is smaller than the size of the table.
using ExternalPointerHandle = uint32_t;

// ExternalPointers point to objects located outside the sandbox. When the V8
// sandbox is enabled, these are stored on heap as ExternalPointerHandles,
// otherwise they are simply raw pointers.
#ifdef V8_ENABLE_SANDBOX
using ExternalPointer_t = ExternalPointerHandle;
#else
using ExternalPointer_t = Address;
#endif

constexpr ExternalPointer_t kNullExternalPointer = 0;
constexpr ExternalPointerHandle kNullExternalPointerHandle = 0;

// See `ExternalPointerHandle` for the main documentation. The difference to
// `ExternalPointerHandle` is that the handle does not represent an arbitrary
// external pointer but always refers to an object managed by `CppHeap`. The
// handles are using in combination with a dedicated table for `CppHeap`
// references.
using CppHeapPointerHandle = uint32_t;

// The actual pointer to objects located on the `CppHeap`. When pointer
// compression is enabled these pointers are stored as `CppHeapPointerHandle`.
// In non-compressed configurations the pointers are simply stored as raw
// pointers.
#ifdef V8_COMPRESS_POINTERS
using CppHeapPointer_t = CppHeapPointerHandle;
#else
using CppHeapPointer_t = Address;
#endif

constexpr CppHeapPointer_t kNullCppHeapPointer = 0;
constexpr CppHeapPointerHandle kNullCppHeapPointerHandle = 0;

constexpr uint64_t kCppHeapPointerMarkBit = 1ULL;
constexpr uint64_t kCppHeapPointerTagShift = 1;
constexpr uint64_t kCppHeapPointerPayloadShift = 16;

#ifdef V8_COMPRESS_POINTERS
// CppHeapPointers use a dedicated pointer table. These constants control the
// size and layout of the table. See the corresponding constants for the
// external pointer table for further details.
constexpr size_t kCppHeapPointerTableReservationSize =
    kExternalPointerTableReservationSize;
constexpr uint32_t kCppHeapPointerIndexShift = kExternalPointerIndexShift;

constexpr int kCppHeapPointerTableEntrySize = 8;
constexpr int kCppHeapPointerTableEntrySizeLog2 = 3;
constexpr size_t kMaxCppHeapPointers =
    kCppHeapPointerTableReservationSize / kCppHeapPointerTableEntrySize;
static_assert((1 << (32 - kCppHeapPointerIndexShift)) == kMaxCppHeapPointers,
              "kCppHeapPointerTableReservationSize and "
              "kCppHeapPointerIndexShift don't match");

#else  // !V8_COMPRESS_POINTERS

// Needed for the V8.SandboxedCppHeapPointersCount histogram.
constexpr size_t kMaxCppHeapPointers = 0;

#endif  // V8_COMPRESS_POINTERS

// Generic tag range struct to represent ranges of type tags.
//
// When referencing external objects via pointer tables, type tags are
// frequently necessary to guarantee type safety for the external objects. When
// support for subtyping is necessary, range-based type checks are used in
// which all subtypes of a given supertype use contiguous tags. This struct can
// then be used to represent such a type range.
//
// As an example, consider the following type hierarchy:
//
//          A     F
//         / \
//        B   E
//       / \
//      C   D
//
// A potential type id assignment for range-based type checks is
// {A: 0, B: 1, C: 2, D: 3, E: 4, F: 5}. With that, the type check for type A
// would check for the range [A, E], while the check for B would check range
// [B, D], and for F it would simply check [F, F].
//
// In addition, there is an option for performance tweaks: if the size of the
// type range corresponding to a supertype is a power of two and starts at a
// power of two (e.g. [0x100, 0x13f]), then the compiler can often optimize
// the type check to use even fewer instructions (essentially replace a AND +
// SUB with a single AND).
//
template <typename Tag>
struct TagRange {
  static_assert(std::is_enum_v<Tag> &&
                    std::is_same_v<std::underlying_type_t<Tag>, uint16_t>,
                "Tag parameter must be an enum with base type uint16_t");

  // Construct the inclusive tag range [first, last].
  constexpr TagRange(Tag first, Tag last) : first(first), last(last) {}

  // Construct a tag range consisting of a single tag.
  //
  // A single tag is always implicitly convertible to a tag range. This greatly
  // increases readability as most of the time, the exact tag of a field is
  // known and so no tag range needs to explicitly be created for it.
  constexpr TagRange(Tag tag)  // NOLINT(runtime/explicit)
      : first(tag), last(tag) {}

  // Construct an empty tag range.
  constexpr TagRange() : TagRange(static_cast<Tag>(0)) {}

  // A tag range is considered empty if it only contains the null tag.
  constexpr bool IsEmpty() const { return first == 0 && last == 0; }

  constexpr size_t Size() const {
    if (IsEmpty()) {
      return 0;
    } else {
      return last - first + 1;
    }
  }

  constexpr bool Contains(Tag tag) const {
    // Need to perform the math with uint32_t. Otherwise, the uint16_ts would
    // be promoted to (signed) int, allowing the compiler to (wrongly) assume
    // that an underflow cannot happen as that would be undefined behavior.
    return static_cast<uint32_t>(tag) - first <=
           static_cast<uint32_t>(last) - first;
  }

  constexpr bool Contains(TagRange tag_range) const {
    return tag_range.first >= first && tag_range.last <= last;
  }

  constexpr bool operator==(const TagRange other) const {
    return first == other.first && last == other.last;
  }

  constexpr size_t hash_value() const {
    static_assert(std::is_same_v<std::underlying_type_t<Tag>, uint16_t>);
    return (static_cast<size_t>(first) << 16) | last;
  }

  // Internally we represent tag ranges as half-open ranges [first, last).
  const Tag first;
  const Tag last;
};

//
// External Pointers.
//
// When the sandbox is enabled, external pointers are stored in an external
// pointer table and are referenced from HeapObjects through an index (a
// "handle"). When stored in the table, the pointers are tagged with per-type
// tags to prevent type confusion attacks between different external objects.
//
// When loading an external pointer, a range of allowed tags can be specified.
// This way, type hierarchies can be supported. The main requirement for that
// is that all (transitive) child classes of a given parent class have type ids
// in the same range, and that there are no unrelated types in that range. For
// more details about how to assign type tags to types, see the TagRange class.
//
// The external pointer sandboxing mechanism ensures that every access to an
// external pointer field will result in a valid pointer of the expected type
// even in the presence of an attacker able to corrupt memory inside the
// sandbox. However, if any data related to the external object is stored
// inside the sandbox it may still be corrupted and so must be validated before
// use or moved into the external object. Further, an attacker will always be
// able to substitute different external pointers of the same type for each
// other. Therefore, code using external pointers must be written in a
// "substitution-safe" way, i.e. it must always be possible to substitute
// external pointers of the same type without causing memory corruption outside
// of the sandbox. Generally this is achieved by referencing any group of
// related external objects through a single external pointer.
//
// Currently we use bit 62 for the marking bit which should always be unused as
// it's part of the non-canonical address range. When Arm's top-byte ignore
// (TBI) is enabled, this bit will be part of the ignored byte, and we assume
// that the Embedder is not using this byte (really only this one bit) for any
// other purpose. This bit also does not collide with the memory tagging
// extension (MTE) which would use bits [56, 60).
//
// External pointer tables are also available even when the sandbox is off but
// pointer compression is on. In that case, the mechanism can be used to ease
// alignment requirements as it turns unaligned 64-bit raw pointers into
// aligned 32-bit indices. To "opt-in" to the external pointer table mechanism
// for this purpose, instead of using the ExternalPointer accessors one needs to
// use ExternalPointerHandles directly and use them to access the pointers in an
// ExternalPointerTable.
//
// The tag is currently in practice limited to 15 bits since it needs to fit
// together with a marking bit into the unused parts of a pointer.
enum ExternalPointerTag : uint16_t {
  kFirstExternalPointerTag = 0,
  kExternalPointerNullTag = 0,

  // When adding new tags, please ensure that the code using these tags is
  // "substitution-safe", i.e. still operate safely if external pointers of the
  // same type are swapped by an attacker. See comment above for more details.

  // Shared external pointers are owned by the shared Isolate and stored in the
  // shared external pointer table associated with that Isolate, where they can
  // be accessed from multiple threads at the same time. The objects referenced
  // in this way must therefore always be thread-safe.
  kFirstSharedExternalPointerTag,
  kWaiterQueueNodeTag = kFirstSharedExternalPointerTag,
  kExternalStringResourceTag,
  kExternalStringResourceDataTag,
  kLastSharedExternalPointerTag = kExternalStringResourceDataTag,

  // External pointers using these tags are kept in a per-Isolate external
  // pointer table and can only be accessed when this Isolate is active.
  kNativeContextMicrotaskQueueTag,
  kEmbedderDataSlotPayloadTag,
  // This tag essentially stands for a `void*` pointer in the V8 API, and it is
  // the Embedder's responsibility to ensure type safety (against substitution)
  // and lifetime validity of these objects.
  kExternalObjectValueTag,
  kFirstMaybeReadOnlyExternalPointerTag,
  kFunctionTemplateInfoCallbackTag = kFirstMaybeReadOnlyExternalPointerTag,
  kAccessorInfoGetterTag,
  kAccessorInfoSetterTag,

  // InterceptorInfo external pointers.
  kFirstInterceptorInfoExternalPointerTag,
  kApiNamedPropertyQueryCallbackTag = kFirstInterceptorInfoExternalPointerTag,
  kApiNamedPropertyGetterCallbackTag,
  kApiNamedPropertySetterCallbackTag,
  kApiNamedPropertyDescriptorCallbackTag,
  kApiNamedPropertyDefinerCallbackTag,
  kApiNamedPropertyDeleterCallbackTag,
  kApiNamedPropertyEnumeratorCallbackTag,
  kApiIndexedPropertyQueryCallbackTag,
  kApiIndexedPropertyGetterCallbackTag,
  kApiIndexedPropertySetterCallbackTag,
  kApiIndexedPropertyDescriptorCallbackTag,
  kApiIndexedPropertyDefinerCallbackTag,
  kApiIndexedPropertyDeleterCallbackTag,
  kApiIndexedPropertyEnumeratorCallbackTag,
  kLastInterceptorInfoExternalPointerTag =
      kApiIndexedPropertyEnumeratorCallbackTag,

  kLastMaybeReadOnlyExternalPointerTag = kLastInterceptorInfoExternalPointerTag,

  kWasmStackMemoryTag,

  // Foreigns
  kFirstForeignExternalPointerTag,
  kGenericForeignTag = kFirstForeignExternalPointerTag,

  kApiAccessCheckCallbackTag,
  kApiAbortScriptExecutionCallbackTag,
  kSyntheticModuleTag,
  kMicrotaskCallbackTag,
  kMicrotaskCallbackDataTag,
  kCFunctionTag,
  kCFunctionInfoTag,
  kMessageListenerTag,
  kWaiterQueueForeignTag,

  // Managed
  kFirstManagedResourceTag,
  kFirstManagedExternalPointerTag = kFirstManagedResourceTag,
  kGenericManagedTag = kFirstManagedExternalPointerTag,
  kWasmWasmStreamingTag,
  kWasmFuncDataTag,
  kWasmManagedDataTag,
  kWasmNativeModuleTag,
  kIcuBreakIteratorTag,
  kIcuUnicodeStringTag,
  kIcuListFormatterTag,
  kIcuLocaleTag,
  kIcuSimpleDateFormatTag,
  kIcuDateIntervalFormatTag,
  kIcuRelativeDateTimeFormatterTag,
  kIcuLocalizedNumberFormatterTag,
  kIcuPluralRulesTag,
  kIcuCollatorTag,
  kTemporalDurationTag,
  kTemporalInstantTag,
  kTemporalPlainDateTag,
  kTemporalPlainTimeTag,
  kTemporalPlainDateTimeTag,
  kTemporalPlainYearMonthTag,
  kTemporalPlainMonthDayTag,
  kTemporalZonedDateTimeTag,
  kDisplayNamesInternalTag,
  kD8WorkerTag,
  kD8ModuleEmbedderDataTag,
  kLastForeignExternalPointerTag = kD8ModuleEmbedderDataTag,
  kLastManagedExternalPointerTag = kLastForeignExternalPointerTag,
  // External resources whose lifetime is tied to their entry in the external
  // pointer table but which are not referenced via a Managed
  kArrayBufferExtensionTag,
  kLastManagedResourceTag = kArrayBufferExtensionTag,

  kExternalPointerZappedEntryTag = 0x7d,
  kExternalPointerEvacuationEntryTag = 0x7e,
  kExternalPointerFreeEntryTag = 0x7f,
  // The tags are limited to 7 bits, so the last tag is 0x7f.
  kLastExternalPointerTag = 0x7f,
};

using ExternalPointerTagRange = TagRange<ExternalPointerTag>;

constexpr ExternalPointerTagRange kAnyExternalPointerTagRange(
    kFirstExternalPointerTag, kLastExternalPointerTag);
constexpr ExternalPointerTagRange kAnySharedExternalPointerTagRange(
    kFirstSharedExternalPointerTag, kLastSharedExternalPointerTag);
constexpr ExternalPointerTagRange kAnyForeignExternalPointerTagRange(
    kFirstForeignExternalPointerTag, kLastForeignExternalPointerTag);
constexpr ExternalPointerTagRange kAnyInterceptorInfoExternalPointerTagRange(
    kFirstInterceptorInfoExternalPointerTag,
    kLastInterceptorInfoExternalPointerTag);
constexpr ExternalPointerTagRange kAnyManagedExternalPointerTagRange(
    kFirstManagedExternalPointerTag, kLastManagedExternalPointerTag);
constexpr ExternalPointerTagRange kAnyMaybeReadOnlyExternalPointerTagRange(
    kFirstMaybeReadOnlyExternalPointerTag,
    kLastMaybeReadOnlyExternalPointerTag);
constexpr ExternalPointerTagRange kAnyManagedResourceExternalPointerTag(
    kFirstManagedResourceTag, kLastManagedResourceTag);

// True if the external pointer must be accessed from the shared isolate's
// external pointer table.
V8_INLINE static constexpr bool IsSharedExternalPointerType(
    ExternalPointerTagRange tag_range) {
  return kAnySharedExternalPointerTagRange.Contains(tag_range);
}

// True if the external pointer may live in a read-only object, in which case
// the table entry will be in the shared read-only segment of the external
// pointer table.
V8_INLINE static constexpr bool IsMaybeReadOnlyExternalPointerType(
    ExternalPointerTagRange tag_range) {
  return kAnyMaybeReadOnlyExternalPointerTagRange.Contains(tag_range);
}

// True if the external pointer references an external object whose lifetime is
// tied to the entry in the external pointer table.
// In this case, the entry in the ExternalPointerTable always points to an
// object derived from ExternalPointerTable::ManagedResource.
V8_INLINE static constexpr bool IsManagedExternalPointerType(
    ExternalPointerTagRange tag_range) {
  return kAnyManagedResourceExternalPointerTag.Contains(tag_range);
}

// When an external poiner field can contain the null external pointer handle,
// the type checking mechanism needs to also check for null.
// TODO(saelo): this is mostly a temporary workaround to introduce range-based
// type checks. In the future, we should either (a) change the type tagging
// scheme so that null always passes or (b) (more likely) introduce dedicated
// null entries for those tags that need them (similar to other well-known
// empty value constants such as the empty fixed array).
V8_INLINE static constexpr bool ExternalPointerCanBeEmpty(
    ExternalPointerTagRange tag_range) {
  return tag_range.Contains(kArrayBufferExtensionTag) ||
         tag_range.Contains(kEmbedderDataSlotPayloadTag) ||
         kAnyInterceptorInfoExternalPointerTagRange.Contains(tag_range);
}

// Indirect Pointers.
//
// When the sandbox is enabled, indirect pointers are used to reference
// HeapObjects that live outside of the sandbox (but are still managed by V8's
// garbage collector). When object A references an object B through an indirect
// pointer, object A will contain a IndirectPointerHandle, i.e. a shifted
// 32-bit index, which identifies an entry in a pointer table (either the
// trusted pointer table for TrustedObjects, or the code pointer table if it is
// a Code object). This table entry then contains the actual pointer to object
// B. Further, object B owns this pointer table entry, and it is responsible
// for updating the "self-pointer" in the entry when it is relocated in memory.
// This way, in contrast to "normal" pointers, indirect pointers never need to
// be tracked by the GC (i.e. there is no remembered set for them).
// These pointers do not exist when the sandbox is disabled.

// An IndirectPointerHandle represents a 32-bit index into a pointer table.
using IndirectPointerHandle = uint32_t;

// A null handle always references an entry that contains nullptr.
constexpr IndirectPointerHandle kNullIndirectPointerHandle = 0;

// When the sandbox is enabled, indirect pointers are used to implement:
// - TrustedPointers: an indirect pointer using the trusted pointer table (TPT)
//   and referencing a TrustedObject in one of the trusted heap spaces.
// - CodePointers, an indirect pointer using the code pointer table (CPT) and
//   referencing a Code object together with its instruction stream.

//
// Trusted Pointers.
//
// A pointer to a TrustedObject.
// When the sandbox is enabled, these are indirect pointers using the trusted
// pointer table (TPT). They are used to reference trusted objects (located in
// one of V8's trusted heap spaces, outside of the sandbox) from inside the
// sandbox in a memory-safe way. When the sandbox is disabled, these are
// regular tagged pointers.
using TrustedPointerHandle = IndirectPointerHandle;

// The size of the virtual memory reservation for the trusted pointer table.
// As with the external pointer table, a maximum table size in combination with
// shifted indices allows omitting bounds checks.
constexpr size_t kTrustedPointerTableReservationSize = 64 * MB;

// The trusted pointer handles are stored shifted to the left by this amount
// to guarantee that they are smaller than the maximum table size.
constexpr uint32_t kTrustedPointerHandleShift = 9;

// A null handle always references an entry that contains nullptr.
constexpr TrustedPointerHandle kNullTrustedPointerHandle =
    kNullIndirectPointerHandle;

// The maximum number of entries in an trusted pointer table.
constexpr int kTrustedPointerTableEntrySize = 8;
constexpr int kTrustedPointerTableEntrySizeLog2 = 3;
constexpr size_t kMaxTrustedPointers =
    kTrustedPointerTableReservationSize / kTrustedPointerTableEntrySize;
static_assert((1 << (32 - kTrustedPointerHandleShift)) == kMaxTrustedPointers,
              "kTrustedPointerTableReservationSize and "
              "kTrustedPointerHandleShift don't match");

//
// Code Pointers.
//
// A pointer to a Code object.
// Essentially a specialized version of a trusted pointer that (when the
// sandbox is enabled) uses the code pointer table (CPT) instead of the TPT.
// Each entry in the CPT contains both a pointer to a Code object as well as a
// pointer to the Code's entrypoint. This allows calling/jumping into Code with
// one fewer memory access (compared to the case where the entrypoint pointer
// first needs to be loaded from the Code object). As such, a CodePointerHandle
// can be used both to obtain the referenced Code object and to directly load
// its entrypoint.
//
// When the sandbox is disabled, these are regular tagged pointers.
using CodePointerHandle = IndirectPointerHandle;

// The size of the virtual memory reservation for the code pointer table.
// As with the other tables, a maximum table size in combination with shifted
// indices allows omitting bounds checks.
constexpr size_t kCodePointerTableReservationSize = 128 * MB;

// Code pointer handles are shifted by a different amount than indirect pointer
// handles as the tables have a different maximum size.
constexpr uint32_t kCodePointerHandleShift = 9;

// A null handle always references an entry that contains nullptr.
constexpr CodePointerHandle kNullCodePointerHandle = kNullIndirectPointerHandle;

// It can sometimes be necessary to distinguish a code pointer handle from a
// trusted pointer handle. A typical example would be a union trusted pointer
// field that can refer to both Code objects and other trusted objects. To
// support these use-cases, we use a simple marking scheme where some of the
// low bits of a code pointer handle are set, while they will be unset on a
// trusted pointer handle. This way, the correct table to resolve the handle
// can be determined even in the absence of a type tag.
constexpr uint32_t kCodePointerHandleMarker = 0x1;
static_assert(kCodePointerHandleShift > 0);
static_assert(kTrustedPointerHandleShift > 0);

// The maximum number of entries in a code pointer table.
constexpr int kCodePointerTableEntrySize = 16;
constexpr int kCodePointerTableEntrySizeLog2 = 4;
constexpr size_t kMaxCodePointers =
    kCodePointerTableReservationSize / kCodePointerTableEntrySize;
static_assert(
    (1 << (32 - kCodePointerHandleShift)) == kMaxCodePointers,
    "kCodePointerTableReservationSize and kCodePointerHandleShift don't match");

constexpr int kCodePointerTableEntryEntrypointOffset = 0;
constexpr int kCodePointerTableEntryCodeObjectOffset = 8;

// Constants that can be used to mark places that should be modified once
// certain types of objects are moved out of the sandbox and into trusted space.
constexpr bool kRuntimeGeneratedCodeObjectsLiveInTrustedSpace = true;
constexpr bool kBuiltinCodeObjectsLiveInTrustedSpace = false;
constexpr bool kAllCodeObjectsLiveInTrustedSpace =
    kRuntimeGeneratedCodeObjectsLiveInTrustedSpace &&
    kBuiltinCodeObjectsLiveInTrustedSpace;

// {obj} must be the raw tagged pointer representation of a HeapObject
// that's guaranteed to never be in ReadOnlySpace.
V8_DEPRECATE_SOON(
    "Use GetCurrentIsolate() instead, which is guaranteed to return the same "
    "isolate since https://crrev.com/c/6458560.")
V8_EXPORT internal::Isolate* IsolateFromNeverReadOnlySpaceObject(Address obj);

// Returns if we need to throw when an error occurs. This infers the language
// mode based on the current context and the closure. This returns true if the
// language mode is strict.
V8_EXPORT bool ShouldThrowOnError(internal::Isolate* isolate);

struct HandleScopeData final {
  static constexpr uint32_t kSizeInBytes =
      2 * kApiSystemPointerSize + 2 * kApiInt32Size;

  Address* next;
  Address* limit;
  int level;
  int sealed_level;

  void Initialize() {
    next = limit = nullptr;
    sealed_level = level = 0;
  }
};

static_assert(HandleScopeData::kSizeInBytes == sizeof(HandleScopeData));

/**
 * This class exports constants and functionality from within v8 that
 * is necessary to implement inline functions in the v8 api.  Don't
 * depend on functions and constants defined here.
 */
class Internals {
#ifdef V8_MAP_PACKING
  V8_INLINE static constexpr Address UnpackMapWord(Address mapword) {
    // TODO(wenyuzhao): Clear header metadata.
    return mapword ^ kMapWordXorMask;
  }
#endif

 public:
  // These values match non-compiler-dependent values defined within
  // the implementation of v8.
  static const int kHeapObjectMapOffset = 0;
  static const int kMapInstanceTypeOffset = 1 * kApiTaggedSize + kApiInt32Size;
  static const int kStringResourceOffset =
      1 * kApiTaggedSize + 2 * kApiInt32Size;

  static const int kOddballKindOffset = 4 * kApiTaggedSize + kApiDoubleSize;
  static const int kJSObjectHeaderSize = 3 * kApiTaggedSize;
#ifdef V8_COMPRESS_POINTERS
  static const int kJSAPIObjectWithEmbedderSlotsHeaderSize =
      kJSObjectHeaderSize + kApiInt32Size;
#else   // !V8_COMPRESS_POINTERS
  static const int kJSAPIObjectWithEmbedderSlotsHeaderSize =
      kJSObjectHeaderSize + kApiTaggedSize;
#endif  // !V8_COMPRESS_POINTERS
  static const int kFixedArrayHeaderSize = 2 * kApiTaggedSize;
  static const int kEmbedderDataArrayHeaderSize = 2 * kApiTaggedSize;
  static const int kEmbedderDataSlotSize = kApiSystemPointerSize;
#ifdef V8_ENABLE_SANDBOX
  static const int kEmbedderDataSlotExternalPointerOffset = kApiTaggedSize;
#else
  static const int kEmbedderDataSlotExternalPointerOffset = 0;
#endif
  static const int kNativeContextEmbedderDataOffset = 6 * kApiTaggedSize;
  static const int kStringRepresentationAndEncodingMask = 0x0f;
  static const int kStringEncodingMask = 0x8;
  static const int kExternalTwoByteRepresentationTag = 0x02;
  static const int kExternalOneByteRepresentationTag = 0x0a;

  static const uint32_t kNumIsolateDataSlots = 4;
  static const int kStackGuardSize = 8 * kApiSystemPointerSize;
  static const int kNumberOfBooleanFlags = 6;
  static const int kErrorMessageParamSize = 1;
  static const int kTablesAlignmentPaddingSize = 1;
  static const int kRegExpStaticResultOffsetsVectorSize = kApiSystemPointerSize;
  static const int kBuiltinTier0EntryTableSize = 7 * kApiSystemPointerSize;
  static const int kBuiltinTier0TableSize = 7 * kApiSystemPointerSize;
  static const int kLinearAllocationAreaSize = 3 * kApiSystemPointerSize;
  static const int kThreadLocalTopSize = 29 * kApiSystemPointerSize;
  static const int kHandleScopeDataSize =
      2 * kApiSystemPointerSize + 2 * kApiInt32Size;

  // ExternalPointerTable and TrustedPointerTable layout guarantees.
  static const int kExternalPointerTableBasePointerOffset = 0;
  static const int kSegmentedTableSegmentPoolSize = 4;
  static const int kExternalPointerTableSize =
      4 * kApiSystemPointerSize +
      kSegmentedTableSegmentPoolSize * sizeof(uint32_t);
  static const int kTrustedPointerTableSize =
      4 * kApiSystemPointerSize +
      kSegmentedTableSegmentPoolSize * sizeof(uint32_t);
  static const int kTrustedPointerTableBasePointerOffset = 0;

  // IsolateData layout guarantees.
  static const int kIsolateCageBaseOffset = 0;
  static const int kIsolateStackGuardOffset =
      kIsolateCageBaseOffset + kApiSystemPointerSize;
  static const int kVariousBooleanFlagsOffset =
      kIsolateStackGuardOffset + kStackGuardSize;
  static const int kErrorMessageParamOffset =
      kVariousBooleanFlagsOffset + kNumberOfBooleanFlags;
  static const int kBuiltinTier0EntryTableOffset =
      kErrorMessageParamOffset + kErrorMessageParamSize +
      kTablesAlignmentPaddingSize + kRegExpStaticResultOffsetsVectorSize;
  static const int kBuiltinTier0TableOffset =
      kBuiltinTier0EntryTableOffset + kBuiltinTier0EntryTableSize;
  static const int kNewAllocationInfoOffset =
      kBuiltinTier0TableOffset + kBuiltinTier0TableSize;
  static const int kOldAllocationInfoOffset =
      kNewAllocationInfoOffset + kLinearAllocationAreaSize;

  static const int kFastCCallAlignmentPaddingSize =
      kApiSystemPointerSize == 8 ? 5 * kApiSystemPointerSize
                                 : 1 * kApiSystemPointerSize;
  static const int kIsolateFastCCallCallerPcOffset =
      kOldAllocationInfoOffset + kLinearAllocationAreaSize +
      kFastCCallAlignmentPaddingSize;
  static const int kIsolateFastCCallCallerFpOffset =
      kIsolateFastCCallCallerPcOffset + kApiSystemPointerSize;
  static const int kIsolateFastApiCallTargetOffset =
      kIsolateFastCCallCallerFpOffset + kApiSystemPointerSize;
  static const int kIsolateLongTaskStatsCounterOffset =
      kIsolateFastApiCallTargetOffset + kApiSystemPointerSize;
  static const int kIsolateThreadLocalTopOffset =
      kIsolateLongTaskStatsCounterOffset + kApiSizetSize;
  static const int kIsolateHandleScopeDataOffset =
      kIsolateThreadLocalTopOffset + kThreadLocalTopSize;
  static const int kIsolateEmbedderDataOffset =
      kIsolateHandleScopeDataOffset + kHandleScopeDataSize;
#ifdef V8_COMPRESS_POINTERS
  static const int kIsolateExternalPointerTableOffset =
      kIsolateEmbedderDataOffset + kNumIsolateDataSlots * kApiSystemPointerSize;
  static const int kIsolateSharedExternalPointerTableAddressOffset =
      kIsolateExternalPointerTableOffset + kExternalPointerTableSize;
  static const int kIsolateCppHeapPointerTableOffset =
      kIsolateSharedExternalPointerTableAddressOffset + kApiSystemPointerSize;
#ifdef V8_ENABLE_SANDBOX
  static const int kIsolateTrustedCageBaseOffset =
      kIsolateCppHeapPointerTableOffset + kExternalPointerTableSize;
  static const int kIsolateTrustedPointerTableOffset =
      kIsolateTrustedCageBaseOffset + kApiSystemPointerSize;
  static const int kIsolateSharedTrustedPointerTableAddressOffset =
      kIsolateTrustedPointerTableOffset + kTrustedPointerTableSize;
  static const int kIsolateTrustedPointerPublishingScopeOffset =
      kIsolateSharedTrustedPointerTableAddressOffset + kApiSystemPointerSize;
  static const int kIsolateCodePointerTableBaseAddressOffset =
      kIsolateTrustedPointerPublishingScopeOffset + kApiSystemPointerSize;
  static const int kIsolateApiCallbackThunkArgumentOffset =
      kIsolateCodePointerTableBaseAddressOffset + kApiSystemPointerSize;
#else
  static const int kIsolateApiCallbackThunkArgumentOffset =
      kIsolateCppHeapPointerTableOffset + kExternalPointerTableSize;
#endif  // V8_ENABLE_SANDBOX
#else
  static const int kIsolateApiCallbackThunkArgumentOffset =
      kIsolateEmbedderDataOffset + kNumIsolateDataSlots * kApiSystemPointerSize;
#endif  // V8_COMPRESS_POINTERS
  static const int kJSDispatchTableOffset =
      kIsolateApiCallbackThunkArgumentOffset + kApiSystemPointerSize;
  static const int kIsolateRegexpExecVectorArgumentOffset =
      kJSDispatchTableOffset + kApiSystemPointerSize;
  static const int kContinuationPreservedEmbedderDataOffset =
      kIsolateRegexpExecVectorArgumentOffset + kApiSystemPointerSize;
  static const int kIsolateRootsOffset =
      kContinuationPreservedEmbedderDataOffset + kApiSystemPointerSize;

  // Assert scopes
  static const int kDisallowGarbageCollectionAlign = alignof(uint32_t);
  static const int kDisallowGarbageCollectionSize = sizeof(uint32_t);

#if V8_STATIC_ROOTS_BOOL

// These constants are copied from static-roots.h and guarded by static asserts.
#define EXPORTED_STATIC_ROOTS_PTR_LIST(V) \
  V(UndefinedValue, 0x11)                 \
  V(NullValue, 0x2d)                      \
  V(TrueValue, 0x71)                      \
  V(FalseValue, 0x55)                     \
  V(EmptyString, 0x49)                    \
  V(TheHoleValue, 0x7d9)

  using Tagged_t = uint32_t;
  struct StaticReadOnlyRoot {
#define DEF_ROOT(name, value) static constexpr Tagged_t k##name = value;
    EXPORTED_STATIC_ROOTS_PTR_LIST(DEF_ROOT)
#undef DEF_ROOT

    // Use 0 for kStringMapLowerBound since string maps are the first maps.
    static constexpr Tagged_t kStringMapLowerBound = 0;
    static constexpr Tagged_t kStringMapUpperBound = 0x425;

#define PLUSONE(...) +1
    static constexpr size_t kNumberOfExportedStaticRoots =
        2 + EXPORTED_STATIC_ROOTS_PTR_LIST(PLUSONE);
#undef PLUSONE
  };

#endif  // V8_STATIC_ROOTS_BOOL

  static const int kUndefinedValueRootIndex = 0;
  static const int kTheHoleValueRootIndex = 1;
  static const int kNullValueRootIndex = 2;
  static const int kTrueValueRootIndex = 3;
  static const int kFalseValueRootIndex = 4;
  static const int kEmptyStringRootIndex = 5;

  static const int kNodeClassIdOffset = 1 * kApiSystemPointerSize;
  static const int kNodeFlagsOffset = 1 * kApiSystemPointerSize + 3;
  static const int kNodeStateMask = 0x3;
  static const int kNodeStateIsWeakValue = 2;

  static const int kFirstNonstringType = 0x80;
  static const int kOddballType = 0x83;
  static const int kForeignType = 0xcc;
  static const int kJSSpecialApiObjectType = 0x410;
  static const int kJSObjectType = 0x421;
  static const int kFirstJSApiObjectType = 0x422;
  static const int kLastJSApiObjectType = 0x80A;
  // Defines a range [kFirstEmbedderJSApiObjectType, kJSApiObjectTypesCount]
  // of JSApiObject instance type values that an embedder can use.
  static const int kFirstEmbedderJSApiObjectType = 0;
  static const int kLastEmbedderJSApiObjectType =
      kLastJSApiObjectType - kFirstJSApiObjectType;

  static const int kUndefinedOddballKind = 4;
  static const int kNullOddballKind = 3;

  // Constants used by PropertyCallbackInfo to check if we should throw when an
  // error occurs.
  static const int kDontThrow = 0;
  static const int kThrowOnError = 1;
  static const int kInferShouldThrowMode = 2;

  // Soft limit for AdjustAmountofExternalAllocatedMemory. Trigger an
  // incremental GC once the external memory reaches this limit.
  static constexpr size_t kExternalAllocationSoftLimit = 64 * 1024 * 1024;

#ifdef V8_MAP_PACKING
  static const uintptr_t kMapWordMetadataMask = 0xffffULL << 48;
  // The lowest two bits of mapwords are always `0b10`
  static const uintptr_t kMapWordSignature = 0b10;
  // XORing a (non-compressed) map with this mask ensures that the two
  // low-order bits are 0b10. The 0 at the end makes this look like a Smi,
  // although real Smis have all lower 32 bits unset. We only rely on these
  // values passing as Smis in very few places.
  static const int kMapWordXorMask = 0b11;
#endif

  V8_EXPORT static void CheckInitializedImpl(v8::Isolate* isolate);
  V8_INLINE static void CheckInitialized(v8::Isolate* isolate) {
#ifdef V8_ENABLE_CHECKS
    CheckInitializedImpl(isolate);
#endif
  }

  V8_INLINE static constexpr bool HasHeapObjectTag(Address value) {
    return (value & kHeapObjectTagMask) == static_cast<Address>(kHeapObjectTag);
  }

  V8_INLINE static constexpr int SmiValue(Address value) {
    return PlatformSmiTagging::SmiToInt(value);
  }

  V8_INLINE static constexpr Address AddressToSmi(Address value) {
    return (value << (kSmiTagSize + PlatformSmiTagging::kSmiShiftSize)) |
           kSmiTag;
  }

  V8_INLINE static constexpr Address IntToSmi(int value) {
    return AddressToSmi(static_cast<Address>(value));
  }

  template <typename T,
            typename std::enable_if_t<std::is_integral_v<T>>* = nullptr>
  V8_INLINE static constexpr Address IntegralToSmi(T value) {
    return AddressToSmi(static_cast<Address>(value));
  }

  template <typename T,
            typename std::enable_if_t<std::is_integral_v<T>>* = nullptr>
  V8_INLINE static constexpr bool IsValidSmi(T value) {
    return PlatformSmiTagging::IsValidSmi(value);
  }

  template <typename T,
            typename std::enable_if_t<std::is_integral_v<T>>* = nullptr>
  static constexpr std::optional<Address> TryIntegralToSmi(T value) {
    if (V8_LIKELY(PlatformSmiTagging::IsValidSmi(value))) {
      return {AddressToSmi(static_cast<Address>(value))};
    }
    return {};
  }

#if V8_STATIC_ROOTS_BOOL
  V8_INLINE static bool is_identical(Address obj, Tagged_t constant) {
    return static_cast<Tagged_t>(obj) == constant;
  }

  V8_INLINE static bool CheckInstanceMapRange(Address obj, Tagged_t first_map,
                                              Tagged_t last_map) {
    auto map = ReadRawField<Tagged_t>(obj, kHeapObjectMapOffset);
#ifdef V8_MAP_PACKING
    map = UnpackMapWord(map);
#endif
    return map >= first_map && map <= last_map;
  }
#endif

  V8_INLINE static int GetInstanceType(Address obj) {
    Address map = ReadTaggedPointerField(obj, kHeapObjectMapOffset);
#ifdef V8_MAP_PACKING
    map = UnpackMapWord(map);
#endif
    return ReadRawField<uint16_t>(map, kMapInstanceTypeOffset);
  }

  V8_INLINE static Address LoadMap(Address obj) {
    if (!HasHeapObjectTag(obj)) return kNullAddress;
    Address map = ReadTaggedPointerField(obj, kHeapObjectMapOffset);
#ifdef V8_MAP_PACKING
    map = UnpackMapWord(map);
#endif
    return map;
  }

  V8_INLINE static int GetOddballKind(Address obj) {
    return SmiValue(ReadTaggedSignedField(obj, kOddballKindOffset));
  }

  V8_INLINE static bool IsExternalTwoByteString(int instance_type) {
    int representation = (instance_type & kStringRepresentationAndEncodingMask);
    return representation == kExternalTwoByteRepresentationTag;
  }

  V8_INLINE static constexpr bool CanHaveInternalField(int instance_type) {
    static_assert(kJSObjectType + 1 == kFirstJSApiObjectType);
    static_assert(kJSObjectType < kLastJSApiObjectType);
    static_assert(kFirstJSApiObjectType < kLastJSApiObjectType);
    // Check for IsJSObject() || IsJSSpecialApiObject() || IsJSApiObject()
    return instance_type == kJSSpecialApiObjectType ||
           // inlined version of base::IsInRange
           (static_cast<unsigned>(static_cast<unsigned>(instance_type) -
                                  static_cast<unsigned>(kJSObjectType)) <=
            static_cast<unsigned>(kLastJSApiObjectType - kJSObjectType));
  }

  V8_INLINE static uint8_t GetNodeFlag(Address* obj, int shift) {
    uint8_t* addr = reinterpret_cast<uint8_t*>(obj) + kNodeFlagsOffset;
    return *addr & static_cast<uint8_t>(1U << shift);
  }

  V8_INLINE static void UpdateNodeFlag(Address* obj, bool value, int shift) {
    uint8_t* addr = reinterpret_cast<uint8_t*>(obj) + kNodeFlagsOffset;
    uint8_t mask = static_cast<uint8_t>(1U << shift);
    *addr = static_cast<uint8_t>((*addr & ~mask) | (value << shift));
  }

  V8_INLINE static uint8_t GetNodeState(Address* obj) {
    uint8_t* addr = reinterpret_cast<uint8_t*>(obj) + kNodeFlagsOffset;
    return *addr & kNodeStateMask;
  }

  V8_INLINE static void UpdateNodeState(Address* obj, uint8_t value) {
    uint8_t* addr = reinterpret_cast<uint8_t*>(obj) + kNodeFlagsOffset;
    *addr = static_cast<uint8_t>((*addr & ~kNodeStateMask) | value);
  }

  V8_INLINE static void SetEmbedderData(v8::Isolate* isolate, uint32_t slot,
                                        void* data) {
    Address addr = reinterpret_cast<Address>(isolate) +
                   kIsolateEmbedderDataOffset + slot * kApiSystemPointerSize;
    *reinterpret_cast<void**>(addr) = data;
  }

  V8_INLINE static void* GetEmbedderData(const v8::Isolate* isolate,
                                         uint32_t slot) {
    Address addr = reinterpret_cast<Address>(isolate) +
                   kIsolateEmbedderDataOffset + slot * kApiSystemPointerSize;
    return *reinterpret_cast<void* const*>(addr);
  }

  V8_INLINE static HandleScopeData* GetHandleScopeData(v8::Isolate* isolate) {
    Address addr =
        reinterpret_cast<Address>(isolate) + kIsolateHandleScopeDataOffset;
    return reinterpret_cast<HandleScopeData*>(addr);
  }

  V8_INLINE static void IncrementLongTasksStatsCounter(v8::Isolate* isolate) {
    Address addr =
        reinterpret_cast<Address>(isolate) + kIsolateLongTaskStatsCounterOffset;
    ++(*reinterpret_cast<size_t*>(addr));
  }

  V8_INLINE static Address* GetRootSlot(v8::Isolate* isolate, int index) {
    Address addr = reinterpret_cast<Address>(isolate) + kIsolateRootsOffset +
                   index * kApiSystemPointerSize;
    return reinterpret_cast<Address*>(addr);
  }

  V8_INLINE static Address GetRoot(v8::Isolate* isolate, int index) {
#if V8_STATIC_ROOTS_BOOL
    Address base = *reinterpret_cast<Address*>(
        reinterpret_cast<uintptr_t>(isolate) + kIsolateCageBaseOffset);
    switch (index) {
#define DECOMPRESS_ROOT(name, ...) \
  case k##name##RootIndex:         \
    return base + StaticReadOnlyRoot::k##name;
      EXPORTED_STATIC_ROOTS_PTR_LIST(DECOMPRESS_ROOT)
#undef DECOMPRESS_ROOT
#undef EXPORTED_STATIC_ROOTS_PTR_LIST
      default:
        break;
    }
#endif  // V8_STATIC_ROOTS_BOOL
    return *GetRootSlot(isolate, index);
  }

#ifdef V8_ENABLE_SANDBOX
  V8_INLINE static Address* GetExternalPointerTableBase(v8::Isolate* isolate) {
    Address addr = reinterpret_cast<Address>(isolate) +
                   kIsolateExternalPointerTableOffset +
                   kExternalPointerTableBasePointerOffset;
    return *reinterpret_cast<Address**>(addr);
  }

  V8_INLINE static Address* GetSharedExternalPointerTableBase(
      v8::Isolate* isolate) {
    Address addr = reinterpret_cast<Address>(isolate) +
                   kIsolateSharedExternalPointerTableAddressOffset;
    addr = *reinterpret_cast<Address*>(addr);
    addr += kExternalPointerTableBasePointerOffset;
    return *reinterpret_cast<Address**>(addr);
  }
#endif

  template <typename T>
  V8_INLINE static T ReadRawField(Address heap_object_ptr, int offset) {
    Address addr = heap_object_ptr + offset - kHeapObjectTag;
#ifdef V8_COMPRESS_POINTERS
    if constexpr (sizeof(T) > kApiTaggedSize) {
      // TODO(ishell, v8:8875): When pointer compression is enabled 8-byte size
      // fields (external pointers, doubles and BigInt data) are only
      // kTaggedSize aligned so we have to use unaligned pointer friendly way of
      // accessing them in order to avoid undefined behavior in C++ code.
      T r;
      memcpy(&r, reinterpret_cast<void*>(addr), sizeof(T));
      return r;
    }
#endif
    return *reinterpret_cast<const T*>(addr);
  }

  V8_INLINE static Address ReadTaggedPointerField(Address heap_object_ptr,
                                                  int offset) {
#ifdef V8_COMPRESS_POINTERS
    uint32_t value = ReadRawField<uint32_t>(heap_object_ptr, offset);
    Address base = GetPtrComprCageBaseFromOnHeapAddress(heap_object_ptr);
    return base + static_cast<Address>(static_cast<uintptr_t>(value));
#else
    return ReadRawField<Address>(heap_object_ptr, offset);
#endif
  }

  V8_INLINE static Address ReadTaggedSignedField(Address heap_object_ptr,
                                                 int offset) {
#ifdef V8_COMPRESS_POINTERS
    uint32_t value = ReadRawField<uint32_t>(heap_object_ptr, offset);
    return static_cast<Address>(static_cast<uintptr_t>(value));
#else
    return ReadRawField<Address>(heap_object_ptr, offset);
#endif
  }

  V8_DEPRECATE_SOON(
      "Use GetCurrentIsolateForSandbox() instead, which is guaranteed to "
      "return the same isolate since https://crrev.com/c/6458560.")
  V8_INLINE static v8::Isolate* GetIsolateForSandbox(Address obj) {
#ifdef V8_ENABLE_SANDBOX
    return GetCurrentIsolate();
#else
    // Not used in non-sandbox mode.
    return nullptr;
#endif
  }

  // Returns v8::Isolate::Current(), but without needing to include the
  // v8-isolate.h header.
  V8_EXPORT static v8::Isolate* GetCurrentIsolate();

  V8_INLINE static v8::Isolate* GetCurrentIsolateForSandbox() {
#ifdef V8_ENABLE_SANDBOX
    return GetCurrentIsolate();
#else
    // Not used in non-sandbox mode.
    return nullptr;
#endif
  }

  template <ExternalPointerTagRange tag_range>
  V8_INLINE static Address ReadExternalPointerField(v8::Isolate* isolate,
                                                    Address heap_object_ptr,
                                                    int offset) {
#ifdef V8_ENABLE_SANDBOX
    static_assert(!tag_range.IsEmpty());
    // See src/sandbox/external-pointer-table.h. Logic duplicated here so
    // it can be inlined and doesn't require an additional call.
    Address* table = IsSharedExternalPointerType(tag_range)
                         ? GetSharedExternalPointerTableBase(isolate)
                         : GetExternalPointerTableBase(isolate);
    internal::ExternalPointerHandle handle =
        ReadRawField<ExternalPointerHandle>(heap_object_ptr, offset);
    uint32_t index = handle >> kExternalPointerIndexShift;
    std::atomic<Address>* ptr =
        reinterpret_cast<std::atomic<Address>*>(&table[index]);
    Address entry = std::atomic_load_explicit(ptr, std::memory_order_relaxed);
    ExternalPointerTag actual_tag = static_cast<ExternalPointerTag>(
        (entry & kExternalPointerTagMask) >> kExternalPointerTagShift);
    if (V8_LIKELY(tag_range.Contains(actual_tag))) {
      return entry & kExternalPointerPayloadMask;
    } else {
      return 0;
    }
    return entry;
#else
    return ReadRawField<Address>(heap_object_ptr, offset);
#endif  // V8_ENABLE_SANDBOX
  }

#ifdef V8_COMPRESS_POINTERS
  V8_INLINE static Address GetPtrComprCageBaseFromOnHeapAddress(Address addr) {
    return addr & -static_cast<intptr_t>(kPtrComprCageBaseAlignment);
  }

  V8_INLINE static uint32_t CompressTagged(Address value) {
    return static_cast<uint32_t>(value);
  }

  V8_INLINE static Address DecompressTaggedField(Address heap_object_ptr,
                                                 uint32_t value) {
    Address base = GetPtrComprCageBaseFromOnHeapAddress(heap_object_ptr);
    return base + static_cast<Address>(static_cast<uintptr_t>(value));
  }

#endif  // V8_COMPRESS_POINTERS
};

// Only perform cast check for types derived from v8::Data since
// other types do not implement the Cast method.
template <bool PerformCheck>
struct CastCheck {
  template <class T>
  static void Perform(T* data);
};

template <>
template <class T>
void CastCheck<true>::Perform(T* data) {
  T::Cast(data);
}

template <>
template <class T>
void CastCheck<false>::Perform(T* data) {}

template <class T>
V8_INLINE void PerformCastCheck(T* data) {
  CastCheck<std::is_base_of_v<Data, T> &&
            !std::is_same_v<Data, std::remove_cv_t<T>>>::Perform(data);
}

// A base class for backing stores, which is needed due to vagaries of
// how static casts work with std::shared_ptr.
class BackingStoreBase {};

// The maximum value in enum GarbageCollectionReason, defined in heap.h.
// This is needed for histograms sampling garbage collection reasons.
constexpr int kGarbageCollectionReasonMaxValue = 30;

// Base class for the address block allocator compatible with standard
// containers, which registers its allocated range as strong roots.
class V8_EXPORT StrongRootAllocatorBase {
 public:
  Heap* heap() const { return heap_; }

  friend bool operator==(const StrongRootAllocatorBase& a,
                         const StrongRootAllocatorBase& b) {
    // TODO(pkasting): Replace this body with `= default` after dropping support
    // for old gcc versions.
    return a.heap_ == b.heap_;
  }

 protected:
  explicit StrongRootAllocatorBase(Heap* heap) : heap_(heap) {}
  explicit StrongRootAllocatorBase(LocalHeap* heap);
  explicit StrongRootAllocatorBase(Isolate* isolate);
  explicit StrongRootAllocatorBase(v8::Isolate* isolate);
  explicit StrongRootAllocatorBase(LocalIsolate* isolate);

  // Allocate/deallocate a range of n elements of type internal::Address.
  Address* allocate_impl(size_t n);
  void deallocate_impl(Address* p, size_t n) noexcept;

 private:
  Heap* heap_;
};

// The general version of this template behaves just as std::allocator, with
// the exception that the constructor takes the isolate as parameter. Only
// specialized versions, e.g., internal::StrongRootAllocator<internal::Address>
// and internal::StrongRootAllocator<v8::Local<T>> register the allocated range
// as strong roots.
template <typename T>
class StrongRootAllocator : private std::allocator<T> {
 public:
  using value_type = T;

  template <typename HeapOrIsolateT>
  explicit StrongRootAllocator(HeapOrIsolateT*) {}
  template <typename U>
  StrongRootAllocator(const StrongRootAllocator<U>& other) noexcept {}

  using std::allocator<T>::allocate;
  using std::allocator<T>::deallocate;
};

// TODO(pkasting): Replace with `requires` clauses after dropping support for
// old gcc versions.
template <typename Iterator, typename = void>
inline constexpr bool kHaveIteratorConcept = false;
template <typename Iterator>
inline constexpr bool kHaveIteratorConcept<
    Iterator, std::void_t<typename Iterator::iterator_concept>> = true;

template <typename Iterator, typename = void>
inline constexpr bool kHaveIteratorCategory = false;
template <typename Iterator>
inline constexpr bool kHaveIteratorCategory<
    Iterator, std::void_t<typename Iterator::iterator_category>> = true;

// Helper struct that contains an `iterator_concept` type alias only when either
// `Iterator` or `std::iterator_traits<Iterator>` do.
// Default: no alias.
template <typename Iterator, typename = void>
struct MaybeDefineIteratorConcept {};
// Use `Iterator::iterator_concept` if available.
template <typename Iterator>
struct MaybeDefineIteratorConcept<
    Iterator, std::enable_if_t<kHaveIteratorConcept<Iterator>>> {
  using iterator_concept = typename Iterator::iterator_concept;
};
// Otherwise fall back to `std::iterator_traits<Iterator>` if possible.
template <typename Iterator>
struct MaybeDefineIteratorConcept<
    Iterator, std::enable_if_t<kHaveIteratorCategory<Iterator> &&
                               !kHaveIteratorConcept<Iterator>>> {
  // There seems to be no feature-test macro covering this, so use the
  // presence of `<ranges>` as a crude proxy, since it was added to the
  // standard as part of the Ranges papers.
  // TODO(pkasting): Add this unconditionally after dropping support for old
  // libstdc++ versions.
#if __has_include(<ranges>)
  using iterator_concept =
      typename std::iterator_traits<Iterator>::iterator_concept;
#endif
};

// A class of iterators that wrap some different iterator type.
// If specified, ElementType is the type of element accessed by the wrapper
// iterator; in this case, the actual reference and pointer types of Iterator
// must be convertible to ElementType& and ElementType*, respectively.
template <typename Iterator, typename ElementType = void>
class WrappedIterator : public MaybeDefineIteratorConcept<Iterator> {
 public:
  static_assert(
      std::is_void_v<ElementType> ||
      (std::is_convertible_v<typename std::iterator_traits<Iterator>::pointer,
                             std::add_pointer_t<ElementType>> &&
       std::is_convertible_v<typename std::iterator_traits<Iterator>::reference,
                             std::add_lvalue_reference_t<ElementType>>));

  using difference_type =
      typename std::iterator_traits<Iterator>::difference_type;
  using value_type =
      std::conditional_t<std::is_void_v<ElementType>,
                         typename std::iterator_traits<Iterator>::value_type,
                         ElementType>;
  using pointer =
      std::conditional_t<std::is_void_v<ElementType>,
                         typename std::iterator_traits<Iterator>::pointer,
                         std::add_pointer_t<ElementType>>;
  using reference =
      std::conditional_t<std::is_void_v<ElementType>,
                         typename std::iterator_traits<Iterator>::reference,
                         std::add_lvalue_reference_t<ElementType>>;
  using iterator_category =
      typename std::iterator_traits<Iterator>::iterator_category;

  constexpr WrappedIterator() noexcept = default;
  constexpr explicit WrappedIterator(Iterator it) noexcept : it_(it) {}

  // TODO(pkasting): Switch to `requires` and concepts after dropping support
  // for old gcc and libstdc++ versions.
  template <typename OtherIterator, typename OtherElementType,
            typename = std::enable_if_t<
                std::is_convertible_v<OtherIterator, Iterator>>>
  constexpr WrappedIterator(
      const WrappedIterator<OtherIterator, OtherElementType>& other) noexcept
      : it_(other.base()) {}

  [[nodiscard]] constexpr reference operator*() const noexcept { return *it_; }
  [[nodiscard]] constexpr pointer operator->() const noexcept {
    if constexpr (std::is_pointer_v<Iterator>) {
      return it_;
    } else {
      return it_.operator->();
    }
  }

  template <typename OtherIterator, typename OtherElementType>
  [[nodiscard]] constexpr bool operator==(
      const WrappedIterator<OtherIterator, OtherElementType>& other)
      const noexcept {
    return it_ == other.base();
  }
#if V8_HAVE_SPACESHIP_OPERATOR
  template <typename OtherIterator, typename OtherElementType>
  [[nodiscard]] constexpr auto operator<=>(
      const WrappedIterator<OtherIterator, OtherElementType>& other)
      const noexcept {
    if constexpr (std::three_way_comparable_with<Iterator, OtherIterator>) {
      return it_ <=> other.base();
    } else if constexpr (std::totally_ordered_with<Iterator, OtherIterator>) {
      if (it_ < other.base()) {
        return std::strong_ordering::less;
      }
      return (it_ > other.base()) ? std::strong_ordering::greater
                                  : std::strong_ordering::equal;
    } else {
      if (it_ < other.base()) {
        return std::partial_ordering::less;
      }
      if (other.base() < it_) {
        return std::partial_ordering::greater;
      }
      return (it_ == other.base()) ? std::partial_ordering::equivalent
                                   : std::partial_ordering::unordered;
    }
  }
#else
  // Assume that if spaceship isn't present, operator rewriting might not be
  // either.
  template <typename OtherIterator, typename OtherElementType>
  [[nodiscard]] constexpr bool operator!=(
      const WrappedIterator<OtherIterator, OtherElementType>& other)
      const noexcept {
    return it_ != other.base();
  }

  template <typename OtherIterator, typename OtherElementType>
  [[nodiscard]] constexpr bool operator<(
      const WrappedIterator<OtherIterator, OtherElementType>& other)
      const noexcept {
    return it_ < other.base();
  }
  template <typename OtherIterator, typename OtherElementType>
  [[nodiscard]] constexpr bool operator<=(
      const WrappedIterator<OtherIterator, OtherElementType>& other)
      const noexcept {
    return it_ <= other.base();
  }
  template <typename OtherIterator, typename OtherElementType>
  [[nodiscard]] constexpr bool operator>(
      const WrappedIterator<OtherIterator, OtherElementType>& other)
      const noexcept {
    return it_ > other.base();
  }
  template <typename OtherIterator, typename OtherElementType>
  [[nodiscard]] constexpr bool operator>=(
      const WrappedIterator<OtherIterator, OtherElementType>& other)
      const noexcept {
    return it_ >= other.base();
  }
#endif

  constexpr WrappedIterator& operator++() noexcept {
    ++it_;
    return *this;
  }
  constexpr WrappedIterator operator++(int) noexcept {
    WrappedIterator result(*this);
    ++(*this);
    return result;
  }

  constexpr WrappedIterator& operator--() noexcept {
    --it_;
    return *this;
  }
  constexpr WrappedIterator operator--(int) noexcept {
    WrappedIterator result(*this);
    --(*this);
    return result;
  }
  [[nodiscard]] constexpr WrappedIterator operator+(
      difference_type n) const noexcept {
    WrappedIterator result(*this);
    result += n;
    return result;
  }
  [[nodiscard]] friend constexpr WrappedIterator operator+(
      difference_type n, const WrappedIterator& x) noexcept {
    return x + n;
  }
  constexpr WrappedIterator& operator+=(difference_type n) noexcept {
    it_ += n;
    return *this;
  }
  [[nodiscard]] constexpr WrappedIterator operator-(
      difference_type n) const noexcept {
    return *this + -n;
  }
  constexpr WrappedIterator& operator-=(difference_type n) noexcept {
    return *this += -n;
  }
  template <typename OtherIterator, typename OtherElementType>
  [[nodiscard]] constexpr auto operator-(
      const WrappedIterator<OtherIterator, OtherElementType>& other)
      const noexcept {
    return it_ - other.base();
  }
  [[nodiscard]] constexpr reference operator[](
      difference_type n) const noexcept {
    return it_[n];
  }

  [[nodiscard]] constexpr const Iterator& base() const noexcept { return it_; }

 private:
  Iterator it_;
};

// Helper functions about values contained in handles.
// A value is either an indirect pointer or a direct pointer, depending on
// whether direct local support is enabled.
class ValueHelper final {
 public:
  // ValueHelper::InternalRepresentationType is an abstract type that
  // corresponds to the internal representation of v8::Local and essentially
  // to what T* really is (these two are always in sync). This type is used in
  // methods like GetDataFromSnapshotOnce that need access to a handle's
  // internal representation. In particular, if `x` is a `v8::Local<T>`, then
  // `v8::Local<T>::FromRepr(x.repr())` gives exactly the same handle as `x`.
#ifdef V8_ENABLE_DIRECT_HANDLE
  static constexpr Address kTaggedNullAddress = 1;

  using InternalRepresentationType = internal::Address;
  static constexpr InternalRepresentationType kEmpty = kTaggedNullAddress;
#else
  using InternalRepresentationType = internal::Address*;
  static constexpr InternalRepresentationType kEmpty = nullptr;
#endif  // V8_ENABLE_DIRECT_HANDLE

  template <typename T>
  V8_INLINE static bool IsEmpty(T* value) {
    return ValueAsRepr(value) == kEmpty;
  }

  // Returns a handle's "value" for all kinds of abstract handles. For Local,
  // it is equivalent to `*handle`. The variadic parameters support handle
  // types with extra type parameters, like `Persistent<T, M>`.
  template <template <typename T, typename... Ms> typename H, typename T,
            typename... Ms>
  V8_INLINE static T* HandleAsValue(const H<T, Ms...>& handle) {
    return handle.template value<T>();
  }

#ifdef V8_ENABLE_DIRECT_HANDLE

  template <typename T>
  V8_INLINE static Address ValueAsAddress(const T* value) {
    return reinterpret_cast<Address>(value);
  }

  template <typename T, bool check_null = true, typename S>
  V8_INLINE static T* SlotAsValue(S* slot) {
    if (check_null && slot == nullptr) {
      return reinterpret_cast<T*>(kTaggedNullAddress);
    }
    return *reinterpret_cast<T**>(slot);
  }

  template <typename T>
  V8_INLINE static InternalRepresentationType ValueAsRepr(const T* value) {
    return reinterpret_cast<InternalRepresentationType>(value);
  }

  template <typename T>
  V8_INLINE static T* ReprAsValue(InternalRepresentationType repr) {
    return reinterpret_cast<T*>(repr);
  }

#else  // !V8_ENABLE_DIRECT_HANDLE

  template <typename T>
  V8_INLINE static Address ValueAsAddress(const T* value) {
    return *reinterpret_cast<const Address*>(value);
  }

  template <typename T, bool check_null = true, typename S>
  V8_INLINE static T* SlotAsValue(S* slot) {
    return reinterpret_cast<T*>(slot);
  }

  template <typename T>
  V8_INLINE static InternalRepresentationType ValueAsRepr(const T* value) {
    return const_cast<InternalRepresentationType>(
        reinterpret_cast<const Address*>(value));
  }

  template <typename T>
  V8_INLINE static T* ReprAsValue(InternalRepresentationType repr) {
    return reinterpret_cast<T*>(repr);
  }

#endif  // V8_ENABLE_DIRECT_HANDLE
};

/**
 * Helper functions about handles.
 */
class HandleHelper final {
 public:
  /**
   * Checks whether two handles are equal.
   * They are equal iff they are both empty or they are both non-empty and the
   * objects to which they refer are physically equal.
   *
   * If both handles refer to JS objects, this is the same as strict equality.
   * For primitives, such as numbers or strings, a `false` return value does not
   * indicate that the values aren't equal in the JavaScript sense.
   * Use `Value::StrictEquals()` to check primitives for equality.
   */
  template <typename T1, typename T2>
  V8_INLINE static bool EqualHandles(const T1& lhs, const T2& rhs) {
    if (lhs.IsEmpty()) return rhs.IsEmpty();
    if (rhs.IsEmpty()) return false;
    return lhs.ptr() == rhs.ptr();
  }
};

V8_EXPORT void VerifyHandleIsNonEmpty(bool is_empty);

// These functions are here just to match friend declarations in
// XxxCallbackInfo classes allowing these functions to access the internals
// of the info objects. These functions are supposed to be called by debugger
// macros.
void PrintFunctionCallbackInfo(void* function_callback_info);
void PrintPropertyCallbackInfo(void* property_callback_info);

}  // namespace internal
}  // namespace v8

#endif  // INCLUDE_V8_INTERNAL_H_
