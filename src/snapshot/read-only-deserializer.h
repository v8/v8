// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_READ_ONLY_DESERIALIZER_H_
#define V8_SNAPSHOT_READ_ONLY_DESERIALIZER_H_

#include <vector>

#include "src/snapshot/deserializer.h"

namespace v8 {
namespace internal {

class SnapshotData;

// Describes a range of objects on a page that need post-processing after
// deserialization.
struct PostProcessRange {
  uint32_t page_index;
  uint32_t first_offset;  // Offset from page area_start of first object.
  uint32_t end_offset;    // Offset from page area_start past last object.
};

// Deserializes the read-only blob and creates the read-only roots table.
class ReadOnlyDeserializer final : public Deserializer<Isolate> {
 public:
  ReadOnlyDeserializer(Isolate* isolate, const SnapshotData* data,
                       bool can_rehash);

  void DeserializeIntoIsolate();

 private:
  void PostProcessNewObjects(const std::vector<PostProcessRange>& ranges);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_READ_ONLY_DESERIALIZER_H_
