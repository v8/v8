// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_EMBEDDER_DATA_SNAPSHOT_H_
#define V8_HEAP_EMBEDDER_DATA_SNAPSHOT_H_

#include "include/v8-cppgc.h"
#include "src/common/globals.h"
#include "src/heap/embedder-tracing.h"
#include "src/objects/js-objects.h"
#include "src/objects/map.h"

namespace v8 {
namespace internal {

// Snapshot for embedder data that is used for concurrently processing embedder
// fields.
//
// The snapshot is used together with a notification for object layout change
// which locks out the concurrent marker from processing embedder fields. This
// is necessary as embedder fields are only aligned for tagged values which
// violates atomicity of a single pointer read and prevents us from using atomic
// operations.
class EmbedderDataSnapshot final {
 public:
  explicit EmbedderDataSnapshot(const WrapperDescriptor wrapper_descriptor)
      : wrapper_descriptor_(wrapper_descriptor),
        last_index_(std::max(wrapper_descriptor_.wrappable_type_index,
                             wrapper_descriptor_.wrappable_instance_index)) {}

  V8_INLINE bool Populate(Map map, JSObject js_object);

  V8_INLINE std::pair<EmbedderDataSlot, EmbedderDataSlot> ExtractWrapperSlots()
      const;

 private:
  static constexpr size_t kTypeIndex = 0;
  static constexpr size_t kInstanceIndex = 1;
  static constexpr size_t kMaxNumTaggedEmbedderSlots =
      JSObject::kMaxEmbedderFields * kEmbedderDataSlotSize / kTaggedSize;

  static_assert(
      kMaxNumTaggedEmbedderSlots < 32,
      "EmbedderDataSnapshot is allocated on stack and should stay small.");

  const WrapperDescriptor wrapper_descriptor_;
  Tagged_t snapshot_[kMaxNumTaggedEmbedderSlots];
  int last_index_;
#ifdef DEBUG
  bool has_valid_snapshot_{false};
#endif  // DEBUG
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_EMBEDDER_DATA_SNAPSHOT_H_
