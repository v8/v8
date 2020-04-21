// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/serializer-deserializer.h"

#include "src/objects/foreign-inl.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

// The partial snapshot cache is terminated by undefined. We visit the
// partial snapshot...
//  - during deserialization to populate it.
//  - during normal GC to keep its content alive.
//  - not during serialization. The partial serializer adds to it explicitly.
DISABLE_CFI_PERF
void SerializerDeserializer::Iterate(Isolate* isolate, RootVisitor* visitor) {
  std::vector<Object>* cache = isolate->partial_snapshot_cache();
  for (size_t i = 0;; ++i) {
    // Extend the array ready to get a value when deserializing.
    if (cache->size() <= i) cache->push_back(Smi::zero());
    // During deserialization, the visitor populates the partial snapshot cache
    // and eventually terminates the cache with undefined.
    visitor->VisitRootPointer(Root::kPartialSnapshotCache, nullptr,
                              FullObjectSlot(&cache->at(i)));
    if (cache->at(i).IsUndefined(isolate)) break;
  }
}

bool SerializerDeserializer::CanBeDeferred(HeapObject o) {
  // ArrayBuffer instances are serialized by first re-assigning a index
  // to the backing store field, then serializing the object, and then
  // storing the actual backing store address again (and the same for the
  // ArrayBufferExtension). If serialization of the object itself is deferred,
  // the real backing store address is written into the snapshot, which cannot
  // be processed when deserializing.
  return !o.IsString() && !o.IsScript() && !o.IsJSTypedArray() &&
         !o.IsJSArrayBuffer();
}

void SerializerDeserializer::RestoreExternalReferenceRedirectors(
    const std::vector<AccessorInfo>& accessor_infos) {
  // Restore wiped accessor infos.
  for (AccessorInfo info : accessor_infos) {
    Foreign::cast(info.js_getter())
        .set_foreign_address(info.redirected_getter());
  }
}

void SerializerDeserializer::RestoreExternalReferenceRedirectors(
    const std::vector<CallHandlerInfo>& call_handler_infos) {
  for (CallHandlerInfo info : call_handler_infos) {
    Foreign::cast(info.js_callback())
        .set_foreign_address(info.redirected_callback());
  }
}

}  // namespace internal
}  // namespace v8
