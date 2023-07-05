// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARK_COMPACT_BASE_INL_H_
#define V8_HEAP_MARK_COMPACT_BASE_INL_H_

#include "src/heap/heap-inl.h"
#include "src/heap/mark-compact-base.h"

namespace v8 {
namespace internal {

template <ExternalStringTableCleaningMode mode>
void ExternalStringTableCleaner<mode>::VisitRootPointers(
    Root root, const char* description, FullObjectSlot start,
    FullObjectSlot end) {
  // Visit all HeapObject pointers in [start, end).
  DCHECK_EQ(static_cast<int>(root),
            static_cast<int>(Root::kExternalStringsTable));
  NonAtomicMarkingState* marking_state = heap_->non_atomic_marking_state();
  Object the_hole = ReadOnlyRoots(heap_).the_hole_value();
  for (FullObjectSlot p = start; p < end; ++p) {
    Object o = *p;
    if (!o.IsHeapObject()) continue;
    HeapObject heap_object = HeapObject::cast(o);
    // MinorMS doesn't update the young strings set and so it may contain
    // strings that are already in old space.
    if (!marking_state->IsUnmarked(heap_object)) continue;
    if ((mode == ExternalStringTableCleaningMode::kYoungOnly) &&
        !Heap::InYoungGeneration(heap_object))
      continue;
    if (o.IsExternalString()) {
      heap_->FinalizeExternalString(String::cast(o));
    } else {
      // The original external string may have been internalized.
      DCHECK(o.IsThinString());
    }
    // Set the entry to the_hole_value (as deleted).
    p.store(the_hole);
  }
}

Isolate* MarkCompactCollectorBase::isolate() const { return heap()->isolate(); }

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARK_COMPACT_BASE_INL_H_
