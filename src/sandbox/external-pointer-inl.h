// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_POINTER_INL_H_
#define V8_SANDBOX_EXTERNAL_POINTER_INL_H_

#include "include/v8-internal.h"
#include "src/base/atomic-utils.h"
#include "src/execution/isolate.h"
#include "src/sandbox/external-pointer-table-inl.h"
#include "src/sandbox/external-pointer.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_SANDBOX
template <ExternalPointerTag tag>
const ExternalPointerTable& GetExternalPointerTable(const Isolate* isolate) {
  return IsSharedExternalPointerType(tag)
             ? isolate->shared_external_pointer_table()
             : isolate->external_pointer_table();
}

template <ExternalPointerTag tag>
ExternalPointerTable& GetExternalPointerTable(Isolate* isolate) {
  return IsSharedExternalPointerType(tag)
             ? isolate->shared_external_pointer_table()
             : isolate->external_pointer_table();
}
#endif  // V8_ENABLE_SANDBOX

#if DEBUG && V8_ENABLE_SANDBOX
// Helper routines to detect double-initialization of external pointer slots.
V8_INLINE bool IsUninitializedExternalPointerFieldInDebugBuilds(
    Address field_address) {
  auto MayBeInitializedExternalPointerHandle =
      [](ExternalPointerHandle handle) constexpr {
    uint32_t index = handle >> kExternalPointerIndexShift;
    return index != 0 && index < kMaxExternalPointers &&
           (index << kExternalPointerIndexShift) == handle;
  };

  // In debug builds, an uninitialized ExternalPointerSlot (on the V8 heap) will
  // always contain one of these values.
  static_assert(
      !MayBeInitializedExternalPointerHandle(kNullExternalPointerHandle));
  static_assert(!MayBeInitializedExternalPointerHandle(
      static_cast<ExternalPointerHandle>(kZapValue)));
  static_assert(!MayBeInitializedExternalPointerHandle(
      static_cast<ExternalPointerHandle>(kZapValue >> 32)));
  static_assert(!MayBeInitializedExternalPointerHandle(
      static_cast<ExternalPointerHandle>(kClearedFreeMemoryValue)));
  static_assert(!MayBeInitializedExternalPointerHandle(
      static_cast<ExternalPointerHandle>(kClearedFreeMemoryValue >> 32)));

  auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
  ExternalPointerHandle handle = base::AsAtomic32::Relaxed_Load(location);
  return !MayBeInitializedExternalPointerHandle(handle);
}
#endif  // DEBUG && V8_ENABLE_SANDBOX

template <ExternalPointerTag tag>
V8_INLINE void InitExternalPointerField(Address field_address, Isolate* isolate,
                                        Address value) {
#ifdef V8_ENABLE_SANDBOX
  if (IsSandboxedExternalPointerType(tag)) {
    // Re-initialization of external pointer slots is forbidden as it would
    // interfere with table compaction. See the explanation of the table
    // compaction algorithm in external-poiner-table.h.
    DCHECK(IsUninitializedExternalPointerFieldInDebugBuilds(field_address));

    ExternalPointerTable& table = GetExternalPointerTable<tag>(isolate);
    ExternalPointerHandle handle = table.AllocateAndInitializeEntry(value, tag);
    // Use a Release_Store to ensure that the store of the pointer into the
    // table is not reordered after the store of the handle. Otherwise, other
    // threads may access an uninitialized table entry and crash.
    auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
    base::AsAtomic32::Release_Store(location, handle);
    return;
  }
#endif  // V8_ENABLE_SANDBOX
  WriteExternalPointerField<tag>(field_address, isolate, value);
}

template <ExternalPointerTag tag>
V8_INLINE Address ReadExternalPointerField(Address field_address,
                                           const Isolate* isolate) {
#ifdef V8_ENABLE_SANDBOX
  if (IsSandboxedExternalPointerType(tag)) {
    // Handles may be written to objects from other threads so the handle needs
    // to be loaded atomically. We assume that the load from the table cannot
    // be reordered before the load of the handle due to the data dependency
    // between the two loads and therefore use relaxed memory ordering.
    auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
    ExternalPointerHandle handle = base::AsAtomic32::Relaxed_Load(location);
    return GetExternalPointerTable<tag>(isolate).Get(handle, tag);
  }
#endif  // V8_ENABLE_SANDBOX
  return ReadMaybeUnalignedValue<Address>(field_address);
}

template <ExternalPointerTag tag>
V8_INLINE void WriteExternalPointerField(Address field_address,
                                         Isolate* isolate, Address value) {
#ifdef V8_ENABLE_SANDBOX
  if (IsSandboxedExternalPointerType(tag)) {
    // See comment above for why this is a Relaxed_Load.
    auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
    ExternalPointerHandle handle = base::AsAtomic32::Relaxed_Load(location);
    GetExternalPointerTable<tag>(isolate).Set(handle, value, tag);
    return;
  }
#endif  // V8_ENABLE_SANDBOX
  WriteMaybeUnalignedValue<Address>(field_address, value);
}

template <ExternalPointerTag tag>
V8_INLINE void WriteLazilyInitializedExternalPointerField(Address field_address,
                                                          Isolate* isolate,
                                                          Address value) {
#ifdef V8_ENABLE_SANDBOX
  if (IsSandboxedExternalPointerType(tag)) {
    // See comment above for why this uses a Relaxed_Load and Release_Store.
    ExternalPointerTable& table = GetExternalPointerTable<tag>(isolate);
    auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
    ExternalPointerHandle handle = base::AsAtomic32::Relaxed_Load(location);
    if (handle == kNullExternalPointerHandle) {
      // Field has not been initialized yet.
      ExternalPointerHandle handle =
          table.AllocateAndInitializeEntry(value, tag);
      base::AsAtomic32::Release_Store(location, handle);
    } else {
      table.Set(handle, value, tag);
    }
    return;
  }
#endif  // V8_ENABLE_SANDBOX
  WriteMaybeUnalignedValue<Address>(field_address, value);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_EXTERNAL_POINTER_INL_H_
