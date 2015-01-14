// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The common functionality when building with or without snapshots.

#include "src/v8.h"

#include "src/api.h"
#include "src/base/platform/platform.h"
#include "src/serialize.h"
#include "src/snapshot.h"

namespace v8 {
namespace internal {

bool Snapshot::HaveASnapshotToStartFrom() {
  return SnapshotBlob().data != NULL;
}


#ifdef DEBUG
bool Snapshot::SnapshotIsValid(v8::StartupData* snapshot_blob) {
  return !Snapshot::ExtractStartupData(snapshot_blob).is_empty() &&
         !Snapshot::ExtractContextData(snapshot_blob).is_empty();
}
#endif  // DEBUG


bool Snapshot::EmbedsScript() {
  if (!HaveASnapshotToStartFrom()) return false;
  const v8::StartupData blob = SnapshotBlob();
  return ExtractMetadata(&blob).embeds_script();
}


bool Snapshot::Initialize(Isolate* isolate) {
  if (!HaveASnapshotToStartFrom()) return false;
  base::ElapsedTimer timer;
  if (FLAG_profile_deserialization) timer.Start();

  const v8::StartupData blob = SnapshotBlob();
  Vector<const byte> startup_data = ExtractStartupData(&blob);
  SnapshotData snapshot_data(startup_data);
  Deserializer deserializer(&snapshot_data);
  bool success = isolate->Init(&deserializer);
  if (FLAG_profile_deserialization) {
    double ms = timer.Elapsed().InMillisecondsF();
    int bytes = startup_data.length();
    PrintF("[Deserializing isolate (%d bytes) took %0.3f ms]\n", bytes, ms);
  }
  return success;
}


MaybeHandle<Context> Snapshot::NewContextFromSnapshot(
    Isolate* isolate, Handle<JSGlobalProxy> global_proxy,
    Handle<FixedArray>* outdated_contexts_out) {
  if (!HaveASnapshotToStartFrom()) return Handle<Context>();
  base::ElapsedTimer timer;
  if (FLAG_profile_deserialization) timer.Start();

  const v8::StartupData blob = SnapshotBlob();
  Vector<const byte> context_data = ExtractContextData(&blob);
  SnapshotData snapshot_data(context_data);
  Deserializer deserializer(&snapshot_data);

  MaybeHandle<Object> maybe_context = deserializer.DeserializePartial(
      isolate, global_proxy, outdated_contexts_out);
  Handle<Object> result;
  if (!maybe_context.ToHandle(&result)) return MaybeHandle<Context>();
  CHECK(result->IsContext());
  // If the snapshot does not contain a custom script, we need to update
  // the global object for exactly one context.
  CHECK(EmbedsScript() || (*outdated_contexts_out)->length() == 1);
  if (FLAG_profile_deserialization) {
    double ms = timer.Elapsed().InMillisecondsF();
    int bytes = context_data.length();
    PrintF("[Deserializing context (%d bytes) took %0.3f ms]\n", bytes, ms);
  }
  return Handle<Context>::cast(result);
}


v8::StartupData Snapshot::CreateSnapshotBlob(
    const Vector<const byte> startup_data,
    const Vector<const byte> context_data, Snapshot::Metadata metadata) {
  int startup_length = startup_data.length();
  int context_length = context_data.length();
  int context_offset = ContextOffset(startup_length);

  int length = context_offset + context_length;
  char* data = new char[length];

  memcpy(data + kMetadataOffset, &metadata.RawValue(), kInt32Size);
  memcpy(data + kStartupLengthOffset, &startup_length, kInt32Size);
  memcpy(data + kStartupDataOffset, startup_data.begin(), startup_length);
  memcpy(data + context_offset, context_data.begin(), context_length);
  v8::StartupData result = {data, length};
  return result;
}


Snapshot::Metadata Snapshot::ExtractMetadata(const v8::StartupData* data) {
  uint32_t raw;
  memcpy(&raw, data->data + kMetadataOffset, kInt32Size);
  return Metadata(raw);
}


Vector<const byte> Snapshot::ExtractStartupData(const v8::StartupData* data) {
  DCHECK_LT(kIntSize, data->raw_size);
  int startup_length;
  memcpy(&startup_length, data->data + kStartupLengthOffset, kInt32Size);
  DCHECK_LT(startup_length, data->raw_size);
  const byte* startup_data =
      reinterpret_cast<const byte*>(data->data + kStartupDataOffset);
  return Vector<const byte>(startup_data, startup_length);
}


Vector<const byte> Snapshot::ExtractContextData(const v8::StartupData* data) {
  DCHECK_LT(kIntSize, data->raw_size);
  int startup_length;
  memcpy(&startup_length, data->data + kStartupLengthOffset, kIntSize);
  int context_offset = ContextOffset(startup_length);
  const byte* context_data =
      reinterpret_cast<const byte*>(data->data + context_offset);
  DCHECK_LT(context_offset, data->raw_size);
  int context_length = data->raw_size - context_offset;
  return Vector<const byte>(context_data, context_length);
}
} }  // namespace v8::internal
