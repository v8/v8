// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_HEAP_NUMBER_INL_H_
#define V8_OBJECTS_HEAP_NUMBER_INL_H_

#include "src/objects/heap-number.h"

#include "src/objects/objects-inl.h"
#include "src/objects/primitive-heap-object-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/heap-number-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(HeapNumber)

uint64_t HeapNumber::value_as_bits() const {
  // Bug(v8:8875): HeapNumber's double may be unaligned.
  return base::ReadUnalignedValue<uint64_t>(field_address(kValueOffset));
}

uint64_t HeapNumber::value_as_bits_relaxed() const {
#if defined(V8_TARGET_BIG_ENDIAN)
  return (static_cast<uint64_t>(RELAXED_READ_UINT32_FIELD(*this, kValueOffset))
          << 32) |
         static_cast<uint64_t>(
             RELAXED_READ_UINT32_FIELD(*this, kValueOffset + kUInt32Size));
#else
  return static_cast<uint64_t>(RELAXED_READ_UINT32_FIELD(*this, kValueOffset)) |
         (static_cast<uint64_t>(
              RELAXED_READ_UINT32_FIELD(*this, kValueOffset + kUInt32Size))
          << 32);
#endif
}

void HeapNumber::set_value_as_bits(uint64_t bits) {
  base::WriteUnalignedValue<uint64_t>(field_address(kValueOffset), bits);
}

int HeapNumber::get_exponent() {
  return ((ReadField<int>(kExponentOffset) & kExponentMask) >> kExponentShift) -
         kExponentBias;
}

int HeapNumber::get_sign() {
  return ReadField<int>(kExponentOffset) & kSignMask;
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_HEAP_NUMBER_INL_H_
