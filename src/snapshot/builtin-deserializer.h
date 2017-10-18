// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_BUILTIN_DESERIALIZER_H_
#define V8_SNAPSHOT_BUILTIN_DESERIALIZER_H_

#include "src/snapshot/builtin-deserializer-allocator.h"
#include "src/snapshot/deserializer.h"

namespace v8 {
namespace internal {

class BuiltinSnapshotData;

// Deserializes the builtins blob.
class BuiltinDeserializer final
    : public Deserializer<BuiltinDeserializerAllocator> {
 public:
  BuiltinDeserializer(Isolate* isolate, const BuiltinSnapshotData* data);

  // Builtins deserialization is tightly integrated with deserialization of the
  // startup blob. In particular, we need to ensure that no GC can occur
  // between startup- and builtins deserialization, as all builtins have been
  // pre-allocated and their pointers may not be invalidated.
  //
  // After this, the instruction cache must be flushed by the caller (we don't
  // do it ourselves since the startup serializer batch-flushes all code pages).
  void DeserializeEagerBuiltins();

  // Deserializes the single given builtin. This is used whenever a builtin is
  // lazily deserialized at runtime.
  Code* DeserializeBuiltin(int builtin_id);

 private:
  // Deserializes the single given builtin. Assumes that reservations have
  // already been allocated.
  Code* DeserializeBuiltinRaw(int builtin_id);


  // Extracts the size builtin Code objects (baked into the snapshot).
  uint32_t ExtractBuiltinSize(int builtin_id);

  // BuiltinDeserializer implements its own builtin iteration logic. Make sure
  // the RootVisitor API is not used accidentally.
  void VisitRootPointers(Root root, Object** start, Object** end) override {
    UNREACHABLE();
  }

  int CurrentBuiltinId() const { return current_builtin_id_; }

 private:
  // Stores the builtin currently being deserialized. We need this to determine
  // where to 'allocate' from during deserialization.
  static const int kNoBuiltinId = -1;
  int current_builtin_id_ = kNoBuiltinId;

  // The offsets of each builtin within the serialized data. Equivalent to
  // BuiltinSerializer::builtin_offsets_ but on the deserialization side.
  Vector<const uint32_t> builtin_offsets_;

  // For current_builtin_id_.
  friend class DeserializingBuiltinScope;

  // For isolate(), IsLazyDeserializationEnabled(), CurrentBuiltinId() and
  // ExtractBuiltinSize().
  friend class BuiltinDeserializerAllocator;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_BUILTIN_DESERIALIZER_H_
