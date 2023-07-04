// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARKING_STATE_INL_H_
#define V8_HEAP_MARKING_STATE_INL_H_

#include "src/heap/marking-inl.h"
#include "src/heap/marking-state.h"
#include "src/heap/memory-chunk.h"

namespace v8 {
namespace internal {

template <typename ConcreteState, AccessMode access_mode>
MarkBit MarkingStateBase<ConcreteState, access_mode>::MarkBitFrom(
    const HeapObject obj) const {
  return MarkBitFrom(BasicMemoryChunk::FromHeapObject(obj), obj.ptr());
}

template <typename ConcreteState, AccessMode access_mode>
MarkBit MarkingStateBase<ConcreteState, access_mode>::MarkBitFrom(
    const BasicMemoryChunk* p, Address addr) const {
  return MarkBit::From(addr);
}

template <typename ConcreteState, AccessMode access_mode>
bool MarkingStateBase<ConcreteState, access_mode>::IsMarked(
    const HeapObject obj) const {
  return MarkBitFrom(obj).template Get<access_mode>();
}

template <typename ConcreteState, AccessMode access_mode>
bool MarkingStateBase<ConcreteState, access_mode>::IsUnmarked(
    const HeapObject obj) const {
  return !IsMarked(obj);
}

template <typename ConcreteState, AccessMode access_mode>
bool MarkingStateBase<ConcreteState, access_mode>::TryMark(HeapObject obj) {
  return MarkBitFrom(obj).template Set<access_mode>();
}

template <typename ConcreteState, AccessMode access_mode>
bool MarkingStateBase<ConcreteState, access_mode>::TryMarkAndAccountLiveBytes(
    HeapObject obj) {
  if (TryMark(obj)) {
    MemoryChunk::FromHeapObject(obj)->IncrementLiveBytesAtomically(
        ALIGN_TO_ALLOCATION_ALIGNMENT(obj.Size(cage_base())));
    return true;
  }
  return false;
}

template <typename ConcreteState, AccessMode access_mode>
void MarkingStateBase<ConcreteState, access_mode>::ClearLiveness(
    MemoryChunk* chunk) {
  static_cast<ConcreteState*>(this)
      ->bitmap(chunk)
      ->template Clear<access_mode>();
  chunk->SetLiveBytes(0);
}

MarkingBitmap* MarkingState::bitmap(MemoryChunk* chunk) const {
  return chunk->marking_bitmap();
}

MarkingBitmap* NonAtomicMarkingState::bitmap(MemoryChunk* chunk) const {
  return chunk->marking_bitmap();
}

MarkingBitmap* AtomicMarkingState::bitmap(MemoryChunk* chunk) const {
  return chunk->marking_bitmap();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARKING_STATE_INL_H_
