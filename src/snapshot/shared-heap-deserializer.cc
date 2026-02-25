// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/shared-heap-deserializer.h"

#include "src/heap/heap-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/objects/string-table.h"

namespace v8 {
namespace internal {

void SharedHeapDeserializer::DeserializeIntoIsolate() {
  // Don't deserialize into isolates that don't own their string table. If there
  // are client Isolates, the shared heap object cache should already be
  // populated.
  // TODO(372493838): The shared heap object cache can only contain strings.
  // Update name to reflect this.
  if (!isolate()->OwnsStringTables()) {
    DCHECK(!isolate()->shared_heap_object_cache()->empty());
    return;
  }

  DCHECK(isolate()->shared_heap_object_cache()->empty());
  HandleScope scope(isolate());

  IterateSharedHeapObjectCache(isolate(), this);
  DeserializeStringTable();
  DeserializeDeferredObjects();

  if (should_rehash()) {
    // The hash seed has already been initialized in ReadOnlyDeserializer, thus
    // there is no need to call `HashSeed::InitializeRoots(isolate());`.
    Rehash();
  }
}

void SharedHeapDeserializer::DeserializeStringTable() {
  // See SharedHeapSerializer::SerializeStringTable.

  DCHECK(isolate()->OwnsStringTables());

  StringTable* t = isolate()->string_table();

#ifdef V8_COMPRESS_POINTERS
  DCHECK_EQ(t->NumberOfElements(), 0);

  // With pointer compression, we use the blob format.
  const int capacity = source()->GetUint30();
  DCHECK_NE(capacity, 0);
  const int number_of_ro_elements = source()->GetUint30();
  const int number_of_deleted_elements = source()->GetUint30();
  const size_t elements_size = capacity * sizeof(Tagged_t);

  // Skip alignment padding.
  while (!IsAligned(source()->position(), sizeof(Tagged_t))) {
    source()->Advance(1);
  }

  // Get a direct pointer to the elements in the snapshot.
  const Tagged_t* elements = reinterpret_cast<const Tagged_t*>(
      source()->data() + source()->position());
  source()->Advance(static_cast<int>(elements_size));

  if (!should_rehash()) {
    // Fast path: copy the hash table blob directly into the string table.
    // This works because with pointer compression, the elements are cage
    // offsets which are stable across processes (with static roots).
    t->InitializeFromSerializedData(isolate(), capacity, number_of_ro_elements,
                                    number_of_deleted_elements, elements);
    DCHECK_EQ(t->NumberOfElements(), number_of_ro_elements);
  } else {
    // Slow path: need to rehash.
    // Iterate the blob to find RO strings, then insert them into the string
    // table with freshly computed hash positions.
    for (int i = 0; i < capacity; i++) {
      Tagged_t raw = elements[i];
      // Skip Smi entries (empty and deleted markers).
      // Smis have the tag bit (bit 0) clear; heap object pointers have it set.
      if (HAS_SMI_TAG(raw)) {
        continue;
      }
      // Decompress the pointer using OffHeapObjectSlot.
      OffHeapObjectSlot slot(const_cast<Tagged_t*>(&elements[i]));
      Tagged<Object> obj = slot.load(isolate());
      DCHECK(IsInternalizedString(obj));
      t->InsertForReadOnlyDeserialization(isolate(),
                                          Cast<InternalizedString>(obj));
    }
    DCHECK_EQ(t->NumberOfElements(), number_of_ro_elements);
  }

#else   // !V8_COMPRESS_POINTERS
  // Without pointer compression, we populate the string table by iterating
  // the RO heap. For the first isolate this was already done during RO
  // deserialization, but subsequent isolates sharing the RO heap need to
  // do it here.
  if (t->NumberOfElements() == 0) {
    ReadOnlyHeapObjectIterator it(isolate()->read_only_heap());
    for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
      if (IsInternalizedString(o)) {
        t->InsertForReadOnlyDeserialization(isolate(),
                                            Cast<InternalizedString>(o));
      }
    }
  }
#endif  // V8_COMPRESS_POINTERS

  // Insert non-read-only-space internalized strings into the string table.
  const int num_ro_strings = t->NumberOfElements();
  const int num_non_ro_strings = source()->GetUint30();
  for (int i = 0; i < num_non_ro_strings; i++) {
    DirectHandle<HeapObject> obj = ReadObject();
    DCHECK(IsInternalizedString(*obj));
    t->InsertForReadOnlyDeserialization(isolate(),
                                        Cast<InternalizedString>(*obj));
  }
  CHECK_EQ(t->NumberOfElements(), num_ro_strings + num_non_ro_strings);
}

}  // namespace internal
}  // namespace v8
