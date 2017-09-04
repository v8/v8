// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_BUILTIN_DESERIALIZER_H_
#define V8_SNAPSHOT_BUILTIN_DESERIALIZER_H_

#include "src/heap/heap.h"
#include "src/snapshot/deserializer.h"

namespace v8 {
namespace internal {

class BuiltinSnapshotData;

// Deserializes the builtins blob.
class BuiltinDeserializer final : public Deserializer {
 public:
  explicit BuiltinDeserializer(const BuiltinSnapshotData* data);

  // Expose Deserializer::Initialize.
  using Deserializer::Initialize;

  // Builtins deserialization is tightly integrated with deserialization of the
  // startup blob. In particular, we need to ensure that no GC can occur
  // between startup- and builtins deserialization, as all builtins have been
  // pre-allocated and their pointers may not be invalidated.
  void DeserializeEagerBuiltins();
  Code* DeserializeBuiltin(int builtin_id);

  // These methods are used to pre-allocate builtin objects prior to
  // deserialization.
  // TODO(jgruber): Refactor reservation/allocation logic in deserializers to
  // make this less messy.
  Heap::Reservation CreateReservationsForEagerBuiltins();
  void InitializeBuiltinsTable(const Heap::Reservation& reservation);

 private:
  // Extracts the size builtin Code objects (baked into the snapshot).
  uint32_t ExtractBuiltinSize(int builtin_id);
  std::vector<uint32_t> ExtractBuiltinSizes();

  // Allocation works differently here than in other deserializers. Instead of
  // a statically-known memory area determined at serialization-time, our
  // memory requirements here are determined at runtime. Another major
  // difference is that we create builtin Code objects up-front (before
  // deserialization) in order to avoid having to patch builtin references
  // later on. See also the kBuiltin case in deserializer.cc.
  //
  // Allocate simply returns the pre-allocated object prepared by
  // InitializeBuiltinsTable.
  Address Allocate(int space_index, int size) override;

  // BuiltinDeserializer implements its own builtin iteration logic. Make sure
  // the RootVisitor API is not used accidentally.
  void VisitRootPointers(Root root, Object** start, Object** end) override {
    UNREACHABLE();
  }

  // Stores the builtin currently being deserialized. We need this to determine
  // where to 'allocate' from during deserialization.
  static const int kNoBuiltinId = -1;
  int current_builtin_id_ = kNoBuiltinId;

  // The sizes of each builtin Code object in its deserialized state. This list
  // is used to determine required space prior to deserialization.
  std::vector<uint32_t> builtin_sizes_;

  // The offsets of each builtin within the serialized data. Equivalent to
  // BuiltinSerializer::builtin_offsets_ but on the deserialization side.
  Vector<const uint32_t> builtin_offsets_;

  friend class DeserializingBuiltinScope;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_BUILTIN_DESERIALIZER_H_
