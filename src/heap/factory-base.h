// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_FACTORY_BASE_H_
#define V8_HEAP_FACTORY_BASE_H_

#include "src/common/globals.h"
#include "src/handles/handle-for.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

class HeapObject;
class SeqOneByteString;
class SeqTwoByteString;
class FreshlyAllocatedBigInt;

template <typename Impl>
class V8_EXPORT_PRIVATE FactoryBase {
 public:
  HandleFor<Impl, SeqOneByteString> NewOneByteInternalizedString(
      const Vector<const uint8_t>& str, uint32_t hash_field);
  HandleFor<Impl, SeqTwoByteString> NewTwoByteInternalizedString(
      const Vector<const uc16>& str, uint32_t hash_field);

  HandleFor<Impl, SeqOneByteString> AllocateRawOneByteInternalizedString(
      int length, uint32_t hash_field);
  HandleFor<Impl, SeqTwoByteString> AllocateRawTwoByteInternalizedString(
      int length, uint32_t hash_field);

  // Allocates and partially initializes an one-byte or two-byte String. The
  // characters of the string are uninitialized. Currently used in regexp code
  // only, where they are pretenured.
  V8_WARN_UNUSED_RESULT MaybeHandleFor<Impl, SeqOneByteString>
  NewRawOneByteString(int length,
                      AllocationType allocation = AllocationType::kYoung);
  V8_WARN_UNUSED_RESULT MaybeHandleFor<Impl, SeqTwoByteString>
  NewRawTwoByteString(int length,
                      AllocationType allocation = AllocationType::kYoung);
  // Create a new cons string object which consists of a pair of strings.
  V8_WARN_UNUSED_RESULT MaybeHandleFor<Impl, String> NewConsString(
      HandleFor<Impl, String> left, HandleFor<Impl, String> right,
      AllocationType allocation = AllocationType::kYoung);

  V8_WARN_UNUSED_RESULT HandleFor<Impl, String> NewConsString(
      HandleFor<Impl, String> left, HandleFor<Impl, String> right, int length,
      bool one_byte, AllocationType allocation = AllocationType::kYoung);

  // Allocates a new BigInt with {length} digits. Only to be used by
  // MutableBigInt::New*.
  HandleFor<Impl, FreshlyAllocatedBigInt> NewBigInt(
      int length, AllocationType allocation = AllocationType::kYoung);

 protected:
  HeapObject AllocateRawWithImmortalMap(
      int size, AllocationType allocation, Map map,
      AllocationAlignment alignment = kWordAligned);
  HeapObject NewWithImmortalMap(Map map, AllocationType allocation);

 private:
  Impl* impl() { return static_cast<Impl*>(this); }
  auto isolate() { return impl()->isolate(); }
  ReadOnlyRoots read_only_roots() { return impl()->read_only_roots(); }

  HeapObject AllocateRaw(int size, AllocationType allocation,
                         AllocationAlignment alignment = kWordAligned);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_FACTORY_BASE_H_
