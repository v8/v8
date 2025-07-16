// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_ISOLATE_CURRENT_H_
#define V8_EXECUTION_ISOLATE_CURRENT_H_

// This header is needed to break cyclic header dependencies:
// MemoryChunk::Metadata() needs Isolate::Current(), but cannot include
// isolate-inl.h, which includes memory-chunk-inl.h.

#include "src/execution/isolate.h"

namespace v8::internal {

// static
V8_INLINE Isolate* Isolate::Current() {
  Isolate* isolate = TryGetCurrent();
  DCHECK_NOT_NULL(isolate);
  return isolate;
}

bool Isolate::IsCurrent() const { return this == TryGetCurrent(); }

}  // namespace v8::internal

#endif  // V8_EXECUTION_ISOLATE_CURRENT_H_
