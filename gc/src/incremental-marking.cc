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

IncrementalMarking::State IncrementalMarking::state_ = STOPPED;
MarkingStack IncrementalMarking::marking_stack_;

double IncrementalMarking::steps_took_ = 0;
int IncrementalMarking::steps_count_ = 0;
intptr_t IncrementalMarking::allocation_marking_factor_ = 0;

static intptr_t allocated = 0;

class IncrementalMarkingMarkingVisitor : public ObjectVisitor {
 public:
  void VisitPointer(Object** p) {
    MarkObjectByPointer(p);
  }

  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) MarkObjectByPointer(p);
  }

 private:
  // Mark object pointed to by p.
  INLINE(static void MarkObjectByPointer(Object** p)) {
    Object* obj = *p;
    // Since we can be sure that the object is not tagged as a failure we can
    // inline a slightly more efficient tag check here than IsHeapObject() would
    // produce.
    if (obj->NonFailureIsHeapObject()) {
      HeapObject* heap_object = HeapObject::cast(obj);
      MarkBit mark_bit = Marking::MarkBitFrom(heap_object);
      if (mark_bit.data_only()) {
        IncrementalMarking::MarkBlackOrKeepGrey(mark_bit);
      } else {
        if (IncrementalMarking::IsWhite(mark_bit)) {
          IncrementalMarking::WhiteToGreyAndPush(heap_object, mark_bit);
        }
      }
    }
  }
};


static IncrementalMarkingMarkingVisitor marking_visitor;

class IncrementalMarkingRootMarkingVisitor : public ObjectVisitor {
 public:
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
      IncrementalMarking::MarkBlackOrKeepGrey(mark_bit);
    } else {
      if (IncrementalMarking::IsWhite(mark_bit)) {
        IncrementalMarking::WhiteToGreyAndPush(heap_object, mark_bit);
      }
    }
  }
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
  ClearMarkbits(Heap::old_pointer_space());
  ClearMarkbits(Heap::old_data_space());
  ClearMarkbits(Heap::cell_space());
  ClearMarkbits(Heap::map_space());
  ClearMarkbits(Heap::code_space());
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
    VerifyMarkbitsAreClean(Heap::old_pointer_space());
    VerifyMarkbitsAreClean(Heap::old_data_space());
    VerifyMarkbitsAreClean(Heap::code_space());
    VerifyMarkbitsAreClean(Heap::cell_space());
    VerifyMarkbitsAreClean(Heap::map_space());
  }
#endif

bool IncrementalMarking::WorthActivating() {
#ifndef DEBUG
  static const intptr_t kActivationThreshold = 20*MB;
#else
  // TODO(gc) consider setting this to some low level so that some
  // debug tests run with incremental marking and some without.
  static const intptr_t kActivationThreshold = 0;
#endif

  return FLAG_incremental_marking &&
      Heap::PromotedSpaceSize() > kActivationThreshold;
}


static void PatchIncrementalMarkingRecordWriteStubs(bool enable) {
  NumberDictionary* stubs = Heap::code_stubs();

  int capacity = stubs->Capacity();
  for (int i = 0; i < capacity; i++) {
    Object* k = stubs->KeyAt(i);
    if (stubs->IsKey(k)) {
      uint32_t key = NumberToUint32(k);

      if (CodeStub::MajorKeyFromKey(key) ==
          CodeStub::IncrementalMarkingRecordWrite) {
        Object* e = stubs->ValueAt(i);
        if (e->IsCode()) {
          IncrementalMarkingRecordWriteStub::Patch(Code::cast(e), enable);
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
  Address new_space_low = Heap::new_space()->ToSpaceLow();
  Address new_space_high = Heap::new_space()->ToSpaceHigh();
  Marking::ClearRange(new_space_low,
                      static_cast<int>(new_space_high - new_space_low));

  ClearMarkbits();

#ifdef DEBUG
  VerifyMarkbitsAreClean();
#endif

  Heap::new_space()->LowerInlineAllocationLimit(kAllocatedThreshold);

  // Mark strong roots grey.
  IncrementalMarkingRootMarkingVisitor visitor;
  Heap::IterateStrongRoots(&visitor, VISIT_ONLY_STRONG);

  // Ready to start incremental marking.
  if (FLAG_trace_incremental_marking) {
    PrintF("[IncrementalMarking] Running\n");
  }
}


void IncrementalMarking::PrepareForScavenge() {
  if (IsStopped()) return;

  ResetStepCounters();

  Address new_space_low = Heap::new_space()->FromSpaceLow();
  Address new_space_high = Heap::new_space()->FromSpaceHigh();
  Marking::ClearRange(new_space_low,
                      static_cast<int>(new_space_high - new_space_low));
}


void IncrementalMarking::UpdateMarkingStackAfterScavenge() {
  if (IsStopped()) return;

  HeapObject** current = marking_stack_.low();
  HeapObject** top = marking_stack_.top();
  HeapObject** new_top = current;

  while (current < top) {
    HeapObject* obj = *current++;
    if (Heap::InNewSpace(obj)) {
      MapWord map_word = obj->map_word();
      if (map_word.IsForwardingAddress()) {
        HeapObject* dest = map_word.ToForwardingAddress();
        WhiteToGrey(dest, Marking::MarkBitFrom(dest));
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
    Map* filler_map = Heap::one_pointer_filler_map();
    while (!marking_stack_.is_empty()) {
      HeapObject* obj = marking_stack_.Pop();

      // Explicitly skip one word fillers. Incremental markbit patterns are
      // correct only for objects that occupy at least two words.
      if (obj->map() != filler_map) {
        obj->Iterate(&marking_visitor);
        MarkBit mark_bit = Marking::MarkBitFrom(obj);
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
  ResetStepCounters();
  PatchIncrementalMarkingRecordWriteStubs(false);
  ASSERT(marking_stack_.is_empty());
}


void IncrementalMarking::MarkingComplete() {
  state_ = COMPLETE;
  if (FLAG_trace_incremental_marking) {
    PrintF("[IncrementalMarking] Complete (normal).\n");
  }
  StackGuard::RequestGC();
}


void IncrementalMarking::Step(intptr_t allocated_bytes) {
  if (state_ == MARKING &&
      Heap::gc_state() == Heap::NOT_IN_GC &&
      FLAG_incremental_marking_steps) {
    allocated += allocated_bytes;

    if (allocated >= kAllocatedThreshold) {
      double start = 0;

      if (FLAG_trace_incremental_marking || FLAG_trace_gc) {
        start = OS::TimeCurrentMillis();
      }

      intptr_t bytes_to_process = allocated * allocation_marking_factor_;
      int count = 0;

      Map* filler_map = Heap::one_pointer_filler_map();
      while (!marking_stack_.is_empty() && bytes_to_process > 0) {
        HeapObject* obj = marking_stack_.Pop();

        // Explicitly skip one word fillers. Incremental markbit patterns are
        // correct only for objects that occupy at least two words.
        Map* map = obj->map();
        if (map != filler_map) {
          ASSERT(IsGrey(Marking::MarkBitFrom(obj)));
          int size = obj->SizeFromMap(map);
          bytes_to_process -= size;
          MarkBit map_mark_bit = Marking::MarkBitFromOldSpace(map);
          if (IsWhite(map_mark_bit)) WhiteToGreyAndPush(map, map_mark_bit);
          // TODO(gc) switch to static visitor instead of normal visitor.
          obj->IterateBody(map->instance_type(), size, &marking_visitor);
          MarkBit obj_mark_bit = Marking::MarkBitFrom(obj);
          MarkBlack(obj_mark_bit);
        }
        count++;
      }
      allocated = 0;
      if (marking_stack_.is_empty()) MarkingComplete();
      if (FLAG_trace_incremental_marking || FLAG_trace_gc) {
        double end = OS::TimeCurrentMillis();
        steps_took_ += (end - start);
      }

      steps_count_++;

      if ((steps_count_ % kAllocationMarkingFactorSpeedupInterval) == 0) {
        allocation_marking_factor_ *= kAllocationMarkingFactorSpeedup;
      }
    }
  }
}


} }  // namespace v8::internal
