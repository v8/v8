// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used for building without snapshots.

#include "src/v8.h"

#include "src/snapshot.h"

namespace v8 {
namespace internal {

const byte Snapshot::data_[] = { 0 };
const int Snapshot::size_ = 0;
const byte Snapshot::context_data_[] = { 0 };
const int Snapshot::context_size_ = 0;

} }  // namespace v8::internal
