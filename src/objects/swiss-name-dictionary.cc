// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Only including the -inl.h file directly makes the linter complain.
#include "src/objects/swiss-name-dictionary.h"

#include "src/objects/swiss-name-dictionary-inl.h"

namespace v8 {
namespace internal {

// static
Handle<SwissNameDictionary> SwissNameDictionary::DeleteEntry(
    Isolate* isolate, Handle<SwissNameDictionary> table, InternalIndex entry) {
  // GetCtrl() does the bounds check.
  DCHECK(IsFull(table->GetCtrl(entry.as_int())));

  int i = entry.as_int();

  table->SetCtrl(i, Ctrl::kDeleted);
  table->ClearDataTableEntry(isolate, i);
  // We leave the PropertyDetails unchanged because they are not relevant for
  // GC.

  int nof = table->NumberOfElements();
  table->SetNumberOfElements(nof - 1);
  int nod = table->NumberOfDeletedElements();
  table->SetNumberOfDeletedElements(nod + 1);

  // TODO(v8:11388) Abseil's flat_hash_map doesn't shrink on deletion, but may
  // decide on addition to do an in-place rehash to remove deleted elements. We
  // shrink on deletion here to follow what NameDictionary and
  // OrderedNameDictionary do. We should investigate which approach works
  // better.
  return Shrink(isolate, table);
}

// static
template <typename LocalIsolate>
Handle<SwissNameDictionary> SwissNameDictionary::Rehash(
    LocalIsolate* isolate, Handle<SwissNameDictionary> table,
    int new_capacity) {
  DCHECK(IsValidCapacity(new_capacity));
  DCHECK_LE(table->NumberOfElements(), MaxUsableCapacity(new_capacity));
  ReadOnlyRoots roots(isolate);

  Handle<SwissNameDictionary> new_table =
      isolate->factory()->NewSwissNameDictionaryWithCapacity(
          new_capacity, Heap::InYoungGeneration(*table) ? AllocationType::kYoung
                                                        : AllocationType::kOld);

  DisallowHeapAllocation no_gc;

  int new_enum_index = 0;
  new_table->SetNumberOfElements(table->NumberOfElements());
  for (int enum_index = 0; enum_index < table->UsedCapacity(); ++enum_index) {
    int entry = table->EntryForEnumerationIndex(enum_index);

    Object key;

    if (table->ToKey(roots, entry, &key)) {
      Object value = table->ValueAtRaw(entry);
      PropertyDetails details = table->DetailsAt(entry);

      int new_entry = new_table->AddInternal(Name::cast(key), value, details);

      // TODO(v8::11388) Investigate ways of hoisting the branching needed to
      // select the correct meta table entry size (based on the capacity of the
      // table) out of the loop.
      new_table->SetEntryForEnumerationIndex(new_enum_index, new_entry);
      ++new_enum_index;
    }
  }

  new_table->SetHash(table->Hash());
  return new_table;
}

// static
Handle<SwissNameDictionary> SwissNameDictionary::Shrink(
    Isolate* isolate, Handle<SwissNameDictionary> table) {
  // TODO(v8:11388) We're using the same logic to decide whether or not to
  // shrink as OrderedNameDictionary and NameDictionary here. We should compare
  // this with the logic used by Abseil's flat_hash_map, which has a heuristic
  // for triggering an (in-place) rehash on addition, but never shrinks the
  // table. Abseil's heuristic doesn't take the numbere of deleted elements into
  // account, because it doesn't track that.

  int nof = table->NumberOfElements();
  int capacity = table->Capacity();
  if (nof >= (capacity >> 2)) return table;
  int new_capacity = std::max(capacity / 2, kInitialCapacity);
  return Rehash(isolate, table, new_capacity);
}

// TODO(v8::11388) Copying all data into a std::vector and then re-adding into
// the table doesn't seem like a good algorithm. Abseil's Swiss Tables come with
// a clever algorithm for re-hashing in place: It first changes the control
// table, effectively changing the roles of full, empty and deleted buckets. It
// then moves each entry to its new bucket by swapping entries (see
// drop_deletes_without_resize in Abseil's raw_hash_set.h). This algorithm could
// generally adapted to work on our insertion order preserving implementation,
// too. However, it would require a mapping from hash table buckets back to
// enumeration indices. This could either be be created in this function
// (requiring a vector with Capacity() entries and a separate pass over the
// enumeration table) or by creating this backwards mapping ahead of time and
// storing it somewhere in the main table or the meta table, for those
// SwissNameDictionaries that we know will be in-place rehashed, most notably
// those stored in the snapshot.
void SwissNameDictionary::Rehash(Isolate* isolate) {
  DisallowHeapAllocation no_gc;

  struct Entry {
    Name key;
    Object value;
    PropertyDetails details;
  };

  if (Capacity() == 0) return;

  Entry dummy{Name(), Object(), PropertyDetails::Empty()};
  std::vector<Entry> data(NumberOfElements(), dummy);

  ReadOnlyRoots roots(isolate);
  int data_index = 0;
  for (int enum_index = 0; enum_index < UsedCapacity(); ++enum_index) {
    int entry = EntryForEnumerationIndex(enum_index);
    Object key;
    if (!ToKey(roots, entry, &key)) continue;

    data[data_index++] =
        Entry{Name::cast(key), ValueAtRaw(entry), DetailsAt(entry)};
  }

  Initialize(isolate, meta_table(), Capacity());

  int new_enum_index = 0;
  SetNumberOfElements(static_cast<int>(data.size()));
  for (Entry& e : data) {
    int new_entry = AddInternal(e.key, e.value, e.details);

    // TODO(v8::11388) Investigate ways of hoisting the branching needed to
    // select the correct meta table entry size (based on the capacity of the
    // table) out of the loop.
    SetEntryForEnumerationIndex(new_enum_index, new_entry);
    ++new_enum_index;
  }
}

// The largest value we ever have to store in the enumeration table is
// Capacity() - 1. The largest value we ever have to store for the present or
// deleted element count is MaxUsableCapacity(Capacity()). All data in the
// meta table is unsigned. Using this, we verify the values of the constants
// |kMax1ByteMetaTableCapacity| and |kMax2ByteMetaTableCapacity|.
STATIC_ASSERT(SwissNameDictionary::kMax1ByteMetaTableCapacity - 1 <=
              std::numeric_limits<uint8_t>::max());
STATIC_ASSERT(SwissNameDictionary::MaxUsableCapacity(
                  SwissNameDictionary::kMax1ByteMetaTableCapacity) <=
              std::numeric_limits<uint8_t>::max());
STATIC_ASSERT(SwissNameDictionary::kMax2ByteMetaTableCapacity - 1 <=
              std::numeric_limits<uint16_t>::max());
STATIC_ASSERT(SwissNameDictionary::MaxUsableCapacity(
                  SwissNameDictionary::kMax2ByteMetaTableCapacity) <=
              std::numeric_limits<uint16_t>::max());

template void SwissNameDictionary::Initialize(Isolate* isolate,
                                              ByteArray meta_table,
                                              int capacity);
template void SwissNameDictionary::Initialize(LocalIsolate* isolate,
                                              ByteArray meta_table,
                                              int capacity);

constexpr int SwissNameDictionary::kInitialCapacity;

}  // namespace internal
}  // namespace v8
