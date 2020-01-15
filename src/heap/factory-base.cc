// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/factory-base.h"

#include "src/ast/ast.h"
#include "src/handles/handles-inl.h"
#include "src/heap/factory.h"
#include "src/heap/off-thread-factory.h"
#include "src/objects/string-inl.h"
#include "src/utils/memcopy.h"

namespace v8 {
namespace internal {

template <typename Impl>
FactoryHandle<Impl, SeqOneByteString>
FactoryBase<Impl>::NewOneByteInternalizedString(
    const Vector<const uint8_t>& str, uint32_t hash_field) {
  FactoryHandle<Impl, SeqOneByteString> result =
      AllocateRawOneByteInternalizedString(str.length(), hash_field);
  DisallowHeapAllocation no_gc;
  MemCopy(result->GetChars(no_gc), str.begin(), str.length());
  return result;
}

template <typename Impl>
FactoryHandle<Impl, SeqTwoByteString>
FactoryBase<Impl>::NewTwoByteInternalizedString(const Vector<const uc16>& str,
                                                uint32_t hash_field) {
  FactoryHandle<Impl, SeqTwoByteString> result =
      AllocateRawTwoByteInternalizedString(str.length(), hash_field);
  DisallowHeapAllocation no_gc;
  MemCopy(result->GetChars(no_gc), str.begin(), str.length() * kUC16Size);
  return result;
}

template <typename Impl>
FactoryMaybeHandle<Impl, SeqOneByteString>
FactoryBase<Impl>::NewRawOneByteString(int length, AllocationType allocation) {
  if (length > String::kMaxLength || length < 0) {
    return impl()->template Throw<SeqOneByteString>(
        impl()->NewInvalidStringLengthError());
  }
  DCHECK_GT(length, 0);  // Use Factory::empty_string() instead.
  int size = SeqOneByteString::SizeFor(length);
  DCHECK_GE(SeqOneByteString::kMaxSize, size);

  HeapObject result = AllocateRawWithImmortalMap(
      size, allocation, read_only_roots().one_byte_string_map());
  FactoryHandle<Impl, SeqOneByteString> string =
      handle(SeqOneByteString::cast(result), impl());
  string->set_length(length);
  string->set_hash_field(String::kEmptyHashField);
  DCHECK_EQ(size, string->Size());
  return string;
}

template <typename Impl>
FactoryMaybeHandle<Impl, SeqTwoByteString>
FactoryBase<Impl>::NewRawTwoByteString(int length, AllocationType allocation) {
  if (length > String::kMaxLength || length < 0) {
    return impl()->template Throw<SeqTwoByteString>(
        impl()->NewInvalidStringLengthError());
  }
  DCHECK_GT(length, 0);  // Use Factory::empty_string() instead.
  int size = SeqTwoByteString::SizeFor(length);
  DCHECK_GE(SeqTwoByteString::kMaxSize, size);

  HeapObject result = AllocateRawWithImmortalMap(
      size, allocation, read_only_roots().string_map());
  FactoryHandle<Impl, SeqTwoByteString> string =
      handle(SeqTwoByteString::cast(result), impl());
  string->set_length(length);
  string->set_hash_field(String::kEmptyHashField);
  DCHECK_EQ(size, string->Size());
  return string;
}

template <typename Impl>
FactoryHandle<Impl, SeqOneByteString>
FactoryBase<Impl>::AllocateRawOneByteInternalizedString(int length,
                                                        uint32_t hash_field) {
  CHECK_GE(String::kMaxLength, length);
  // The canonical empty_string is the only zero-length string we allow.
  DCHECK_IMPLIES(length == 0, !impl()->EmptyStringRootIsInitialized());

  Map map = read_only_roots().one_byte_internalized_string_map();
  int size = SeqOneByteString::SizeFor(length);
  HeapObject result = AllocateRawWithImmortalMap(
      size,
      impl()->CanAllocateInReadOnlySpace() ? AllocationType::kReadOnly
                                           : AllocationType::kOld,
      map);
  FactoryHandle<Impl, SeqOneByteString> answer =
      handle(SeqOneByteString::cast(result), impl());
  answer->set_length(length);
  answer->set_hash_field(hash_field);
  DCHECK_EQ(size, answer->Size());
  return answer;
}

template <typename Impl>
FactoryHandle<Impl, SeqTwoByteString>
FactoryBase<Impl>::AllocateRawTwoByteInternalizedString(int length,
                                                        uint32_t hash_field) {
  CHECK_GE(String::kMaxLength, length);
  DCHECK_NE(0, length);  // Use Heap::empty_string() instead.

  Map map = read_only_roots().internalized_string_map();
  int size = SeqTwoByteString::SizeFor(length);
  HeapObject result =
      AllocateRawWithImmortalMap(size, AllocationType::kOld, map);
  FactoryHandle<Impl, SeqTwoByteString> answer =
      handle(SeqTwoByteString::cast(result), impl());
  answer->set_length(length);
  answer->set_hash_field(hash_field);
  DCHECK_EQ(size, result.Size());
  return answer;
}

template <typename Impl>
HeapObject FactoryBase<Impl>::AllocateRawWithImmortalMap(
    int size, AllocationType allocation, Map map,
    AllocationAlignment alignment) {
  HeapObject result = AllocateRaw(size, allocation, alignment);
  result.set_map_after_allocation(map, SKIP_WRITE_BARRIER);
  return result;
}

template <typename Impl>
HeapObject FactoryBase<Impl>::AllocateRaw(int size, AllocationType allocation,
                                          AllocationAlignment alignment) {
  return impl()->AllocateRaw(size, allocation, alignment);
}

// Instantiate FactoryBase for the two variants we want.
template class EXPORT_TEMPLATE_DEFINE(V8_BASE_EXPORT) FactoryBase<Factory>;
template class EXPORT_TEMPLATE_DEFINE(V8_BASE_EXPORT)
    FactoryBase<OffThreadFactory>;

}  // namespace internal
}  // namespace v8
