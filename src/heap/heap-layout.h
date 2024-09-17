// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_LAYOUT_H_
#define V8_HEAP_HEAP_LAYOUT_H_

// Clients of this interface shouldn't depend on lots of heap internals.
// Do not include anything from src/heap here!

#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/objects/tagged.h"

namespace v8::internal {

// Checks for heap layouts. The checks generally use Heap infrastructure (heap,
// space, page, mark bits, etc) and do not rely on instance types.
class HeapLayout final : public AllStatic {
 public:
  // Returns whether `object` is part of a read-only space.
  static V8_INLINE bool InReadOnlySpace(Tagged<HeapObject> object);

  // Returns whether `object` is in a writable shared space. The is agnostic to
  // how the shared space itself is managed.
  static V8_INLINE bool InWritableSharedSpace(Tagged<HeapObject> object);
  // Returns whether `object` is in a shared space.
  static V8_INLINE bool InAnySharedSpace(Tagged<HeapObject> object);
};

}  // namespace v8::internal

#endif  // V8_HEAP_HEAP_LAYOUT_H_
