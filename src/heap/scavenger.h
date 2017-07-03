// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SCAVENGER_H_
#define V8_HEAP_SCAVENGER_H_

#include "src/heap/objects-visiting.h"
#include "src/heap/slot-set.h"

namespace v8 {
namespace internal {

class Scavenger {
 public:
  explicit Scavenger(Heap* heap)
      : heap_(heap), is_logging_(false), is_incremental_marking_(false) {}

  Scavenger(Heap* heap, bool is_logging, bool is_incremental_marking)
      : heap_(heap),
        is_logging_(is_logging),
        is_incremental_marking_(is_incremental_marking) {}

  // Callback function passed to Heap::Iterate etc.  Copies an object if
  // necessary, the object might be promoted to an old space.  The caller must
  // ensure the precondition that the object is (a) a heap object and (b) in
  // the heap's from space.
  inline void ScavengeObject(HeapObject** p, HeapObject* object);

  inline SlotCallbackResult CheckAndScavengeObject(Heap* heap,
                                                   Address slot_address);
  inline Heap* heap() { return heap_; }

 private:
  V8_INLINE HeapObject* MigrateObject(HeapObject* source, HeapObject* target,
                                      int size);

  V8_INLINE bool SemiSpaceCopyObject(Map* map, HeapObject** slot,
                                     HeapObject* object, int object_size);

  V8_INLINE bool PromoteObject(Map* map, HeapObject** slot, HeapObject* object,
                               int object_size);

  V8_INLINE void EvacuateObject(HeapObject** slot, Map* map,
                                HeapObject* source);

  // Different cases for object evacuation.

  V8_INLINE void EvacuateObjectDefault(Map* map, HeapObject** slot,
                                       HeapObject* object, int object_size);

  V8_INLINE void EvacuateJSFunction(Map* map, HeapObject** slot,
                                    JSFunction* object, int object_size);

  inline void EvacuateThinString(Map* map, HeapObject** slot,
                                 ThinString* object, int object_size);

  inline void EvacuateShortcutCandidate(Map* map, HeapObject** slot,
                                        ConsString* object, int object_size);

  void RecordCopiedObject(HeapObject* obj);

  Heap* heap_;
  bool is_logging_;
  bool is_incremental_marking_;
};

// Helper class for turning the scavenger into an object visitor that is also
// filtering out non-HeapObjects and objects which do not reside in new space.
class RootScavengeVisitor final : public RootVisitor {
 public:
  RootScavengeVisitor(Heap* heap, Scavenger* scavenger)
      : heap_(heap), scavenger_(scavenger) {}

  void VisitRootPointer(Root root, Object** p) final;
  void VisitRootPointers(Root root, Object** start, Object** end) final;

 private:
  void ScavengePointer(Object** p);

  Heap* const heap_;
  Scavenger* const scavenger_;
};

class ScavengeVisitor final : public NewSpaceVisitor<ScavengeVisitor> {
 public:
  ScavengeVisitor(Heap* heap, Scavenger* scavenger)
      : heap_(heap), scavenger_(scavenger) {}

  V8_INLINE void VisitPointers(HeapObject* host, Object** start,
                               Object** end) final;

 private:
  Heap* const heap_;
  Scavenger* const scavenger_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SCAVENGER_H_
