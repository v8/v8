// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/scavenger.h"

#include "src/heap/heap-inl.h"
#include "src/heap/scavenger-inl.h"

namespace v8 {
namespace internal {

void Scavenger::RecordCopiedObject(HeapObject* obj) {
  bool should_record = FLAG_log_gc;
#ifdef DEBUG
  should_record = FLAG_heap_stats;
#endif
  if (should_record) {
    if (heap()->new_space()->Contains(obj)) {
      heap()->new_space()->RecordAllocation(obj);
    } else {
      heap()->new_space()->RecordPromotion(obj);
    }
  }
}

void RootScavengeVisitor::VisitRootPointer(Root root, Object** p) {
  ScavengePointer(p);
}

void RootScavengeVisitor::VisitRootPointers(Root root, Object** start,
                                            Object** end) {
  // Copy all HeapObject pointers in [start, end)
  for (Object** p = start; p < end; p++) ScavengePointer(p);
}

void RootScavengeVisitor::ScavengePointer(Object** p) {
  Object* object = *p;
  if (!heap_->InNewSpace(object)) return;

  scavenger_->ScavengeObject(reinterpret_cast<HeapObject**>(p),
                             reinterpret_cast<HeapObject*>(object));
}

}  // namespace internal
}  // namespace v8
