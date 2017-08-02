// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_OBJECT_DESERIALIZER_H_
#define V8_SNAPSHOT_OBJECT_DESERIALIZER_H_

#include "src/snapshot/deserializer.h"

namespace v8 {
namespace internal {

// Deserializes the object graph rooted at a given object.
// Currently, the ObjectDeserializer is only used to deserialize code objects
// and compiled wasm modules.
class ObjectDeserializer : public Deserializer {
 public:
  template <class Data>
  ObjectDeserializer(Data* data, bool deserializing_user_code)
      : Deserializer(data, deserializing_user_code) {}

  // Deserialize an object graph. Fail gracefully.
  MaybeHandle<HeapObject> Deserialize(Isolate* isolate) {
    return DeserializeObject(isolate);
  }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_OBJECT_DESERIALIZER_H_
