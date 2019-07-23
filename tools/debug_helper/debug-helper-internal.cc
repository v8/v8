// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug-helper-internal.h"
#include "src/common/ptr-compr-inl.h"

namespace i = v8::internal;

namespace v8_debug_helper_internal {

bool IsPointerCompressed(uintptr_t address) {
#if COMPRESS_POINTERS_BOOL
  STATIC_ASSERT(i::kPtrComprHeapReservationSize == uintptr_t{1} << 32);
  intptr_t upper_half = static_cast<intptr_t>(address) >> 32;
  // Allow compressed pointers to be either zero-extended or sign-extended by
  // the caller.
  return upper_half == 0 || upper_half == -1;
#else
  return false;
#endif
}

uintptr_t Decompress(uintptr_t address, uintptr_t any_uncompressed_ptr) {
  if (!COMPRESS_POINTERS_BOOL || !IsPointerCompressed(address)) return address;
  return i::DecompressTaggedAny(any_uncompressed_ptr,
                                static_cast<i::Tagged_t>(address));
}

d::PropertyKind GetArrayKind(d::MemoryAccessResult mem_result) {
  d::PropertyKind indexed_field_kind{};
  switch (mem_result) {
    case d::MemoryAccessResult::kOk:
      indexed_field_kind = d::PropertyKind::kArrayOfKnownSize;
      break;
    case d::MemoryAccessResult::kAddressNotValid:
      indexed_field_kind =
          d::PropertyKind::kArrayOfUnknownSizeDueToInvalidMemory;
      break;
    default:
      indexed_field_kind =
          d::PropertyKind::kArrayOfUnknownSizeDueToValidButInaccessibleMemory;
      break;
  }
  return indexed_field_kind;
}

std::vector<std::unique_ptr<ObjectProperty>> TqObject::GetProperties(
    d::MemoryAccessor accessor) {
  return std::vector<std::unique_ptr<ObjectProperty>>();
}

}  // namespace v8_debug_helper_internal
