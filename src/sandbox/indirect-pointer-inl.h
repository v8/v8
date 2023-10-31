// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_INDIRECT_POINTER_INL_H_
#define V8_SANDBOX_INDIRECT_POINTER_INL_H_

#include "include/v8-internal.h"
#include "src/base/atomic-utils.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate.h"
#include "src/sandbox/code-pointer-table-inl.h"
#include "src/sandbox/indirect-pointer.h"
#include "src/sandbox/trusted-pointer-table-inl.h"

namespace v8 {
namespace internal {

V8_INLINE void InitSelfIndirectPointerField(Address field_address,
                                            LocalIsolate* isolate,
                                            Tagged<HeapObject> object) {
#ifdef V8_ENABLE_SANDBOX
  // TODO(saelo): we'll need the tag here in the future (to tag the entry in
  // the pointer table). At that point, DCHECK that we don't see
  // kCodeIndirectPointerTag here.
  // TODO(saelo): in the future, we might want to CHECK here or in
  // AllocateAndInitializeEntry that the object lives in trusted space.
  TrustedPointerTable::Space* space = isolate->heap()->trusted_pointer_space();
  IndirectPointerHandle handle =
      isolate->trusted_pointer_table()->AllocateAndInitializeEntry(
          space, object->ptr());
  // Use a Release_Store to ensure that the store of the pointer into the table
  // is not reordered after the store of the handle. Otherwise, other threads
  // may access an uninitialized table entry and crash.
  auto location = reinterpret_cast<IndirectPointerHandle*>(field_address);
  base::AsAtomic32::Release_Store(location, handle);
#else
  UNREACHABLE();
#endif
}

namespace {
#ifdef V8_ENABLE_SANDBOX
template <IndirectPointerTag tag>
V8_INLINE Tagged<Object> ResolveTrustedPointerHandle(
    IndirectPointerHandle handle, const Isolate* isolate) {
  const TrustedPointerTable& table = isolate->trusted_pointer_table();
  return Tagged<Object>(table.Get(handle));
}

V8_INLINE Tagged<Object> ResolveCodePointerHandle(
    IndirectPointerHandle handle) {
  CodePointerTable* table = GetProcessWideCodePointerTable();
  return Tagged<Object>(table->GetCodeObject(handle));
}
#endif  // V8_ENABLE_SANDBOX
}  // namespace

template <IndirectPointerTag tag>
V8_INLINE Tagged<Object> ReadIndirectPointerField(Address field_address,
                                                  const Isolate* isolate) {
#ifdef V8_ENABLE_SANDBOX
  // Load the indirect pointer handle from the object.
  auto location = reinterpret_cast<IndirectPointerHandle*>(field_address);
  IndirectPointerHandle handle = base::AsAtomic32::Relaxed_Load(location);
  DCHECK_NE(handle, kNullIndirectPointerHandle);

  // Resolve the handle. The tag implies the pointer table to use.
  // Here we generally assume that the load from the table cannot be reordered
  // before the load of the code object pointer due to the data dependency
  // between the two loads and therefore use relaxed memory ordering, but
  // technically we should use memory_order_consume here.
  if constexpr (tag == kUnknownIndirectPointerTag) {
    // In this case we need to check if the handle is a code pointer handle and
    // select the appropriate table based on that.
    if (handle & kCodePointerHandleMarker) {
      return ResolveCodePointerHandle(handle);
    } else {
      // TODO(saelo): once we have type tagging for entries in the trusted
      // pointer table, we could ASSUME that the top bits of the tag match the
      // instance type, which might allow the compiler to optimize subsequent
      // instance type checks.
      return ResolveTrustedPointerHandle<tag>(handle, isolate);
    }
  } else if constexpr (tag == kCodeIndirectPointerTag) {
    return ResolveCodePointerHandle(handle);
  } else {
    return ResolveTrustedPointerHandle<tag>(handle, isolate);
  }
#else
  UNREACHABLE();
#endif
}

template <IndirectPointerTag tag>
V8_INLINE void WriteIndirectPointerField(Address field_address,
                                         Tagged<ExposedTrustedObject> value) {
#ifdef V8_ENABLE_SANDBOX
  static_assert(tag != kIndirectPointerNullTag);
  IndirectPointerHandle handle = value->ReadField<IndirectPointerHandle>(
      ExposedTrustedObject::kSelfIndirectPointerOffset);
  auto location = reinterpret_cast<IndirectPointerHandle*>(field_address);
  base::AsAtomic32::Release_Store(location, handle);
#else
  UNREACHABLE();
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_INDIRECT_POINTER_INL_H_
