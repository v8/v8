// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heap-constants.h"

#include <optional>

#include "src/common/globals.h"
#include "src/heap/memory-chunk.h"

namespace d = v8::debug_helper;

namespace v8 {
namespace internal {
namespace debug_helper_internal {

namespace {

std::optional<uintptr_t> OffsetInKnownReadOnlySpace(
    uintptr_t address, const d::HeapAddresses& heap_addresses) {
  if (heap_addresses.read_only_space_first_page == 0) return std::nullopt;
#if CONTIGUOUS_COMPRESSED_READ_ONLY_SPACE_BOOL
  if (address >= heap_addresses.read_only_space_first_page &&
      address - heap_addresses.read_only_space_first_page <
          kContiguousReadOnlyReservationSize) {
    return address - heap_addresses.read_only_space_first_page;
  }
#else
  if (MemoryChunk::BaseAddress(address) ==
      heap_addresses.read_only_space_first_page) {
    return MemoryChunk::AddressToOffset(address);
  }
#endif
  return std::nullopt;
}

// Note: kContiguousReadOnlySpaceMask only inspects bits
// [log2(kContiguousReadOnlyReservationSize)..31], so it works identically on
// uncompressed 64-bit addresses and zero-extended 32-bit compressed pointers
// (relying on the red zone at offset 0 of non-RO cages).
std::optional<uintptr_t> OffsetInUnknownReadOnlySpace(
    uintptr_t address, const d::HeapAddresses& heap_addresses) {
  if (heap_addresses.read_only_space_first_page != 0) return std::nullopt;
#if CONTIGUOUS_COMPRESSED_READ_ONLY_SPACE_BOOL
  if ((address & kContiguousReadOnlySpaceMask) == 0) {
    return address & (kContiguousReadOnlyReservationSize - 1);
  }
  return std::nullopt;
#else
  return MemoryChunk::AddressToOffset(address);
#endif
}

bool MayBeInMutableSpace(uintptr_t address) {
#if CONTIGUOUS_COMPRESSED_READ_ONLY_SPACE_BOOL
  return (address & kContiguousReadOnlySpaceMask) != 0;
#else
  return true;
#endif
}

}  // namespace

std::string FindKnownObject(uintptr_t address,
                            const d::HeapAddresses& heap_addresses) {
  uintptr_t containing_page = MemoryChunk::BaseAddress(address);
  uintptr_t offset_in_page = MemoryChunk::AddressToOffset(address);

  // If there's a match with a known space/page, search only that space.
  if (heap_addresses.map_space_first_page != 0 &&
      containing_page == heap_addresses.map_space_first_page) {
    return FindKnownObjectInMapSpace(offset_in_page);
  }
  if (heap_addresses.old_space_first_page != 0 &&
      containing_page == heap_addresses.old_space_first_page) {
    return FindKnownObjectInOldSpace(offset_in_page);
  }
  if (std::optional<uintptr_t> ro_offset =
          OffsetInKnownReadOnlySpace(address, heap_addresses)) {
    return FindKnownObjectInReadOnlySpace(*ro_offset);
  }

  // For any unknown pages, compile a list of things this object might be.
  std::string result;
  if (MayBeInMutableSpace(address)) {
    if (heap_addresses.map_space_first_page == 0) {
      std::string sub_result = FindKnownObjectInMapSpace(offset_in_page);
      if (!sub_result.empty()) {
        result += "maybe " + sub_result;
      }
    }
    if (heap_addresses.old_space_first_page == 0) {
      std::string sub_result = FindKnownObjectInOldSpace(offset_in_page);
      if (!sub_result.empty()) {
        result = (result.empty() ? "" : result + ", ") + "maybe " + sub_result;
      }
    }
  }
  if (std::optional<uintptr_t> ro_offset =
          OffsetInUnknownReadOnlySpace(address, heap_addresses)) {
    std::string sub_result = FindKnownObjectInReadOnlySpace(*ro_offset);
    if (!sub_result.empty()) {
      result = (result.empty() ? "" : result + ", ") + "maybe " + sub_result;
    }
  }

  return result;
}

KnownInstanceType FindKnownMapInstanceTypes(
    uintptr_t address, const d::HeapAddresses& heap_addresses) {
  uintptr_t containing_page = MemoryChunk::BaseAddress(address);
  uintptr_t offset_in_page = MemoryChunk::AddressToOffset(address);

  // If there's a match with a known space/page, search only that space.
  if (heap_addresses.map_space_first_page != 0 &&
      containing_page == heap_addresses.map_space_first_page) {
    return KnownInstanceType(
        FindKnownMapInstanceTypeInMapSpace(offset_in_page));
  }
  if (heap_addresses.old_space_first_page != 0 &&
      containing_page == heap_addresses.old_space_first_page) {
    return KnownInstanceType(
        FindKnownMapInstanceTypeInOldSpace(offset_in_page));
  }
  if (std::optional<uintptr_t> ro_offset =
          OffsetInKnownReadOnlySpace(address, heap_addresses)) {
    return KnownInstanceType(
        FindKnownMapInstanceTypeInReadOnlySpace(*ro_offset));
  }

  // For any unknown pages, compile a list of things this object might be.
  KnownInstanceType result;
  if (MayBeInMutableSpace(address)) {
    if (heap_addresses.map_space_first_page == 0) {
      int sub_result = FindKnownMapInstanceTypeInMapSpace(offset_in_page);
      if (sub_result >= 0) {
        result.types.push_back(static_cast<i::InstanceType>(sub_result));
      }
    }
    if (heap_addresses.old_space_first_page == 0) {
      int sub_result = FindKnownMapInstanceTypeInOldSpace(offset_in_page);
      if (sub_result >= 0) {
        result.types.push_back(static_cast<i::InstanceType>(sub_result));
      }
    }
  }
  if (std::optional<uintptr_t> ro_offset =
          OffsetInUnknownReadOnlySpace(address, heap_addresses)) {
    int sub_result = FindKnownMapInstanceTypeInReadOnlySpace(*ro_offset);
    if (sub_result >= 0) {
      result.types.push_back(static_cast<i::InstanceType>(sub_result));
    }
  }

  return result;
}

}  // namespace debug_helper_internal
}  // namespace internal
}  // namespace v8
