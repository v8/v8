// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_PARTIAL_DESERIALIZER_H_
#define V8_SNAPSHOT_PARTIAL_DESERIALIZER_H_

#include "src/snapshot/deserializer.h"
#include "src/snapshot/snapshot.h"

namespace v8 {
namespace internal {

// Deserializes the context-dependent object graph rooted at a given object.
// Currently, the only use-case is to deserialize native contexts.
// The PartialDeserializer is not expected to any deserialize code objects.
class PartialDeserializer : public Deserializer {
 public:
  explicit PartialDeserializer(SnapshotData* data)
      : Deserializer(data, false) {}

  // Deserialize a single object and the objects reachable from it.
  MaybeHandle<Object> Deserialize(
      Isolate* isolate, Handle<JSGlobalProxy> global_proxy,
      v8::DeserializeEmbedderFieldsCallback embedder_fields_deserializer) {
    return DeserializePartial(isolate, global_proxy,
                              embedder_fields_deserializer);
  }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_PARTIAL_DESERIALIZER_H_
