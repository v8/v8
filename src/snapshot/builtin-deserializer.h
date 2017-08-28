// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_BUILTIN_DESERIALIZER_H_
#define V8_SNAPSHOT_BUILTIN_DESERIALIZER_H_

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
  // between startup- and builtins deserialization, as all existing builtin
  // references need to be fixed up after builtins have been deserialized.
  // Thus this quirky two-sided API: required memory needs to be reserved
  // pre-startup deserialization, and builtins must be deserialized at exactly
  // the right point during startup deserialization.
  void DeserializeAllBuiltins();

 private:
  // The offsets of each builtin within the serialized data. Equivalent to
  // BuiltinSerializer::builtin_offsets_ but on the deserialization side.
  Vector<const uint32_t> builtin_offsets_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_BUILTIN_DESERIALIZER_H_
