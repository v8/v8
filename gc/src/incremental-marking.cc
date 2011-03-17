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
    if ((*p)->IsHeapObject()) {
      HeapObject* object = HeapObject::cast(*p);
      if (IncrementalMarking::IsWhite(object)) {
        IncrementalMarking::WhiteToGrey(object);
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
    if (!(*p)->IsHeapObject()) return;

    HeapObject* object = HeapObject::cast(*p);
    if (IncrementalMarking::IsWhite(object)) {
      IncrementalMarking::WhiteToGrey(object);
    }
  }
};


static void ClearMarkbits(PagedSpace* space) {
  PageIterator it(space, PageIterator::PAGES_IN_USE);

  while (it.has_next()) {
    Page* p = it.next();
    p->markbits()->Clear();
  }
}


static void ClearMarkbits() {
  // We are sweeping code and map spaces precisely so clearing is not required.
  ClearMarkbits(Heap::old_pointer_space());
  ClearMarkbits(Heap::old_data_space());
  ClearMarkbits(Heap::cell_space());
}


#ifdef DEBUG
  static void VerifyMarkbitsAreClean(PagedSpace* space) {
    PageIterator it(space, PageIterator::PAGES_IN_USE);

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


void IncrementalMarking::Start() {
  if (FLAG_trace_incremental_marking) {
    PrintF("[IncrementalMarking] Start\n");
  }
  ASSERT(FLAG_incremental_marking);
  ASSERT(state_ == STOPPED);
  state_ = MARKING;

  PatchIncrementalMarkingRecordWriteStubs(true);

  Heap::EnsureFromSpaceIsCommitted();

  // Initialize marking stack.
  marking_stack_.Initialize(Heap::new_space()->FromSpaceLow(),
                            Heap::new_space()->FromSpaceHigh());

  // Clear markbits.
  Address new_space_top = Heap::new_space()->top();
  Address new_space_bottom = Heap::new_space()->bottom();

  Marking::ClearRange(new_space_bottom,
                      static_cast<int>(new_space_top - new_space_bottom));

  ClearMarkbits();

#ifdef DEBUG
  VerifyMarkbitsAreClean();
#endif

  // Mark strong roots grey.
  IncrementalMarkingRootMarkingVisitor visitor;
  Heap::IterateStrongRoots(&visitor, VISIT_ONLY_STRONG);

  // Ready to start incremental marking.
  if (FLAG_trace_incremental_marking) {
    PrintF("[IncrementalMarking] Running\n");
  }
}


void IncrementalMarking::Hurry() {
  if (state() == MARKING) {
    if (FLAG_trace_incremental_marking) {
      PrintF("[IncrementalMarking] Hurry\n");
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
        MarkBlack(obj);
      }
    }
    state_ = COMPLETE;
    if (FLAG_trace_incremental_marking) {
      PrintF("[IncrementalMarking] Complete (hurry)\n");
    }
  }
}


void IncrementalMarking::Finalize() {
  Hurry();
  state_ = STOPPED;
  PatchIncrementalMarkingRecordWriteStubs(false);
  ASSERT(marking_stack_.is_empty());
}


void IncrementalMarking::MarkingComplete() {
  state_ = COMPLETE;
  // We completed marking.
  if (FLAG_trace_incremental_marking) {
    PrintF("[IncrementalMarking] Complete (normal)\n");
  }
  StackGuard::RequestGC();
}


void IncrementalMarking::Step(intptr_t allocated_bytes) {
  if (state_ == MARKING) {
    allocated += allocated_bytes;

    if (allocated >= kAllocatedThreshold) {
      intptr_t bytes_to_process = allocated * kAllocationMarkingFactor;
      int count = 0;
      double start = 0;
      if (FLAG_trace_incremental_marking) {
        PrintF("[IncrementalMarking] Marking %d bytes\n", bytes_to_process);
        start = OS::TimeCurrentMillis();
      }

      Map* filler_map = Heap::one_pointer_filler_map();
      while (!marking_stack_.is_empty() && bytes_to_process > 0) {
        HeapObject* obj = marking_stack_.Pop();

        // Explicitly skip one word fillers. Incremental markbit patterns are
        // correct only for objects that occupy at least two words.
        if (obj->map() != filler_map) {
          ASSERT(IsGrey(obj));
          Map* map = obj->map();
          int size = obj->SizeFromMap(map);
          bytes_to_process -= size;
          if (IsWhite(map)) WhiteToGrey(map);
          obj->IterateBody(map->instance_type(), size, &marking_visitor);
          MarkBlack(obj);
        }
        count++;
      }
      if (FLAG_trace_incremental_marking) {
        double end = OS::TimeCurrentMillis();
        PrintF("[IncrementalMarking]     %d objects marked, spent %d ms\n",
               count,
               static_cast<int>(end - start));
      }
      allocated = 0;
      if (marking_stack_.is_empty()) MarkingComplete();
    }
  }
}


} }  // namespace v8::internal
