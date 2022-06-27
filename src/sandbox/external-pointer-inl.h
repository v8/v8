// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_POINTER_INL_H_
#define V8_SANDBOX_EXTERNAL_POINTER_INL_H_

#include "include/v8-internal.h"
#include "src/execution/isolate.h"
#include "src/sandbox/external-pointer-table-inl.h"
#include "src/sandbox/external-pointer.h"

namespace v8 {
namespace internal {

#ifdef V8_SANDBOXED_EXTERNAL_POINTERS
template <ExternalPointerTag tag>
const ExternalPointerTable& GetExternalPointerTable(const Isolate* isolate) {
  return IsExternalPointerTagShareable(tag)
             ? isolate->shared_external_pointer_table()
             : isolate->external_pointer_table();
}

template <ExternalPointerTag tag>
ExternalPointerTable& GetExternalPointerTable(Isolate* isolate) {
  return IsExternalPointerTagShareable(tag)
             ? isolate->shared_external_pointer_table()
             : isolate->external_pointer_table();
}
#endif

template <ExternalPointerTag tag>
V8_INLINE void InitExternalPointerField(Address field_address,
                                        Isolate* isolate) {
  InitExternalPointerField<tag>(field_address, isolate, kNullExternalPointer);
}

template <ExternalPointerTag tag>
V8_INLINE void InitExternalPointerField(Address field_address, Isolate* isolate,
                                        Address value) {
#ifdef V8_SANDBOXED_EXTERNAL_POINTERS

  ExternalPointerTable& table = GetExternalPointerTable<tag>(isolate);
  ExternalPointerHandle handle = table.Allocate();
  table.Set(handle, value, tag);
  base::Memory<ExternalPointerHandle>(field_address) = handle;
#else
  WriteExternalPointerField<tag>(field_address, isolate, value);
#endif  // V8_SANDBOXED_EXTERNAL_POINTERS
}

template <ExternalPointerTag tag>
V8_INLINE Address ReadExternalPointerField(Address field_address,
                                           const Isolate* isolate) {
#ifdef V8_SANDBOXED_EXTERNAL_POINTERS
  ExternalPointerHandle handle =
      base::Memory<ExternalPointerHandle>(field_address);
  return GetExternalPointerTable<tag>(isolate).Get(handle, tag);
#else
  // Pointer compression causes types larger than kTaggedSize to be unaligned.
  constexpr bool may_be_unaligned = kExternalPointerSize > kTaggedSize;
  if (may_be_unaligned) {
    return base::ReadUnalignedValue<ExternalPointer_t>(field_address);
  } else {
    return base::Memory<ExternalPointer_t>(field_address);
  }
#endif  // V8_SANDBOXED_EXTERNAL_POINTERS
}

template <ExternalPointerTag tag>
V8_INLINE void WriteExternalPointerField(Address field_address,
                                         Isolate* isolate, Address value) {
#ifdef V8_SANDBOXED_EXTERNAL_POINTERS
  ExternalPointerHandle handle =
      base::Memory<ExternalPointerHandle>(field_address);
  GetExternalPointerTable<tag>(isolate).Set(handle, value, tag);
#else
  // Pointer compression causes types larger than kTaggedSize to be unaligned.
  constexpr bool may_be_unaligned = kExternalPointerSize > kTaggedSize;
  if (may_be_unaligned) {
    base::WriteUnalignedValue<ExternalPointer_t>(field_address, value);
  } else {
    base::Memory<ExternalPointer_t>(field_address) = value;
  }
#endif  // V8_SANDBOXED_EXTERNAL_POINTERS
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_EXTERNAL_POINTER_INL_H_
