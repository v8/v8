// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/heap-layout-inl.h"
#include "src/heap/marking-inl.h"

namespace v8::internal {

// TODO(333906585): Due to cyclic dependency, we cannot pull in marking-inl.h
// here. Fix it and make the call inlined.
bool HeapLayout::InYoungGenerationForStickyMarkbits(const MemoryChunk* chunk,
                                                    Tagged<HeapObject> object) {
  CHECK(v8_flags.sticky_mark_bits.value());
  return !chunk->IsOnlyOldOrMajorMarkingOn() &&
         !MarkingBitmap::MarkBitFromAddress(object.address())
              .template Get<AccessMode::ATOMIC>();
}

}  // namespace v8::internal
