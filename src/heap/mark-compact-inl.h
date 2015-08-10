// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARK_COMPACT_INL_H_
#define V8_HEAP_MARK_COMPACT_INL_H_

#include "src/heap/mark-compact.h"
#include "src/isolate.h"


namespace v8 {
namespace internal {


void MarkCompactCollector::SetFlags(int flags) {
  reduce_memory_footprint_ = ((flags & Heap::kReduceMemoryFootprintMask) != 0);
  abort_incremental_marking_ =
      ((flags & Heap::kAbortIncrementalMarkingMask) != 0);
  finalize_incremental_marking_ =
      ((flags & Heap::kFinalizeIncrementalMarkingMask) != 0);
  DCHECK(!finalize_incremental_marking_ || !abort_incremental_marking_);
}


void MarkCompactCollector::MarkObject(HeapObject* obj, MarkBit mark_bit) {
  DCHECK(Marking::MarkBitFrom(obj) == mark_bit);
  if (Marking::IsWhite(mark_bit)) {
    Marking::WhiteToBlack(mark_bit);
    MemoryChunk::IncrementLiveBytesFromGC(obj, obj->Size());
    DCHECK(obj->GetIsolate()->heap()->Contains(obj));
    marking_deque_.PushBlack(obj);
  }
}


void MarkCompactCollector::SetMark(HeapObject* obj, MarkBit mark_bit) {
  DCHECK(Marking::IsWhite(mark_bit));
  DCHECK(Marking::MarkBitFrom(obj) == mark_bit);
  Marking::WhiteToBlack(mark_bit);
  MemoryChunk::IncrementLiveBytesFromGC(obj, obj->Size());
}


bool MarkCompactCollector::IsMarked(Object* obj) {
  DCHECK(obj->IsHeapObject());
  HeapObject* heap_object = HeapObject::cast(obj);
  return Marking::IsBlackOrGrey(Marking::MarkBitFrom(heap_object));
}


void MarkCompactCollector::RecordSlot(HeapObject* object, Object** slot,
                                      Object* target,
                                      SlotsBuffer::AdditionMode mode) {
  Page* target_page = Page::FromAddress(reinterpret_cast<Address>(target));
  if (target_page->IsEvacuationCandidate() &&
      !ShouldSkipEvacuationSlotRecording(object)) {
    if (!SlotsBuffer::AddTo(&slots_buffer_allocator_,
                            target_page->slots_buffer_address(), slot, mode)) {
      EvictPopularEvacuationCandidate(target_page);
    }
  }
}
}
}  // namespace v8::internal

#endif  // V8_HEAP_MARK_COMPACT_INL_H_
