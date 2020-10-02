// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/marking-state.h"

#include <unordered_set>

namespace cppgc {
namespace internal {

void MarkingState::FlushNotFullyConstructedObjects() {
  std::unordered_set<HeapObjectHeader*> objects =
      not_fully_constructed_worklist_.Extract();
  for (HeapObjectHeader* object : objects) {
    if (MarkNoPush(*object))
      previously_not_fully_constructed_worklist_.Push(object);
  }
  DCHECK(not_fully_constructed_worklist_.IsEmpty());
}

}  // namespace internal
}  // namespace cppgc
