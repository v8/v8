// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TRUSTED_OBJECT_INL_H_
#define V8_OBJECTS_TRUSTED_OBJECT_INL_H_

#include "src/objects/instance-type-inl.h"
#include "src/objects/trusted-object.h"
#include "src/sandbox/sandbox.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

CAST_ACCESSOR(TrustedObject)
OBJECT_CONSTRUCTORS_IMPL(TrustedObject, HeapObject)

ProtectedPointerSlot TrustedObject::RawProtectedPointerField(
    int byte_offset) const {
  return ProtectedPointerSlot(field_address(byte_offset));
}

CAST_ACCESSOR(ExposedTrustedObject)
OBJECT_CONSTRUCTORS_IMPL(ExposedTrustedObject, TrustedObject)

void ExposedTrustedObject::init_self_indirect_pointer(
    IsolateForSandbox isolate) {
#ifdef V8_ENABLE_SANDBOX
  InitSelfIndirectPointerField(kSelfIndirectPointerOffset, isolate);
#endif
}

IndirectPointerHandle ExposedTrustedObject::self_indirect_pointer_handle()
    const {
#ifdef V8_ENABLE_SANDBOX
  return Relaxed_ReadField<IndirectPointerHandle>(kSelfIndirectPointerOffset);
#else
  UNREACHABLE();
#endif
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_TRUSTED_OBJECT_INL_H_
