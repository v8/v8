// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/isolate.h"

#ifndef V8_SNAPSHOT_H_
#define V8_SNAPSHOT_H_

namespace v8 {
namespace internal {

class Snapshot {
 public:
  // Initialize the Isolate from the internal snapshot. Returns false if no
  // snapshot could be found.
  static bool Initialize(Isolate* isolate);
  // Create a new context using the internal partial snapshot.
  static Handle<Context> NewContextFromSnapshot(Isolate* isolate);

  static const Vector<const byte> StartupSnapshot();
  static const Vector<const byte> ContextSnapshot();
  static bool HaveASnapshotToStartFrom();

  static const v8::StartupData SnapshotBlob();

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Snapshot);
};

#ifdef V8_USE_EXTERNAL_STARTUP_DATA
void SetSnapshotFromFile(StartupData* snapshot_blob);
#endif

} }  // namespace v8::internal

#endif  // V8_SNAPSHOT_H_
