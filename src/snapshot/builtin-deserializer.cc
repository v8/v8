// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/builtin-deserializer.h"

#include "src/snapshot/snapshot.h"

namespace v8 {
namespace internal {

BuiltinDeserializer::BuiltinDeserializer(const BuiltinSnapshotData* data)
    : Deserializer(data, false) {
  builtin_offsets_ = data->BuiltinOffsets();
  DCHECK(std::is_sorted(builtin_offsets_.begin(), builtin_offsets_.end()));
}

void BuiltinDeserializer::DeserializeAllBuiltins() {
  DCHECK(!AllowHeapAllocation::IsAllowed());

  isolate()->builtins()->IterateBuiltins(this);
  PostProcessDeferredBuiltinReferences();
}

}  // namespace internal
}  // namespace v8
