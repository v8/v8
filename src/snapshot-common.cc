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


const Vector<const byte> Snapshot::StartupSnapshot() {
  DCHECK(HaveASnapshotToStartFrom());
  const v8::StartupData blob = SnapshotBlob();
  SnapshotByteSource source(blob.data, blob.raw_size);
  const byte* data;
  int length;
  bool success = source.GetBlob(&data, &length);
  CHECK(success);
  return Vector<const byte>(data, length);
}


const Vector<const byte> Snapshot::ContextSnapshot() {
  DCHECK(HaveASnapshotToStartFrom());
  const v8::StartupData blob = SnapshotBlob();
  SnapshotByteSource source(blob.data, blob.raw_size);
  const byte* data;
  int length;
  bool success = source.GetBlob(&data, &length);
  success &= source.GetBlob(&data, &length);
  CHECK(success);
  return Vector<const byte>(data, length);
}


bool Snapshot::Initialize(Isolate* isolate) {
  if (!HaveASnapshotToStartFrom()) return false;
  base::ElapsedTimer timer;
  if (FLAG_profile_deserialization) timer.Start();

  SnapshotData snapshot_data(StartupSnapshot());
  Deserializer deserializer(&snapshot_data);
  bool success = isolate->Init(&deserializer);
  if (FLAG_profile_deserialization) {
    double ms = timer.Elapsed().InMillisecondsF();
    PrintF("[Snapshot loading and deserialization took %0.3f ms]\n", ms);
  }
  return success;
}


Handle<Context> Snapshot::NewContextFromSnapshot(Isolate* isolate) {
  if (!HaveASnapshotToStartFrom()) return Handle<Context>();

  SnapshotData snapshot_data(ContextSnapshot());
  Deserializer deserializer(&snapshot_data);
  Object* root;
  deserializer.DeserializePartial(isolate, &root);
  CHECK(root->IsContext());
  return Handle<Context>(Context::cast(root));
}

} }  // namespace v8::internal
