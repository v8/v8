// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used for building with external snapshots.

#include "src/snapshot.h"

#include "src/serialize.h"
#include "src/snapshot-source-sink.h"
#include "src/v8.h"  // for V8::Initialize

namespace v8 {
namespace internal {


struct SnapshotImpl {
 public:
  const byte* data;
  int size;
  const byte* context_data;
  int context_size;
};


static SnapshotImpl* snapshot_impl_ = NULL;


bool Snapshot::HaveASnapshotToStartFrom() {
  return snapshot_impl_ != NULL;
}


bool Snapshot::Initialize(Isolate* isolate) {
  if (!HaveASnapshotToStartFrom()) return false;
  base::ElapsedTimer timer;
  if (FLAG_profile_deserialization) timer.Start();


  SnapshotData snapshot_data(snapshot_impl_->data, snapshot_impl_->size);
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

  SnapshotData snapshot_data(snapshot_impl_->context_data,
                             snapshot_impl_->context_size);
  Deserializer deserializer(&snapshot_data);
  Object* root;
  deserializer.DeserializePartial(isolate, &root);
  CHECK(root->IsContext());
  return Handle<Context>(Context::cast(root));
}


void SetSnapshotFromFile(StartupData* snapshot_blob) {
  DCHECK(snapshot_blob);
  DCHECK(snapshot_blob->data);
  DCHECK(snapshot_blob->raw_size > 0);
  DCHECK(!snapshot_impl_);

  snapshot_impl_ = new SnapshotImpl;
  SnapshotByteSource source(reinterpret_cast<const byte*>(snapshot_blob->data),
                            snapshot_blob->raw_size);
  bool success = source.GetBlob(&snapshot_impl_->data,
                                &snapshot_impl_->size);
  success &= source.GetBlob(&snapshot_impl_->context_data,
                            &snapshot_impl_->context_size);
  DCHECK(success);
}

} }  // namespace v8::internal
