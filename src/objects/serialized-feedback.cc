// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/serialized-feedback.h"

#include "src/common/assert-scope.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/serialized-feedback-inl.h"

namespace v8 {
namespace internal {

namespace {

// Currently, only smi-based feedback is serialized.
bool IsSerialized(FeedbackSlotKind kind) {
  switch (kind) {
    case FeedbackSlotKind::kBinaryOp:
    case FeedbackSlotKind::kCompareOp:
    case FeedbackSlotKind::kForIn:
      return true;
    case FeedbackSlotKind::kStoreGlobalSloppy:
    case FeedbackSlotKind::kStoreNamedSloppy:
    case FeedbackSlotKind::kStoreKeyedSloppy:
    case FeedbackSlotKind::kCall:
    case FeedbackSlotKind::kLoadProperty:
    case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
    case FeedbackSlotKind::kLoadGlobalInsideTypeof:
    case FeedbackSlotKind::kLoadKeyed:
    case FeedbackSlotKind::kHasKeyed:
    case FeedbackSlotKind::kStoreGlobalStrict:
    case FeedbackSlotKind::kStoreNamedStrict:
    case FeedbackSlotKind::kStoreOwnNamed:
    case FeedbackSlotKind::kStoreKeyedStrict:
    case FeedbackSlotKind::kStoreInArrayLiteral:
    case FeedbackSlotKind::kStoreDataPropertyInLiteral:
    case FeedbackSlotKind::kTypeProfile:
    case FeedbackSlotKind::kLiteral:
    case FeedbackSlotKind::kInstanceOf:
    case FeedbackSlotKind::kCloneObject:
      return false;
    case FeedbackSlotKind::kInvalid:
    case FeedbackSlotKind::kKindsNumber:
      UNREACHABLE();
  }
  UNREACHABLE();
}

constexpr int SlotCountToByteLength(int slot_count) {
  return slot_count * kUInt32Size;
}

constexpr int ByteLengthToSlotCount(int byte_length) {
  CONSTEXPR_DCHECK((byte_length % kUInt32Size) == 0);
  return byte_length / kUInt32Size;
}

}  // namespace

// static
Handle<SerializedFeedback> SerializedFeedback::Serialize(
    Isolate* isolate, Handle<FeedbackVector> vector) {
  Handle<FeedbackMetadata> metadata(vector->metadata(), isolate);

  const int slot_count = metadata->slot_count();
  const int byte_length = SlotCountToByteLength(slot_count);

  // Allocating in old space since these objects are inserted into long-lived
  // caches.
  auto sf = Handle<SerializedFeedback>::cast(
      isolate->factory()->NewByteArray(byte_length, AllocationType::kOld));

  // Initialize all relevant slots.
  for (int i = 0; i < slot_count;) {
    const FeedbackSlot slot{i};
    const FeedbackSlotKind slot_kind = metadata->GetKind(slot);
    const int slot_size = FeedbackMetadata::GetSlotSize(slot_kind);
    if (IsSerialized(slot_kind)) {
      // All handled slot kinds currently use smi-based feedback; these are
      // simply serialized as the value.
      DCHECK_EQ(slot_size, 1);
      const uint32_t value = vector->Get(slot)->ToSmi().value();
      sf->set_uint32(i, value);
    } else {
      // Unhandled slot kinds are zeroed.
      sf->set_uint32(i, 0);
    }
    i += slot_size;
  }

  return sf;
}

void SerializedFeedback::DeserializeInto(FeedbackVector vector) const {
  DisallowGarbageCollection no_gc;
  FeedbackMetadata metadata = vector.metadata();

  const int slot_count = metadata.slot_count();
  CHECK_EQ(slot_count, ByteLengthToSlotCount(length()));

  for (int i = 0; i < slot_count;) {
    const FeedbackSlot slot{i};
    const FeedbackSlotKind slot_kind = metadata.GetKind(slot);
    const int slot_size = FeedbackMetadata::GetSlotSize(slot_kind);
    const uint32_t serialized_value = get_uint32(i);
    if (IsSerialized(slot_kind)) {
      DCHECK_EQ(slot_size, 1);
      DCHECK_EQ(vector.Get(slot)->ToSmi().value(), 0);  // Uninitialized.
      vector.SynchronizedSet(slot, Smi::FromInt(serialized_value),
                             SKIP_WRITE_BARRIER);
      DCHECK_EQ(vector.Get(slot)->ToSmi().value(), serialized_value);
    } else {
      DCHECK_EQ(serialized_value, 0);
    }
    i += slot_size;
  }
}

}  // namespace internal
}  // namespace v8
