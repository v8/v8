// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/ephemeron-remembered-set.h"

#include "src/heap/heap-inl.h"
#include "src/heap/remembered-set.h"

namespace v8::internal {

void EphemeronRememberedSet::RecordEphemeronKeyWrite(EphemeronHashTable table,
                                                     Address slot) {
  DCHECK(ObjectInYoungGeneration(HeapObjectSlot(slot).ToHeapObject()));
  if (v8_flags.minor_mc) {
    // Minor MC lacks support for specialized generational ephemeron barriers.
    // The regular write barrier works as well but keeps more memory alive.
    // TODO(v8:12612): Add support to MinorMC.
    MemoryChunk* chunk = MemoryChunk::FromHeapObject(table);
    RememberedSet<OLD_TO_NEW>::Insert<AccessMode::NON_ATOMIC>(chunk, slot);
  } else {
    int slot_index = EphemeronHashTable::SlotToIndex(table.address(), slot);
    InternalIndex entry = EphemeronHashTable::IndexToEntry(slot_index);
    auto it = tables_.insert({table, std::unordered_set<int>()});
    it.first->second.insert(entry.as_int());
  }
}

void EphemeronRememberedSet::RecordEphemeronKeyWrites(EphemeronHashTable table,
                                                      IndicesSet indices) {
  DCHECK(!Heap::InYoungGeneration(table));
  auto it = tables_.find(table);
  if (it != tables_.end()) {
    it->second.merge(std::move(indices));
  } else {
    tables_.insert({table, std::move(indices)});
  }
}

}  // namespace v8::internal
