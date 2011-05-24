// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "incremental-marking.h"

#include "code-stubs.h"

namespace v8 {
namespace internal {


IncrementalMarking::IncrementalMarking(Heap* heap)
    : heap_(heap),
      state_(STOPPED),
      steps_count_(0),
      steps_took_(0),
      should_hurry_(false),
      allocation_marking_factor_(0),
      allocated_(0) {
}


void IncrementalMarking::RecordWriteFromCode(HeapObject* obj,
                                             Object* value,
                                             Isolate* isolate) {
  isolate->heap()->incremental_marking()->RecordWrite(obj, value);
}


class IncrementalMarkingMarkingVisitor : public ObjectVisitor {
 public:
  IncrementalMarkingMarkingVisitor(Heap* heap,
                                   IncrementalMarking* incremental_marking)
      : heap_(heap),
        incremental_marking_(incremental_marking) {
  }

  void VisitPointer(Object** p) {
    MarkObjectByPointer(p);
  }

  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) MarkObjectByPointer(p);
  }

 private:
  // Mark object pointed to by p.
  INLINE(void MarkObjectByPointer(Object** p)) {
    Object* obj = *p;
    // Since we can be sure that the object is not tagged as a failure we can
    // inline a slightly more efficient tag check here than IsHeapObject() would
    // produce.
    if (obj->NonFailureIsHeapObject()) {
      HeapObject* heap_object = HeapObject::cast(obj);
      MarkBit mark_bit = Marking::MarkBitFrom(heap_object);
      if (mark_bit.data_only()) {
        incremental_marking_->MarkBlackOrKeepGrey(mark_bit);
      } else {
        if (Marking::IsWhite(mark_bit)) {
          incremental_marking_->WhiteToGreyAndPush(heap_object, mark_bit);
        }
      }
    }
  }

  Heap* heap_;
  IncrementalMarking* incremental_marking_;
};


class IncrementalMarkingRootMarkingVisitor : public ObjectVisitor {
 public:
  IncrementalMarkingRootMarkingVisitor(Heap* heap,
                                       IncrementalMarking* incremental_marking)
      : heap_(heap),
        incremental_marking_(incremental_marking) {
  }

  void VisitPointer(Object** p) {
    MarkObjectByPointer(p);
  }

  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) MarkObjectByPointer(p);
  }

 private:
  void MarkObjectByPointer(Object** p) {
    Object* obj = *p;
    if (!obj->IsHeapObject()) return;

    HeapObject* heap_object = HeapObject::cast(obj);
    MarkBit mark_bit = Marking::MarkBitFrom(heap_object);
    if (mark_bit.data_only()) {
      incremental_marking_->MarkBlackOrKeepGrey(mark_bit);
    } else {
      if (Marking::IsWhite(mark_bit)) {
        incremental_marking_->WhiteToGreyAndPush(heap_object, mark_bit);
      }
    }
  }

  Heap* heap_;
  IncrementalMarking* incremental_marking_;
};


void IncrementalMarking::SetOldSpacePageFlags(MemoryChunk* chunk,
                                              bool is_marking) {
  if (is_marking) {
    chunk->SetFlag(MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING);
    chunk->SetFlag(MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING);
  } else if (chunk->owner()->identity() == CELL_SPACE ||
             chunk->scan_on_scavenge()) {
    chunk->ClearFlag(MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING);
    chunk->ClearFlag(MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING);
  } else {
    chunk->ClearFlag(MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING);
    chunk->SetFlag(MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING);
  }
}


void IncrementalMarking::SetNewSpacePageFlags(MemoryChunk* chunk,
                                              bool is_marking) {
  chunk->SetFlag(MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING);
  if (is_marking) {
    chunk->SetFlag(MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING);
  } else {
    chunk->ClearFlag(MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING);
  }
}


void IncrementalMarking::DeactivateWriteBarrierForSpace(PagedSpace* space) {
  PageIterator it(space);
  while (it.has_next()) {
    Page* p = it.next();
    SetOldSpacePageFlags(p, false);
  }
}


void IncrementalMarking::DeactivateWriteBarrier() {
  DeactivateWriteBarrierForSpace(heap_->old_pointer_space());
  DeactivateWriteBarrierForSpace(heap_->old_data_space());
  DeactivateWriteBarrierForSpace(heap_->cell_space());
  DeactivateWriteBarrierForSpace(heap_->map_space());
  DeactivateWriteBarrierForSpace(heap_->code_space());

  SetNewSpacePageFlags(heap_->new_space()->ActivePage(), false);

  LargePage* lop = heap_->lo_space()->first_page();
  while (lop->is_valid()) {
    SetOldSpacePageFlags(lop, false);
    lop = lop->next_page();
  }
}


void IncrementalMarking::ClearMarkbits(PagedSpace* space) {
  PageIterator it(space);
  while (it.has_next()) {
    Page* p = it.next();
    p->markbits()->Clear();
    SetOldSpacePageFlags(p, true);
  }
}


void IncrementalMarking::ClearMarkbits() {
  // TODO(gc): Clear the mark bits in the sweeper.
  ClearMarkbits(heap_->old_pointer_space());
  ClearMarkbits(heap_->old_data_space());
  ClearMarkbits(heap_->cell_space());
  ClearMarkbits(heap_->map_space());
  ClearMarkbits(heap_->code_space());
  heap_->new_space()->ActivePage()->markbits()->Clear();

  SetNewSpacePageFlags(heap_->new_space()->ActivePage(), true);

  LargePage* lop = heap_->lo_space()->first_page();
  while (lop->is_valid()) {
    SetOldSpacePageFlags(lop, true);
    lop = lop->next_page();
  }
}


#ifdef DEBUG
void IncrementalMarking::VerifyMarkbitsAreClean(PagedSpace* space) {
  PageIterator it(space);

  while (it.has_next()) {
    Page* p = it.next();
    ASSERT(p->markbits()->IsClean());
  }
}


void IncrementalMarking::VerifyMarkbitsAreClean() {
  VerifyMarkbitsAreClean(heap_->old_pointer_space());
  VerifyMarkbitsAreClean(heap_->old_data_space());
  VerifyMarkbitsAreClean(heap_->code_space());
  VerifyMarkbitsAreClean(heap_->cell_space());
  VerifyMarkbitsAreClean(heap_->map_space());
  ASSERT(heap_->new_space()->ActivePage()->markbits()->IsClean());
}
#endif


bool IncrementalMarking::WorthActivating() {
#ifndef DEBUG
  static const intptr_t kActivationThreshold = 8 * MB;
#else
  // TODO(gc) consider setting this to some low level so that some
  // debug tests run with incremental marking and some without.
  static const intptr_t kActivationThreshold = 0;
#endif

  // TODO(gc) ISOLATES MERGE
  return FLAG_incremental_marking &&
      heap_->PromotedSpaceSize() > kActivationThreshold;
}


static void PatchIncrementalMarkingRecordWriteStubs(bool enable) {
  NumberDictionary* stubs = HEAP->code_stubs();

  int capacity = stubs->Capacity();
  for (int i = 0; i < capacity; i++) {
    Object* k = stubs->KeyAt(i);
    if (stubs->IsKey(k)) {
      uint32_t key = NumberToUint32(k);

      if (CodeStub::MajorKeyFromKey(key) ==
          CodeStub::RecordWrite) {
        Object* e = stubs->ValueAt(i);
        if (e->IsCode()) {
          RecordWriteStub::Patch(Code::cast(e), enable);
        }
      }
    }
  }
}


static VirtualMemory* marking_deque_memory = NULL;


static void EnsureMarkingDequeIsCommitted() {
  if (marking_deque_memory == NULL) {
    marking_deque_memory = new VirtualMemory(4 * MB);
    marking_deque_memory->Commit(
        reinterpret_cast<Address>(marking_deque_memory->address()),
        marking_deque_memory->size(),
        false);  // Not executable.
  }
}


void IncrementalMarking::Start() {
  if (FLAG_trace_incremental_marking) {
    PrintF("[IncrementalMarking] Start\n");
  }
  ASSERT(FLAG_incremental_marking);
  ASSERT(state_ == STOPPED);

  ResetStepCounters();

  if (heap_->old_pointer_space()->IsSweepingComplete() &&
      heap_->old_data_space()->IsSweepingComplete()) {
    StartMarking();
  } else {
    if (FLAG_trace_incremental_marking) {
      PrintF("[IncrementalMarking] Start sweeping.\n");
    }
    state_ = SWEEPING;
  }

  heap_->new_space()->LowerInlineAllocationLimit(kAllocatedThreshold);
}


void IncrementalMarking::StartMarking() {
  if (FLAG_trace_incremental_marking) {
    PrintF("[IncrementalMarking] Start marking\n");
  }

  state_ = MARKING;

  PatchIncrementalMarkingRecordWriteStubs(true);

  EnsureMarkingDequeIsCommitted();

  // Initialize marking stack.
  Address addr = static_cast<Address>(marking_deque_memory->address());
  int size = marking_deque_memory->size();
  if (FLAG_force_marking_deque_overflows) size = 64 * kPointerSize;
  marking_deque_.Initialize(addr, addr + size);

  // Clear markbits.
  ClearMarkbits();

#ifdef DEBUG
  VerifyMarkbitsAreClean();
#endif

  // Mark strong roots grey.
  IncrementalMarkingRootMarkingVisitor visitor(heap_, this);
  heap_->IterateStrongRoots(&visitor, VISIT_ONLY_STRONG);

  // Ready to start incremental marking.
  if (FLAG_trace_incremental_marking) {
    PrintF("[IncrementalMarking] Running\n");
  }
}


void IncrementalMarking::PrepareForScavenge() {
  if (!IsMarking()) return;
  heap_->new_space()->InactivePage()->markbits()->Clear();
}


void IncrementalMarking::UpdateMarkingDequeAfterScavenge() {
  if (!IsMarking()) return;

  intptr_t current = marking_deque_.bottom();
  intptr_t mask = marking_deque_.mask();
  intptr_t limit = marking_deque_.top();
  HeapObject** array = marking_deque_.array();
  intptr_t new_top = current;

  while (current != limit) {
    HeapObject* obj = array[current];
    current = ((current + 1) & mask);
    if (heap_->InNewSpace(obj)) {
      MapWord map_word = obj->map_word();
      if (map_word.IsForwardingAddress()) {
        HeapObject* dest = map_word.ToForwardingAddress();
        array[new_top] = dest;
        new_top = ((new_top + 1) & mask);
        ASSERT(new_top != marking_deque_.bottom());
        ASSERT(Marking::Color(obj) == Marking::Color(dest));
      }
    } else {
      array[new_top] = obj;
      new_top = ((new_top + 1) & mask);
      ASSERT(new_top != marking_deque_.bottom());
    }
  }
  marking_deque_.set_top(new_top);
}


void IncrementalMarking::Hurry() {
  if (state() == MARKING) {
    double start = 0.0;
    if (FLAG_trace_incremental_marking) {
      PrintF("[IncrementalMarking] Hurry\n");
      start = OS::TimeCurrentMillis();
    }
    // TODO(gc) hurry can mark objects it encounters black as mutator
    // was stopped.
    Map* filler_map = heap_->one_pointer_filler_map();
    IncrementalMarkingMarkingVisitor marking_visitor(heap_, this);
    while (!marking_deque_.IsEmpty()) {
      HeapObject* obj = marking_deque_.Pop();

      // Explicitly skip one word fillers. Incremental markbit patterns are
      // correct only for objects that occupy at least two words.
      if (obj->map() != filler_map) {
        obj->Iterate(&marking_visitor);
        MarkBit mark_bit = Marking::MarkBitFrom(obj);
        Marking::MarkBlack(mark_bit);
      }
    }
    state_ = COMPLETE;
    if (FLAG_trace_incremental_marking) {
      double end = OS::TimeCurrentMillis();
      PrintF("[IncrementalMarking] Complete (hurry), spent %d ms.\n",
             static_cast<int>(end - start));
    }
  }
}


void IncrementalMarking::Abort() {
  if (IsStopped()) return;
  if (FLAG_trace_incremental_marking) {
    PrintF("[IncrementalMarking] Aborting.\n");
  }
  heap_->new_space()->LowerInlineAllocationLimit(0);
  IncrementalMarking::set_should_hurry(false);
  ResetStepCounters();
  if (IsMarking()) {
    PatchIncrementalMarkingRecordWriteStubs(false);
    DeactivateWriteBarrier();
  }
  heap_->isolate()->stack_guard()->Continue(GC_REQUEST);
  state_ = STOPPED;
}


void IncrementalMarking::Finalize() {
  Hurry();
  state_ = STOPPED;
  heap_->new_space()->LowerInlineAllocationLimit(0);
  IncrementalMarking::set_should_hurry(false);
  ResetStepCounters();
  PatchIncrementalMarkingRecordWriteStubs(false);
  DeactivateWriteBarrier();
  ASSERT(marking_deque_.IsEmpty());
  heap_->isolate()->stack_guard()->Continue(GC_REQUEST);
}


void IncrementalMarking::MarkingComplete() {
  state_ = COMPLETE;
  // We will set the stack guard to request a GC now.  This will mean the rest
  // of the GC gets performed as soon as possible (we can't do a GC here in a
  // record-write context).  If a few things get allocated between now and then
  // that shouldn't make us do a scavenge and keep being incremental, so we set
  // the should-hurry flag to indicate that there can't be much work left to do.
  set_should_hurry(true);
  if (FLAG_trace_incremental_marking) {
    PrintF("[IncrementalMarking] Complete (normal).\n");
  }
  // TODO(gc) ISOLATES
  ISOLATE->stack_guard()->RequestGC();
}


void IncrementalMarking::Step(intptr_t allocated_bytes) {
  if (heap_->gc_state() != Heap::NOT_IN_GC) return;
  if (!FLAG_incremental_marking) return;
  if (!FLAG_incremental_marking_steps) return;

  allocated_ += allocated_bytes;

  if (allocated_ < kAllocatedThreshold) return;

  intptr_t bytes_to_process = allocated_ * allocation_marking_factor_;

  double start = 0;

  if (FLAG_trace_incremental_marking || FLAG_trace_gc) {
    start = OS::TimeCurrentMillis();
  }

  if (state_ == SWEEPING) {
    if (heap_->old_pointer_space()->AdvanceSweeper(bytes_to_process) &&
        heap_->old_data_space()->AdvanceSweeper(bytes_to_process)) {
      StartMarking();
    }
  } else if (state_ == MARKING) {
    Map* filler_map = heap_->one_pointer_filler_map();
    IncrementalMarkingMarkingVisitor marking_visitor(heap_, this);
    while (!marking_deque_.IsEmpty() && bytes_to_process > 0) {
      HeapObject* obj = marking_deque_.Pop();

      // Explicitly skip one word fillers. Incremental markbit patterns are
      // correct only for objects that occupy at least two words.
      Map* map = obj->map();
      if (map != filler_map) {
        ASSERT(Marking::IsGrey(Marking::MarkBitFrom(obj)));
        int size = obj->SizeFromMap(map);
        bytes_to_process -= size;
        MarkBit map_mark_bit = Marking::MarkBitFrom(map);
        if (Marking::IsWhite(map_mark_bit)) {
          WhiteToGreyAndPush(map, map_mark_bit);
        }
        // TODO(gc) switch to static visitor instead of normal visitor.
        obj->IterateBody(map->instance_type(), size, &marking_visitor);
        MarkBit obj_mark_bit = Marking::MarkBitFrom(obj);
        Marking::MarkBlack(obj_mark_bit);
      }
    }
    if (marking_deque_.IsEmpty()) MarkingComplete();
  }

  allocated_ = 0;

  steps_count_++;

  if ((steps_count_ % kAllocationMarkingFactorSpeedupInterval) == 0) {
    allocation_marking_factor_ += kAllocationMarkingFactorSpeedup;
    allocation_marking_factor_ *= 1.3;
    if (FLAG_trace_gc) {
      PrintF("Marking speed increased to %d\n", allocation_marking_factor_);
    }
  }

  if (FLAG_trace_incremental_marking || FLAG_trace_gc) {
    double end = OS::TimeCurrentMillis();
    steps_took_ += (end - start);
  }
}


} }  // namespace v8::internal
