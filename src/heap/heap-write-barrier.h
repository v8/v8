// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_WRITE_BARRIER_H_
#define V8_HEAP_HEAP_WRITE_BARRIER_H_

namespace v8 {
namespace internal {

class FixedArray;
class Heap;
class HeapObject;
class MaybeObject;
class Object;

// Note: In general it is preferred to use the macros defined in
// object-macros.h.

// Write barrier for FixedArray elements.
#define FIXED_ARRAY_ELEMENTS_WRITE_BARRIER(heap, array, start, length) \
  do {                                                                 \
    GenerationalBarrierForElements(heap, array, start, length);        \
    MarkingBarrierForElements(heap, array);                            \
  } while (false)

// Generational write barrier.
void GenerationalBarrier(HeapObject* object, Object** slot, Object* value);
void GenerationalBarrier(HeapObject* object, MaybeObject** slot,
                         MaybeObject* value);
void GenerationalBarrierForElements(Heap* heap, FixedArray* array, int offset,
                                    int length);

// Marking write barrier.
void MarkingBarrier(HeapObject* object, Object** slot, Object* value);
void MarkingBarrier(HeapObject* object, MaybeObject** slot, MaybeObject* value);
void MarkingBarrierForElements(Heap* heap, HeapObject* object);

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HEAP_WRITE_BARRIER_H_
