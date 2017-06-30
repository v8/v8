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

  V8_INLINE void EvacuateObject(HeapObject** slot, Map* map,
                                HeapObject* source);

  // Callback function passed to Heap::Iterate etc.  Copies an object if
  // necessary, the object might be promoted to an old space.  The caller must
  // ensure the precondition that the object is (a) a heap object and (b) in
  // the heap's from space.
  static inline void ScavengeObject(HeapObject** p, HeapObject* object);
  static inline SlotCallbackResult CheckAndScavengeObject(Heap* heap,
                                                          Address slot_address);

  // Slow part of {ScavengeObject} above.
  static inline void ScavengeObjectSlow(HeapObject** p, HeapObject* object);

  void UpdateConstraints();

  Isolate* isolate();
  Heap* heap() { return heap_; }

 private:
  // White list for objects that for sure only contain data.
  V8_INLINE static bool ContainsOnlyData(VisitorId visitor_id);

  void RecordCopiedObject(HeapObject* obj);

  V8_INLINE HeapObject* MigrateObject(HeapObject* source, HeapObject* target,
                                      int size);

  V8_INLINE bool SemiSpaceCopyObject(Map* map, HeapObject** slot,
                                     HeapObject* object, int object_size);

  V8_INLINE bool PromoteObject(Map* map, HeapObject** slot, HeapObject* object,
                               int object_size);

  V8_INLINE void EvacuateObjectDefault(Map* map, HeapObject** slot,
                                       HeapObject* object, int object_size);

  // Special cases.

  V8_INLINE void EvacuateJSFunction(Map* map, HeapObject** slot,
                                    JSFunction* object, int object_size);

  V8_INLINE void EvacuateThinString(Map* map, HeapObject** slot,
                                    ThinString* object, int object_size);

  V8_INLINE void EvacuateShortcutCandidate(Map* map, HeapObject** slot,
                                           ConsString* object, int object_size);

  Heap* heap_;
  bool is_logging_;
  bool is_incremental_marking_;
};

// Helper class for turning the scavenger into an object visitor that is also
// filtering out non-HeapObjects and objects which do not reside in new space.
class RootScavengeVisitor : public RootVisitor {
 public:
  explicit RootScavengeVisitor(Heap* heap) : heap_(heap) {}

  void VisitRootPointer(Root root, Object** p) override;
  void VisitRootPointers(Root root, Object** start, Object** end) override;

 private:
  inline void ScavengePointer(Object** p);

  Heap* heap_;
};

class ScavengeVisitor final : public NewSpaceVisitor<ScavengeVisitor> {
 public:
  explicit ScavengeVisitor(Heap* heap) : heap_(heap) {}
  V8_INLINE void VisitPointers(HeapObject* host, Object** start,
                               Object** end) final;

 private:
  Heap* heap_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SCAVENGER_H_
