// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_STARTUP_SERIALIZER_H_
#define V8_SNAPSHOT_STARTUP_SERIALIZER_H_

#include "src/snapshot/serializer.h"

namespace v8 {
namespace internal {

class StartupSerializer : public Serializer {
 public:
  StartupSerializer(Isolate* isolate, SnapshotByteSink* sink);
  ~StartupSerializer() override;

  // Serialize the current state of the heap.  The order is:
  // 1) Strong references.
  // 2) Partial snapshot cache.
  // 3) Weak references (e.g. the string table).
  void SerializeStrongReferences();
  void SerializeWeakReferencesAndDeferred();

 private:
  // The StartupSerializer has to serialize the root array, which is slightly
  // different.
  void VisitPointers(Object** start, Object** end) override;
  void SerializeObject(HeapObject* o, HowToCode how_to_code,
                       WhereToPoint where_to_point, int skip) override;
  void Synchronize(VisitorSynchronization::SyncTag tag) override;

  intptr_t root_index_wave_front_;
  bool serializing_builtins_;
  DISALLOW_COPY_AND_ASSIGN(StartupSerializer);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_STARTUP_SERIALIZER_H_
