// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_EMBEDDER_DATA_SNAPSHOT_INL_H_
#define V8_HEAP_EMBEDDER_DATA_SNAPSHOT_INL_H_

#include "src/common/globals.h"
#include "src/heap/embedder-data-snapshot.h"
#include "src/objects/embedder-data-slot-inl.h"
#include "src/objects/js-objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

bool EmbedderDataSnapshot::Populate(Map map, JSObject js_object) {
  if (JSObject::GetEmbedderFieldCount(map) < 2) {
#ifdef DEBUG
    has_valid_snapshot_ = false;
#endif  // DEBUG
    return false;
  }

  // Tracing only requires the first two embedder fields. Avoid taking a
  // snapshot of the other data.
  Address start_address =
      FIELD_ADDR(js_object, JSObject::GetEmbedderFieldsStartOffset(map));
  DCHECK_LE(last_index_, kMaxNumTaggedEmbedderSlots);
  int end_offset = (last_index_ + 1) * kEmbedderDataSlotSize;
  DCHECK_EQ(0, start_address % kTaggedSize);
  DCHECK_EQ(0, end_offset % kTaggedSize);
  for (int i = 0; i < end_offset / kTaggedSize; i++) {
    snapshot_[i] = AsAtomicTagged::Relaxed_Load(
        reinterpret_cast<AtomicTagged_t*>(start_address) + i);
  }

#ifdef DEBUG
  has_valid_snapshot_ = true;
#endif  // DEBUG
  return true;
}

V8_INLINE std::pair<EmbedderDataSlot, EmbedderDataSlot>
EmbedderDataSnapshot::ExtractWrapperSlots() const {
  DCHECK(has_valid_snapshot_);

  static constexpr size_t kTaggedSlotsPerEmbedderSlot =
      kEmbedderDataSlotSize / kTaggedSize;

  return std::make_pair(
      EmbedderDataSlot(reinterpret_cast<Address>(
          &snapshot_[kTaggedSlotsPerEmbedderSlot *
                     wrapper_descriptor_.wrappable_type_index])),
      EmbedderDataSlot(reinterpret_cast<Address>(
          &snapshot_[kTaggedSlotsPerEmbedderSlot *
                     wrapper_descriptor_.wrappable_instance_index])));
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_HEAP_EMBEDDER_DATA_SNAPSHOT_INL_H_
