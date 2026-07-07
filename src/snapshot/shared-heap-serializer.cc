// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/shared-heap-serializer.h"

#include <vector>

#include "src/common/ptr-compr-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/objects/objects-inl.h"
#include "src/snapshot/read-only-serializer.h"

namespace v8 {
namespace internal {

// static
bool SharedHeapSerializer::CanBeInSharedOldSpace(Tagged<HeapObject> obj) {
  if (ReadOnlyHeap::Contains(obj)) return false;
  if (IsString(obj)) {
    return IsInternalizedString(obj) ||
           String::IsInPlaceInternalizable(Cast<String>(obj));
  }
  return false;
}

// static
bool SharedHeapSerializer::ShouldBeInSharedHeapObjectCache(
    Tagged<HeapObject> obj) {
  // To keep the shared heap object cache lean, only include objects that should
  // not be duplicated. Currently, that is only internalized strings. In-place
  // internalizable strings will still be allocated in the shared heap by the
  // deserializer, but do not need to be kept alive forever in the cache.
  if (CanBeInSharedOldSpace(obj)) {
    if (IsInternalizedString(obj)) return true;
  }
  return false;
}

SharedHeapSerializer::SharedHeapSerializer(Isolate* isolate,
                                           Snapshot::SerializerFlags flags)
    : RootsSerializer(isolate, flags, RootIndex::kFirstStrongRoot)
#ifdef DEBUG
      ,
      serialized_objects_(isolate->heap())
#endif
{
  if (ShouldReconstructSharedHeapObjectCacheForTesting()) {
    ReconstructSharedHeapObjectCacheForTesting();
  }
}

SharedHeapSerializer::~SharedHeapSerializer() {
  OutputStatistics("SharedHeapSerializer");
}

void SharedHeapSerializer::FinalizeSerialization() {
  // This is called after serialization of the startup and context snapshots
  // which entries are added to the shared heap object cache. Terminate the
  // cache with an undefined.
  Tagged<Object> undefined = ReadOnlyRoots(isolate()).undefined_value();
  VisitRootPointer(Root::kSharedHeapObjectCache, nullptr,
                   FullObjectSlot(&undefined));

  // When v8_flags.shared_string_table is true, all internalized and
  // internalizable-in-place strings are in the shared heap.
  SerializeStringTable(isolate()->string_table());
  SerializeDeferredObjects();
  Pad();

#ifdef DEBUG
  // Check that all serialized object are in shared heap and not RO. RO objects
  // should be in the RO snapshot.
  IdentityMap<int, base::DefaultAllocationPolicy>::IteratableScope it_scope(
      &serialized_objects_);
  for (auto it = it_scope.begin(); it != it_scope.end(); ++it) {
    Tagged<HeapObject> obj = Cast<HeapObject>(it.key());
    CHECK(CanBeInSharedOldSpace(obj));
    CHECK(!ReadOnlyHeap::Contains(obj));
  }
#endif
}

bool SharedHeapSerializer::SerializeUsingSharedHeapObjectCache(
    SnapshotByteSink* sink, Handle<HeapObject> obj) {
  if (!ShouldBeInSharedHeapObjectCache(*obj)) return false;
  int cache_index = SerializeInObjectCache(obj);

  // When testing deserialization of a snapshot from a live Isolate where there
  // is also a shared Isolate, the shared object cache needs to be extended
  // because the live isolate may have had new internalized strings that were
  // not present in the startup snapshot to be serialized.
  if (ShouldReconstructSharedHeapObjectCacheForTesting()) {
    std::vector<Tagged<Object>>* existing_cache =
        isolate()->shared_space_isolate()->shared_heap_object_cache();
    const size_t existing_cache_size = existing_cache->size();
    // This is strictly < because the existing cache contains the terminating
    // undefined value, which the reconstructed cache does not.
    DCHECK_LT(base::checked_cast<size_t>(cache_index), existing_cache_size);
    if (base::checked_cast<size_t>(cache_index) == existing_cache_size - 1) {
      ReadOnlyRoots roots(isolate());
      DCHECK(IsUndefined(existing_cache->back()));
      existing_cache->back() = *obj;
      existing_cache->push_back(roots.undefined_value());
    }
  }

  sink->Put(kSharedHeapObjectCache, "SharedHeapObjectCache");
  sink->PutUint30(cache_index, "shared_heap_object_cache_index");
  return true;
}

void SharedHeapSerializer::SerializeStringTable(StringTable* string_table) {
  // Iterate the string table and collect non-RO strings for serialization.
  // Non-RO strings need to be serialized separately because their addresses
  // are not stable across isolates.
  class NonRoStringCollector : public RootVisitor {
   public:
    explicit NonRoStringCollector(SharedHeapSerializer* serializer)
        : serializer_(serializer) {}

    void VisitRootPointers(Root root, const char* description,
                           FullObjectSlot start, FullObjectSlot end) override {
      UNREACHABLE();
    }

    void VisitCompressedRootPointers(Root root, const char* description,
                                     OffHeapObjectSlot start,
                                     OffHeapObjectSlot end) override {
      DCHECK_EQ(root, Root::kStringTable);
      Isolate* isolate = serializer_->isolate();
      for (OffHeapObjectSlot current = start; current < end; ++current) {
        Tagged<Object> obj = current.load(isolate);
        if (IsHeapObject(obj)) {
          DCHECK(IsInternalizedString(obj));
          if (!ReadOnlyHeap::Contains(Cast<HeapObject>(obj))) {
            non_ro_strings.push_back(handle(Cast<HeapObject>(obj), isolate));
          }
        }
      }
    }

    std::vector<Handle<HeapObject>> non_ro_strings;

   private:
    SharedHeapSerializer* serializer_;
  };
  NonRoStringCollector collector(this);
  string_table->IterateElements(&collector);

#ifdef V8_COMPRESS_POINTERS
  // With pointer compression, the StringTable is serialized as a blob
  // containing the raw hash table data:
  //
  //   capacity : Uint30
  //   number_of_ro_elements : Uint30
  //   number_of_deleted_elements : Uint30
  //   elements[0..capacity-1] : Tagged_t[]
  //   number_of_non_ro_strings : Uint30
  //   non_ro_strings[0..n-1] : serialized object references
  //
  // The elements array contains compressed pointers (cage offsets) to strings
  // in RO space (stable with static roots), or sentinel values (Smi 0 for
  // empty, Smi 1 for deleted). Non-RO strings are replaced with deleted
  // markers in the blob and serialized separately as object references.
  //
  // For deserialization:
  // - No-rehash case: the blob is memcpy'd directly into a new hash table.
  // - Rehash case: the blob is iterated to load pointers to RO strings, which
  //   are then inserted into a fresh hash table.
  // - Both cases: Non-RO strings are then inserted into the table.

  int capacity;
  int number_of_elements;
  const Tagged_t* elements;
  string_table->GetSerializedData(&capacity, &number_of_elements, &elements);

  // Make a copy of the elements array so we can modify it.
  std::vector<Tagged_t> elements_copy(elements, elements + capacity);
  // Zap non-RO string entries in the copy.
  int number_of_non_ro = static_cast<int>(collector.non_ro_strings.size());
  for (int i = 0; i < capacity; i++) {
    Tagged_t raw = elements_copy[i];
    if (HAS_SMI_TAG(raw)) continue;  // empty or deleted
    Tagged<Object> obj =
        Tagged<Object>(V8HeapCompressionScheme::DecompressTagged(raw));
    if (!ReadOnlyHeap::Contains(Cast<HeapObject>(obj))) {
      elements_copy[i] = V8HeapCompressionScheme::CompressObject(
          StringTable::deleted_element().ptr());
    }
  }

  // Count deleted entries in the blob. This includes both the newly zapped
  // non-RO entries and any pre-existing deleted entries from GC.
  Tagged_t deleted_tag = V8HeapCompressionScheme::CompressObject(
      StringTable::deleted_element().ptr());
  int number_of_deleted = 0;
  for (int i = 0; i < capacity; i++) {
    if (elements_copy[i] == deleted_tag) number_of_deleted++;
  }

  int number_of_ro_elements = number_of_elements - number_of_non_ro;
  sink_.PutUint30(capacity, "String table capacity");
  sink_.PutUint30(number_of_ro_elements, "String table number of RO elements");
  sink_.PutUint30(number_of_deleted, "String table number of deleted elements");
  // Align to Tagged_t size so the elements array is properly aligned.
  while (!IsAligned(sink_.Position(), sizeof(Tagged_t))) {
    sink_.Put(kNop, "String table alignment padding");
  }
  sink_.PutRaw(reinterpret_cast<const uint8_t*>(elements_copy.data()),
               capacity * sizeof(Tagged_t), "String table elements");
#endif  // V8_COMPRESS_POINTERS

  // Serialize non-RO strings. Without pointer compression, RO strings are
  // added to the string table at runtime by iterating the read-only heap,
  // so only non-RO strings need to be serialized.
  sink_.PutUint30(static_cast<int>(collector.non_ro_strings.size()),
                  "Number of non-RO strings");
  for (Handle<HeapObject> obj : collector.non_ro_strings) {
    SerializeObject(obj, SlotType::kAnySlot);
  }
}

void SharedHeapSerializer::SerializeObjectImpl(Handle<HeapObject> obj,
                                               SlotType slot_type) {
  // Objects in the shared heap cannot depend on per-Isolate roots but can
  // depend on RO roots since sharing objects requires sharing the RO space.
  DCHECK(CanBeInSharedOldSpace(*obj) || ReadOnlyHeap::Contains(*obj));
  {
    DisallowGarbageCollection no_gc;
    Tagged<HeapObject> raw = *obj;
    if (SerializeHotObject(raw)) return;
    if (IsRootAndHasBeenSerialized(raw) && SerializeRoot(raw)) return;
  }
  if (SerializeReadOnlyObjectReference(*obj, &sink_)) return;
  {
    DisallowGarbageCollection no_gc;
    Tagged<HeapObject> raw = *obj;
    if (SerializeBackReference(raw)) return;
    CheckRehashability(raw);

    DCHECK(!ReadOnlyHeap::Contains(raw));
  }

  ObjectSerializer object_serializer(this, obj, &sink_);
  object_serializer.Serialize(slot_type);

#ifdef DEBUG
  CHECK_NULL(serialized_objects_.Find(obj));
  // There's no "IdentitySet", so use an IdentityMap with a value that is
  // later ignored.
  serialized_objects_.Insert(obj, 0);
#endif
}

bool SharedHeapSerializer::ShouldReconstructSharedHeapObjectCacheForTesting()
    const {
  // When the live Isolate being serialized is not a client Isolate, there's no
  // need to reconstruct the shared heap object cache because it is not actually
  // shared.
  return reconstruct_read_only_and_shared_object_caches_for_testing() &&
         isolate()->has_shared_space();
}

void SharedHeapSerializer::ReconstructSharedHeapObjectCacheForTesting() {
  std::vector<Tagged<Object>>* cache =
      isolate()->shared_space_isolate()->shared_heap_object_cache();
  // Don't reconstruct the final element, which is always undefined and marks
  // the end of the cache, since serializing the live Isolate may extend the
  // shared object cache.
  for (size_t i = 0, size = cache->size(); i < size - 1; i++) {
    Handle<HeapObject> obj(Cast<HeapObject>(cache->at(i)), isolate());
    DCHECK(ShouldBeInSharedHeapObjectCache(*obj));
    int cache_index = SerializeInObjectCache(obj);
    USE(cache_index);
    DCHECK_EQ(cache_index, i);
  }
  DCHECK(IsUndefined(cache->back()));
}

}  // namespace internal
}  // namespace v8
