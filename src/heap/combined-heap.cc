// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/combined-heap.h"

namespace v8 {
namespace internal {

HeapObject CombinedHeapIterator::next() {
  HeapObject object = heap_iterator_.next();
  if (!object.is_null()) {
    return object;
  }
  return ro_heap_iterator_.next();
}

}  // namespace internal
}  // namespace v8
