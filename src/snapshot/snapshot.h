// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_SNAPSHOT_H_
#define V8_SNAPSHOT_SNAPSHOT_H_

#include "src/snapshot/partial-serializer.h"
#include "src/snapshot/startup-serializer.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Isolate;
class PartialSerializer;
class StartupSerializer;

class Snapshot : public AllStatic {
 public:
  // Initialize the Isolate from the internal snapshot. Returns false if no
  // snapshot could be found.
  static bool Initialize(Isolate* isolate);
  // Create a new context using the internal partial snapshot.
  static MaybeHandle<Context> NewContextFromSnapshot(
      Isolate* isolate, Handle<JSGlobalProxy> global_proxy);

  static bool HaveASnapshotToStartFrom(Isolate* isolate);

  static bool EmbedsScript(Isolate* isolate);

  static uint32_t SizeOfFirstPage(Isolate* isolate, AllocationSpace space);


  // To be implemented by the snapshot source.
  static const v8::StartupData* DefaultSnapshotBlob();

  static v8::StartupData CreateSnapshotBlob(
      const StartupSerializer* startup_serializer,
      const PartialSerializer* context_serializer);

#ifdef DEBUG
  static bool SnapshotIsValid(v8::StartupData* snapshot_blob);
#endif  // DEBUG

 private:
  static Vector<const byte> ExtractStartupData(const v8::StartupData* data);
  static Vector<const byte> ExtractContextData(const v8::StartupData* data);

  // Snapshot blob layout:
  // [0 - 5] pre-calculated first page sizes for paged spaces
  // [6] serialized start up data length
  // ... serialized start up data
  // ... serialized context data

  static const int kNumPagedSpaces = LAST_PAGED_SPACE - FIRST_PAGED_SPACE + 1;

  static const int kFirstPageSizesOffset = 0;
  static const int kStartupLengthOffset =
      kFirstPageSizesOffset + kNumPagedSpaces * kInt32Size;
  static const int kStartupDataOffset = kStartupLengthOffset + kInt32Size;

  static int ContextOffset(int startup_length) {
    return kStartupDataOffset + startup_length;
  }

  DISALLOW_IMPLICIT_CONSTRUCTORS(Snapshot);
};

#ifdef V8_USE_EXTERNAL_STARTUP_DATA
void SetSnapshotFromFile(StartupData* snapshot_blob);
#endif

// Wrapper around reservation sizes and the serialization payload.
class SnapshotData : public SerializedData {
 public:
  // Used when producing.
  explicit SnapshotData(const Serializer* serializer);

  // Used when consuming.
  explicit SnapshotData(const Vector<const byte> snapshot)
      : SerializedData(const_cast<byte*>(snapshot.begin()), snapshot.length()) {
    CHECK(IsSane());
  }

  Vector<const Reservation> Reservations() const;
  Vector<const byte> Payload() const;

  Vector<const byte> RawData() const {
    return Vector<const byte>(data_, size_);
  }

 private:
  bool IsSane();

  // The data header consists of uint32_t-sized entries:
  // [0] magic number and external reference count
  // [1] version hash
  // [2] number of reservation size entries
  // [3] payload length
  // ... reservations
  // ... serialized payload
  static const int kCheckSumOffset = kMagicNumberOffset + kInt32Size;
  static const int kNumReservationsOffset = kCheckSumOffset + kInt32Size;
  static const int kPayloadLengthOffset = kNumReservationsOffset + kInt32Size;
  static const int kHeaderSize = kPayloadLengthOffset + kInt32Size;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_SNAPSHOT_H_
