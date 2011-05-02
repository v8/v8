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

const char* IncrementalMarking::kWhiteBitPattern = "00";
const char* IncrementalMarking::kBlackBitPattern = "10";
const char* IncrementalMarking::kGreyBitPattern = "11";
const char* IncrementalMarking::kImpossibleBitPattern = "01";

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
      MarkBit mark_bit = heap_->marking()->MarkBitFrom(heap_object);
      if (mark_bit.data_only()) {
        incremental_marking_->MarkBlackOrKeepGrey(mark_bit);
      } else {
        if (IncrementalMarking::IsWhite(mark_bit)) {
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
    MarkBit mark_bit = heap_->marking()->MarkBitFrom(heap_object);
    if (mark_bit.data_only()) {
      incremental_marking_->MarkBlackOrKeepGrey(mark_bit);
    } else {
      if (IncrementalMarking::IsWhite(mark_bit)) {
        incremental_marking_->WhiteToGreyAndPush(heap_object, mark_bit);
      }
    }
  }

  Heap* heap_;
  IncrementalMarking* incremental_marking_;
};


static void ClearMarkbits(PagedSpace* space) {
  PageIterator it(space);

  while (it.has_next()) {
    Page* p = it.next();
    p->markbits()->Clear();
  }
}


static void ClearMarkbits() {
  // TODO(gc): Clear the mark bits in the sweeper.
  // TODO(gc) ISOLATES MERGE
  ClearMarkbits(HEAP->old_pointer_space());
  ClearMarkbits(HEAP->old_data_space());
  ClearMarkbits(HEAP->cell_space());
  ClearMarkbits(HEAP->map_space());
  ClearMarkbits(HEAP->code_space());
}


#ifdef DEBUG
  static void VerifyMarkbitsAreClean(PagedSpace* space) {
    PageIterator it(space);

    while (it.has_next()) {
      Page* p = it.next();
      ASSERT(p->markbits()->IsClean());
    }
  }

  static void VerifyMarkbitsAreClean() {
    // TODO(gc) ISOLATES MERGE
    VerifyMarkbitsAreClean(HEAP->old_pointer_space());
    VerifyMarkbitsAreClean(HEAP->old_data_space());
    VerifyMarkbitsAreClean(HEAP->code_space());
    VerifyMarkbitsAreClean(HEAP->cell_space());
    VerifyMarkbitsAreClean(HEAP->map_space());
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

static VirtualMemory* marking_stack_memory = NULL;

static void EnsureMarkingStackIsCommitted() {
  if (marking_stack_memory == NULL) {
    marking_stack_memory = new VirtualMemory(4*MB);
    marking_stack_memory->Commit(
        reinterpret_cast<Address>(marking_stack_memory->address()),
        marking_stack_memory->size(),
        false);  // Not executable.
  }
}


void IncrementalMarking::Start() {
  if (FLAG_trace_incremental_marking) {
    PrintF("[IncrementalMarking] Start\n");
  }
  ASSERT(FLAG_incremental_marking);
  ASSERT(state_ == STOPPED);
  state_ = MARKING;

  ResetStepCounters();

  PatchIncrementalMarkingRecordWriteStubs(true);

  EnsureMarkingStackIsCommitted();

  // Initialize marking stack.
  Address addr = static_cast<Address>(marking_stack_memory->address());
  marking_stack_.Initialize(addr,
                            addr + marking_stack_memory->size());

  // Clear markbits.
  Address new_space_low = heap_->new_space()->ToSpaceLow();
  Address new_space_high = heap_->new_space()->ToSpaceHigh();
  heap_->marking()->ClearRange(
      new_space_low, static_cast<int>(new_space_high - new_space_low));

  ClearMarkbits();

#ifdef DEBUG
  VerifyMarkbitsAreClean();
#endif

  heap_->new_space()->LowerInlineAllocationLimit(kAllocatedThreshold);

  // Mark strong roots grey.
  IncrementalMarkingRootMarkingVisitor visitor(heap_, this);
  heap_->IterateStrongRoots(&visitor, VISIT_ONLY_STRONG);

  // Ready to start incremental marking.
  if (FLAG_trace_incremental_marking) {
    PrintF("[IncrementalMarking] Running\n");
  }
}


void IncrementalMarking::PrepareForScavenge() {
  if (IsStopped()) return;

  Address new_space_low = heap_->new_space()->FromSpaceLow();
  Address new_space_high = heap_->new_space()->FromSpaceHigh();
  heap_->marking()->ClearRange(
      new_space_low, static_cast<int>(new_space_high - new_space_low));
}


void IncrementalMarking::UpdateMarkingStackAfterScavenge() {
  if (IsStopped()) return;

  HeapObject** current = marking_stack_.low();
  HeapObject** top = marking_stack_.top();
  HeapObject** new_top = current;

  while (current < top) {
    HeapObject* obj = *current++;
    if (heap_->InNewSpace(obj)) {
      MapWord map_word = obj->map_word();
      if (map_word.IsForwardingAddress()) {
        HeapObject* dest = map_word.ToForwardingAddress();
        WhiteToGrey(dest, heap_->marking()->MarkBitFrom(dest));
        *new_top++ = dest;
        ASSERT(Color(obj) == Color(dest));
      }
    } else {
      *new_top++ = obj;
    }
  }

  marking_stack_.set_top(new_top);
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
    while (!marking_stack_.is_empty()) {
      HeapObject* obj = marking_stack_.Pop();

      // Explicitly skip one word fillers. Incremental markbit patterns are
      // correct only for objects that occupy at least two words.
      if (obj->map() != filler_map) {
        obj->Iterate(&marking_visitor);
        MarkBit mark_bit = heap_->marking()->MarkBitFrom(obj);
        MarkBlack(mark_bit);
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


void IncrementalMarking::Finalize() {
  Hurry();
  state_ = STOPPED;
  IncrementalMarking::set_should_hurry(false);
  ResetStepCounters();
  PatchIncrementalMarkingRecordWriteStubs(false);
  ASSERT(marking_stack_.is_empty());
  ISOLATE->stack_guard()->Continue(GC_REQUEST);
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
  if (state_ == MARKING &&
      heap_->gc_state() == Heap::NOT_IN_GC &&
      FLAG_incremental_marking_steps) {
    allocated_ += allocated_bytes;

    if (allocated_ >= kAllocatedThreshold) {
      double start = 0;

      if (FLAG_trace_incremental_marking || FLAG_trace_gc) {
        start = OS::TimeCurrentMillis();
      }

      intptr_t bytes_to_process = allocated_ * allocation_marking_factor_;
      int count = 0;

      Map* filler_map = heap_->one_pointer_filler_map();
      IncrementalMarkingMarkingVisitor marking_visitor(heap_, this);
      Marking* marking = heap_->marking();
      while (!marking_stack_.is_empty() && bytes_to_process > 0) {
        HeapObject* obj = marking_stack_.Pop();

        // Explicitly skip one word fillers. Incremental markbit patterns are
        // correct only for objects that occupy at least two words.
        Map* map = obj->map();
        if (map != filler_map) {
          ASSERT(IsGrey(marking->MarkBitFrom(obj)));
          int size = obj->SizeFromMap(map);
          bytes_to_process -= size;
          MarkBit map_mark_bit = marking->MarkBitFromOldSpace(map);
          if (IsWhite(map_mark_bit)) WhiteToGreyAndPush(map, map_mark_bit);
          // TODO(gc) switch to static visitor instead of normal visitor.
          obj->IterateBody(map->instance_type(), size, &marking_visitor);
          MarkBit obj_mark_bit = marking->MarkBitFrom(obj);
          MarkBlack(obj_mark_bit);
        }
        count++;
      }
      allocated_ = 0;
      if (marking_stack_.is_empty()) MarkingComplete();
      if (FLAG_trace_incremental_marking || FLAG_trace_gc) {
        double end = OS::TimeCurrentMillis();
        steps_took_ += (end - start);
      }

      steps_count_++;

      if ((steps_count_ % kAllocationMarkingFactorSpeedupInterval) == 0) {
        allocation_marking_factor_ += kAllocationMarkingFactorSpeedup;
      }
    }
  }
}


} }  // namespace v8::internal
