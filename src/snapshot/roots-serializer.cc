// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/roots-serializer.h"

#include "src/execution/isolate.h"
#include "src/heap/heap.h"

namespace v8 {
namespace internal {

RootsSerializer::RootsSerializer(Isolate* isolate,
                                 Snapshot::SerializerFlags flags,
                                 RootIndex first_root_to_be_serialized)
    : Serializer(isolate, flags),
      first_root_to_be_serialized_(first_root_to_be_serialized),
      object_cache_index_map_(isolate->heap()),
      can_be_rehashed_(true) {}

int RootsSerializer::SerializeInObjectCache(Handle<HeapObject> heap_object) {
  int index;
  if (!object_cache_index_map_.LookupOrInsert(*heap_object, &index)) {
    // This object is not part of the object cache yet. Add it to the cache so
    // we can refer to it via cache index from the delegating snapshot.
    SerializeObjectInObjectCache(heap_object);
  }
  return index;
}

void RootsSerializer::CheckRehashability(Tagged<HeapObject> obj) {
  if (!can_be_rehashed_) return;
  if (!obj->NeedsRehashing()) return;
  if (obj->CanBeRehashed()) return;
  can_be_rehashed_ = false;
}

}  // namespace internal
}  // namespace v8
