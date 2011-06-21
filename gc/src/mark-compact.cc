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

#include "compilation-cache.h"
#include "execution.h"
#include "gdb-jit.h"
#include "global-handles.h"
#include "heap-profiler.h"
#include "ic-inl.h"
#include "incremental-marking.h"
#include "liveobjectlist-inl.h"
#include "mark-compact.h"
#include "objects-visiting.h"
#include "objects-visiting-inl.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {


const char* Marking::kWhiteBitPattern = "00";
const char* Marking::kBlackBitPattern = "10";
const char* Marking::kGreyBitPattern = "11";
const char* Marking::kImpossibleBitPattern = "01";


// -------------------------------------------------------------------------
// MarkCompactCollector

MarkCompactCollector::MarkCompactCollector() :  // NOLINT
#ifdef DEBUG
      state_(IDLE),
#endif
      tracer_(NULL),
#ifdef DEBUG
      live_young_objects_size_(0),
      live_old_pointer_objects_size_(0),
      live_old_data_objects_size_(0),
      live_code_objects_size_(0),
      live_map_objects_size_(0),
      live_cell_objects_size_(0),
      live_lo_objects_size_(0),
      live_bytes_(0),
#endif
      heap_(NULL),
      code_flusher_(NULL) { }


#ifdef DEBUG
class VerifyMarkingVisitor: public ObjectVisitor {
 public:
  void VisitPointers(Object** start, Object** end) {
    for (Object** current = start; current < end; current++) {
      if ((*current)->IsHeapObject()) {
        HeapObject* object = HeapObject::cast(*current);
        ASSERT(HEAP->mark_compact_collector()->IsMarked(object));
      }
    }
  }
};


static void VerifyMarking(Address bottom, Address top) {
  VerifyMarkingVisitor visitor;
  HeapObject* object;
  Address next_object_must_be_here_or_later = bottom;

  for (Address current = bottom;
       current < top;
       current += kPointerSize) {
    object = HeapObject::FromAddress(current);
    if (MarkCompactCollector::IsMarked(object)) {
      ASSERT(current >= next_object_must_be_here_or_later);
      object->Iterate(&visitor);
      next_object_must_be_here_or_later = current + object->Size();
    }
  }
}


static void VerifyMarking(Page* p) {
  VerifyMarking(p->ObjectAreaStart(), p->ObjectAreaEnd());
}


static void VerifyMarking(NewSpace* space) {
  Address end = space->top();
  NewSpacePageIterator it(space->bottom(), end);
  // The bottom position is at the start of its page. Allows us to use
  // page->body() as start of range on all pages.
  ASSERT_EQ(space->bottom(),
            NewSpacePage::FromAddress(space->bottom())->body());
  while (it.has_next()) {
    NewSpacePage* page = it.next();
    Address limit = it.has_next() ? page->body_limit() : end;
    ASSERT(limit == end || !page->Contains(end));
    VerifyMarking(page->body(), limit);
  }
}


static void VerifyMarking(PagedSpace* space) {
  PageIterator it(space);

  while (it.has_next()) {
    VerifyMarking(it.next());
  }
}


static void VerifyMarking(Heap* heap) {
  VerifyMarking(heap->old_pointer_space());
  VerifyMarking(heap->old_data_space());
  VerifyMarking(heap->code_space());
  VerifyMarking(heap->cell_space());
  VerifyMarking(heap->map_space());
  VerifyMarking(heap->new_space());

  VerifyMarkingVisitor visitor;
  heap->IterateStrongRoots(&visitor, VISIT_ONLY_STRONG);
}


class VerifyEvacuationVisitor: public ObjectVisitor {
 public:
  void VisitPointers(Object** start, Object** end) {
    for (Object** current = start; current < end; current++) {
      if ((*current)->IsHeapObject()) {
        HeapObject* object = HeapObject::cast(*current);
        if (MarkCompactCollector::IsOnEvacuationCandidate(object)) {
          HEAP->TracePathToObject(source_);
          CHECK(false);
        }
      }
    }
  }

  HeapObject* source_;
};


static void VerifyEvacuation(Address bottom, Address top) {
  VerifyEvacuationVisitor visitor;
  HeapObject* object;
  Address next_object_must_be_here_or_later = bottom;

  for (Address current = bottom;
       current < top;
       current += kPointerSize) {
    object = HeapObject::FromAddress(current);
    if (MarkCompactCollector::IsMarked(object)) {
      ASSERT(current >= next_object_must_be_here_or_later);
      visitor.source_ = object;
      object->Iterate(&visitor);
      next_object_must_be_here_or_later = current + object->Size();
    }
  }
}


static void VerifyEvacuation(Page* p) {
  if (p->IsEvacuationCandidate()) return;

  VerifyEvacuation(p->ObjectAreaStart(), p->ObjectAreaEnd());
}


static void VerifyEvacuation(NewSpace* space) {
  // TODO(gc) Verify evacution for new space.
}


static void VerifyEvacuation(PagedSpace* space) {
  PageIterator it(space);

  while (it.has_next()) {
    VerifyEvacuation(it.next());
  }
}


static void VerifyEvacuation(Heap* heap) {
  VerifyEvacuation(heap->old_pointer_space());
  VerifyEvacuation(heap->old_data_space());
  VerifyEvacuation(heap->code_space());
  VerifyEvacuation(heap->cell_space());
  VerifyEvacuation(heap->map_space());
  VerifyEvacuation(heap->new_space());

  VerifyEvacuationVisitor visitor;
  heap->IterateStrongRoots(&visitor, VISIT_ALL);
}
#endif


void MarkCompactCollector::AddEvacuationCandidate(Page* p) {
  p->MarkEvacuationCandidate();
  evacuation_candidates_.Add(p);
}


void MarkCompactCollector::CollectGarbage() {
  // Make sure that Prepare() has been called. The individual steps below will
  // update the state as they proceed.
  ASSERT(state_ == PREPARE_GC);

  MarkLiveObjects();
  ASSERT(heap_->incremental_marking()->IsStopped());

  if (FLAG_collect_maps) ClearNonLiveTransitions();

#ifdef DEBUG
  if (FLAG_verify_heap) {
    VerifyMarking(heap_);
  }
#endif

  SweepSpaces();

  heap_->isolate()->pc_to_code_cache()->Flush();

  Finish();

  tracer_ = NULL;
}


#ifdef DEBUG
static void VerifyMarkbitsAreClean(PagedSpace* space) {
  PageIterator it(space);

  while (it.has_next()) {
    Page* p = it.next();
    ASSERT(p->markbits()->IsClean());
  }
}

static void VerifyMarkbitsAreClean(NewSpace* space) {
  NewSpacePageIterator it(space->ToSpaceStart(), space->ToSpaceEnd());

  while (it.has_next()) {
    NewSpacePage* p = it.next();
    ASSERT(p->markbits()->IsClean());
  }
}

static void VerifyMarkbitsAreClean(Heap* heap) {
  VerifyMarkbitsAreClean(heap->old_pointer_space());
  VerifyMarkbitsAreClean(heap->old_data_space());
  VerifyMarkbitsAreClean(heap->code_space());
  VerifyMarkbitsAreClean(heap->cell_space());
  VerifyMarkbitsAreClean(heap->map_space());
  VerifyMarkbitsAreClean(heap->new_space());
}
#endif


static void ClearMarkbits(PagedSpace* space) {
  PageIterator it(space);

  while (it.has_next()) {
    Page* p = it.next();
    p->markbits()->Clear();
  }
}


static void ClearMarkbits(NewSpace* space) {
  NewSpacePageIterator it(space->ToSpaceStart(), space->ToSpaceEnd());

  while (it.has_next()) {
    NewSpacePage* p = it.next();
    p->markbits()->Clear();
  }
}


static void ClearMarkbits(Heap* heap) {
  // TODO(gc): Clean the mark bits while sweeping.
  ClearMarkbits(heap->code_space());
  ClearMarkbits(heap->map_space());
  ClearMarkbits(heap->old_pointer_space());
  ClearMarkbits(heap->old_data_space());
  ClearMarkbits(heap->cell_space());
  ClearMarkbits(heap->new_space());
}


void Marking::TransferMark(Address old_start, Address new_start) {
  if (old_start == new_start) return;

  MarkBit new_mark_bit = MarkBitFrom(new_start);

  if (heap_->incremental_marking()->IsMarking()) {
    MarkBit old_mark_bit = MarkBitFrom(old_start);
#ifdef DEBUG
    ObjectColor old_color = Color(old_mark_bit);
#endif
    if (Marking::IsBlack(old_mark_bit)) {
      Marking::MarkBlack(new_mark_bit);
      old_mark_bit.Clear();
    } else if (Marking::IsGrey(old_mark_bit)) {
      old_mark_bit.Next().Clear();
      heap_->incremental_marking()->WhiteToGreyAndPush(
          HeapObject::FromAddress(new_start), new_mark_bit);
      heap_->incremental_marking()->RestartIfNotMarking();
      // TODO(gc): if we shift huge array in the loop we might end up pushing
      // too much into the marking deque. Maybe we should check one or two
      // elements on top/bottom of the marking deque to see whether they are
      // equal to old_start.
    }

#ifdef DEBUG
    ObjectColor new_color = Color(new_mark_bit);
    ASSERT(new_color == old_color);
#endif
  } else {
    if (heap_->InNewSpace(old_start)) {
      return;
    } else {
      MarkBit old_mark_bit = MarkBitFrom(old_start);
      if (!old_mark_bit.Get()) {
        return;
      }
    }
    new_mark_bit.Set();
  }
}


void MarkCompactCollector::CollectEvacuationCandidates(PagedSpace* space) {
  ASSERT(space->identity() == OLD_POINTER_SPACE ||
         space->identity() == OLD_DATA_SPACE);

  PageIterator it(space);
  while (it.has_next()) {
    Page* p = it.next();
    if (space->IsFragmented(p)) {
      AddEvacuationCandidate(p);
    } else {
      p->ClearEvacuationCandidate();
    }
  }
}


static void ClearEvacuationCandidates(PagedSpace* space) {
  ASSERT(space->identity() == OLD_POINTER_SPACE ||
         space->identity() == OLD_DATA_SPACE);

  PageIterator it(space);
  while (it.has_next()) {
    Page* p = it.next();
    p->ClearEvacuationCandidate();
  }
}


void MarkCompactCollector::Prepare(GCTracer* tracer) {
  // TODO(gc) re-enable code flushing.
  FLAG_flush_code = false;
  FLAG_always_compact = false;

  // Disable collection of maps if incremental marking is enabled.
  // TODO(gc) improve maps collection algorithm to work with incremental
  // marking.
  if (FLAG_incremental_marking) FLAG_collect_maps = false;

  // Rather than passing the tracer around we stash it in a static member
  // variable.
  tracer_ = tracer;

#ifdef DEBUG
  ASSERT(state_ == IDLE);
  state_ = PREPARE_GC;
#endif
  ASSERT(!FLAG_always_compact || !FLAG_never_compact);

  if (FLAG_collect_maps) CreateBackPointers();
#ifdef ENABLE_GDB_JIT_INTERFACE
  if (FLAG_gdbjit) {
    // If GDBJIT interface is active disable compaction.
    compacting_collection_ = false;
  }
#endif

  if (!FLAG_never_compact) {
    slots_buffer_.Clear();
    evacuation_candidates_.Rewind(0);

    if (!heap()->incremental_marking()->IsMarking()) {
      CollectEvacuationCandidates(heap()->old_pointer_space());
      CollectEvacuationCandidates(heap()->old_data_space());
    } else {
      ClearEvacuationCandidates(heap()->old_pointer_space());
      ClearEvacuationCandidates(heap()->old_data_space());
    }
  }

  PagedSpaces spaces;
  for (PagedSpace* space = spaces.next();
       space != NULL;
       space = spaces.next()) {
    space->PrepareForMarkCompact();
  }

  if (!heap()->incremental_marking()->IsMarking()) {
    ClearMarkbits(heap_);
#ifdef DEBUG
    VerifyMarkbitsAreClean(heap_);
#endif
  }

#ifdef DEBUG
  live_bytes_ = 0;
  live_young_objects_size_ = 0;
  live_old_pointer_objects_size_ = 0;
  live_old_data_objects_size_ = 0;
  live_code_objects_size_ = 0;
  live_map_objects_size_ = 0;
  live_cell_objects_size_ = 0;
  live_lo_objects_size_ = 0;
#endif
}


void MarkCompactCollector::Finish() {
#ifdef DEBUG
  ASSERT(state_ == SWEEP_SPACES || state_ == RELOCATE_OBJECTS);
  state_ = IDLE;
#endif
  // The stub cache is not traversed during GC; clear the cache to
  // force lazy re-initialization of it. This must be done after the
  // GC, because it relies on the new address of certain old space
  // objects (empty string, illegal builtin).
  heap()->isolate()->stub_cache()->Clear();

  heap()->external_string_table_.CleanUp();
}


// -------------------------------------------------------------------------
// Phase 1: tracing and marking live objects.
//   before: all objects are in normal state.
//   after: a live object's map pointer is marked as '00'.

// Marking all live objects in the heap as part of mark-sweep or mark-compact
// collection.  Before marking, all objects are in their normal state.  After
// marking, live objects' map pointers are marked indicating that the object
// has been found reachable.
//
// The marking algorithm is a (mostly) depth-first (because of possible stack
// overflow) traversal of the graph of objects reachable from the roots.  It
// uses an explicit stack of pointers rather than recursion.  The young
// generation's inactive ('from') space is used as a marking stack.  The
// objects in the marking stack are the ones that have been reached and marked
// but their children have not yet been visited.
//
// The marking stack can overflow during traversal.  In that case, we set an
// overflow flag.  When the overflow flag is set, we continue marking objects
// reachable from the objects on the marking stack, but no longer push them on
// the marking stack.  Instead, we mark them as both marked and overflowed.
// When the stack is in the overflowed state, objects marked as overflowed
// have been reached and marked but their children have not been visited yet.
// After emptying the marking stack, we clear the overflow flag and traverse
// the heap looking for objects marked as overflowed, push them on the stack,
// and continue with marking.  This process repeats until all reachable
// objects have been marked.

class CodeFlusher {
 public:
  explicit CodeFlusher(Isolate* isolate)
      : isolate_(isolate),
        jsfunction_candidates_head_(NULL),
        shared_function_info_candidates_head_(NULL) {}

  void AddCandidate(SharedFunctionInfo* shared_info) {
    SetNextCandidate(shared_info, shared_function_info_candidates_head_);
    shared_function_info_candidates_head_ = shared_info;
  }

  void AddCandidate(JSFunction* function) {
    ASSERT(function->unchecked_code() ==
           function->unchecked_shared()->unchecked_code());

    SetNextCandidate(function, jsfunction_candidates_head_);
    jsfunction_candidates_head_ = function;
  }

  void ProcessCandidates() {
    ProcessSharedFunctionInfoCandidates();
    ProcessJSFunctionCandidates();
  }

 private:
  void ProcessJSFunctionCandidates() {
    Code* lazy_compile = isolate_->builtins()->builtin(Builtins::kLazyCompile);

    JSFunction* candidate = jsfunction_candidates_head_;
    JSFunction* next_candidate;
    while (candidate != NULL) {
      next_candidate = GetNextCandidate(candidate);

      SharedFunctionInfo* shared = candidate->unchecked_shared();

      Code* code = shared->unchecked_code();
      MarkBit code_mark = Marking::MarkBitFrom(code);
      if (!code_mark.Get()) {
        shared->set_code(lazy_compile);
        candidate->set_code(lazy_compile);
      } else {
        candidate->set_code(shared->unchecked_code());
      }

      candidate = next_candidate;
    }

    jsfunction_candidates_head_ = NULL;
  }


  void ProcessSharedFunctionInfoCandidates() {
    Code* lazy_compile = isolate_->builtins()->builtin(Builtins::kLazyCompile);

    SharedFunctionInfo* candidate = shared_function_info_candidates_head_;
    SharedFunctionInfo* next_candidate;
    while (candidate != NULL) {
      next_candidate = GetNextCandidate(candidate);
      SetNextCandidate(candidate, NULL);

      Code* code = candidate->unchecked_code();
      MarkBit code_mark = Marking::MarkBitFrom(code);
      if (!code_mark.Get()) {
        candidate->set_code(lazy_compile);
      }

      candidate = next_candidate;
    }

    shared_function_info_candidates_head_ = NULL;
  }

  static JSFunction** GetNextCandidateField(JSFunction* candidate) {
    return reinterpret_cast<JSFunction**>(
        candidate->address() + JSFunction::kCodeEntryOffset);
  }

  static JSFunction* GetNextCandidate(JSFunction* candidate) {
    return *GetNextCandidateField(candidate);
  }

  static void SetNextCandidate(JSFunction* candidate,
                               JSFunction* next_candidate) {
    *GetNextCandidateField(candidate) = next_candidate;
  }

  static SharedFunctionInfo** GetNextCandidateField(
      SharedFunctionInfo* candidate) {
    Code* code = candidate->unchecked_code();
    return reinterpret_cast<SharedFunctionInfo**>(
        code->address() + Code::kNextCodeFlushingCandidateOffset);
  }

  static SharedFunctionInfo* GetNextCandidate(SharedFunctionInfo* candidate) {
    return *GetNextCandidateField(candidate);
  }

  static void SetNextCandidate(SharedFunctionInfo* candidate,
                               SharedFunctionInfo* next_candidate) {
    *GetNextCandidateField(candidate) = next_candidate;
  }

  Isolate* isolate_;
  JSFunction* jsfunction_candidates_head_;
  SharedFunctionInfo* shared_function_info_candidates_head_;

  DISALLOW_COPY_AND_ASSIGN(CodeFlusher);
};


MarkCompactCollector::~MarkCompactCollector() {
  if (code_flusher_ != NULL) {
    delete code_flusher_;
    code_flusher_ = NULL;
  }
}


static inline HeapObject* ShortCircuitConsString(Object** p) {
  // Optimization: If the heap object pointed to by p is a non-symbol
  // cons string whose right substring is HEAP->empty_string, update
  // it in place to its left substring.  Return the updated value.
  //
  // Here we assume that if we change *p, we replace it with a heap object
  // (ie, the left substring of a cons string is always a heap object).
  //
  // The check performed is:
  //   object->IsConsString() && !object->IsSymbol() &&
  //   (ConsString::cast(object)->second() == HEAP->empty_string())
  // except the maps for the object and its possible substrings might be
  // marked.
  HeapObject* object = HeapObject::cast(*p);
  Map* map = object->map();
  InstanceType type = map->instance_type();
  if ((type & kShortcutTypeMask) != kShortcutTypeTag) return object;

  Object* second = reinterpret_cast<ConsString*>(object)->unchecked_second();
  Heap* heap = map->GetHeap();
  if (second != heap->empty_string()) {
    return object;
  }

  // Since we don't have the object's start, it is impossible to update the
  // page dirty marks. Therefore, we only replace the string with its left
  // substring when page dirty marks do not change.
  // TODO(gc): Seems like we could relax this restriction with store buffers.
  Object* first = reinterpret_cast<ConsString*>(object)->unchecked_first();
  if (!heap->InNewSpace(object) && heap->InNewSpace(first)) return object;

  *p = first;
  return HeapObject::cast(first);
}


class StaticMarkingVisitor : public StaticVisitorBase {
 public:
  static inline void IterateBody(Map* map, HeapObject* obj) {
    table_.GetVisitor(map)(map, obj);
  }

  static void Initialize() {
    table_.Register(kVisitShortcutCandidate,
                    &FixedBodyVisitor<StaticMarkingVisitor,
                                      ConsString::BodyDescriptor,
                                      void>::Visit);

    table_.Register(kVisitConsString,
                    &FixedBodyVisitor<StaticMarkingVisitor,
                                      ConsString::BodyDescriptor,
                                      void>::Visit);


    table_.Register(kVisitFixedArray,
                    &FlexibleBodyVisitor<StaticMarkingVisitor,
                                         FixedArray::BodyDescriptor,
                                         void>::Visit);

    table_.Register(kVisitGlobalContext, &VisitGlobalContext);

    table_.Register(kVisitByteArray, &DataObjectVisitor::Visit);
    table_.Register(kVisitFreeSpace, &DataObjectVisitor::Visit);
    table_.Register(kVisitSeqAsciiString, &DataObjectVisitor::Visit);
    table_.Register(kVisitSeqTwoByteString, &DataObjectVisitor::Visit);

    table_.Register(kVisitOddball,
                    &FixedBodyVisitor<StaticMarkingVisitor,
                                      Oddball::BodyDescriptor,
                                      void>::Visit);
    table_.Register(kVisitMap,
                    &FixedBodyVisitor<StaticMarkingVisitor,
                                      Map::BodyDescriptor,
                                      void>::Visit);

    table_.Register(kVisitCode, &VisitCode);

    table_.Register(kVisitSharedFunctionInfo,
                    &VisitSharedFunctionInfoAndFlushCode);

    table_.Register(kVisitJSFunction,
                    &VisitJSFunctionAndFlushCode);

    table_.Register(kVisitPropertyCell,
                    &FixedBodyVisitor<StaticMarkingVisitor,
                                      JSGlobalPropertyCell::BodyDescriptor,
                                      void>::Visit);

    table_.RegisterSpecializations<DataObjectVisitor,
                                   kVisitDataObject,
                                   kVisitDataObjectGeneric>();

    table_.RegisterSpecializations<JSObjectVisitor,
                                   kVisitJSObject,
                                   kVisitJSObjectGeneric>();

    table_.RegisterSpecializations<StructObjectVisitor,
                                   kVisitStruct,
                                   kVisitStructGeneric>();
  }

  INLINE(static void VisitPointer(Heap* heap, Object** p)) {
    MarkObjectByPointer(heap->mark_compact_collector(), p, p);
  }

  INLINE(static void VisitPointers(Heap* heap, Object** start, Object** end)) {
    // Mark all objects pointed to in [start, end).
    const int kMinRangeForMarkingRecursion = 64;
    if (end - start >= kMinRangeForMarkingRecursion) {
      if (VisitUnmarkedObjects(heap, start, end)) return;
      // We are close to a stack overflow, so just mark the objects.
    }
    MarkCompactCollector* collector = heap->mark_compact_collector();
    for (Object** p = start; p < end; p++) {
      MarkObjectByPointer(collector, start, p);
    }
  }

  static inline void VisitCodeTarget(Heap* heap, RelocInfo* rinfo) {
    ASSERT(RelocInfo::IsCodeTarget(rinfo->rmode()));
    Code* code = Code::GetCodeFromTargetAddress(rinfo->target_address());
    if (FLAG_cleanup_code_caches_at_gc && code->is_inline_cache_stub()) {
      IC::Clear(rinfo->pc());
      // Please note targets for cleared inline cached do not have to be
      // marked since they are contained in HEAP->non_monomorphic_cache().
    } else {
      MarkBit code_mark = Marking::MarkBitFrom(code);
      heap->mark_compact_collector()->MarkObject(code, code_mark);
    }
  }

  static void VisitGlobalPropertyCell(Heap* heap, RelocInfo* rinfo) {
    ASSERT(rinfo->rmode() == RelocInfo::GLOBAL_PROPERTY_CELL);
    Object* cell = rinfo->target_cell();
    Object* old_cell = cell;
    VisitPointer(heap, &cell);
    if (cell != old_cell) {
      rinfo->set_target_cell(reinterpret_cast<JSGlobalPropertyCell*>(cell),
                             NULL);
    }
  }

  static inline void VisitDebugTarget(Heap* heap, RelocInfo* rinfo) {
    ASSERT((RelocInfo::IsJSReturn(rinfo->rmode()) &&
            rinfo->IsPatchedReturnSequence()) ||
           (RelocInfo::IsDebugBreakSlot(rinfo->rmode()) &&
            rinfo->IsPatchedDebugBreakSlotSequence()));
    HeapObject* code = Code::GetCodeFromTargetAddress(rinfo->call_address());
    MarkBit code_mark = Marking::MarkBitFrom(code);
    heap->mark_compact_collector()->MarkObject(code, code_mark);
  }

  // Mark object pointed to by p.
  INLINE(static void MarkObjectByPointer(MarkCompactCollector* collector,
                                         Object** anchor_slot,
                                         Object** p)) {
    if (!(*p)->IsHeapObject()) return;
    HeapObject* object = ShortCircuitConsString(p);
    collector->RecordSlot(anchor_slot, p, object);
    MarkBit mark = Marking::MarkBitFrom(object);
    collector->MarkObject(object, mark);
  }


  // Visit an unmarked object.
  INLINE(static void VisitUnmarkedObject(MarkCompactCollector* collector,
                                         HeapObject* obj)) {
#ifdef DEBUG
    ASSERT(Isolate::Current()->heap()->Contains(obj));
    ASSERT(!HEAP->mark_compact_collector()->IsMarked(obj));
#endif
    Map* map = obj->map();
    Heap* heap = obj->GetHeap();
    MarkBit mark = Marking::MarkBitFrom(obj);
    heap->mark_compact_collector()->SetMark(obj, mark);
    // Mark the map pointer and the body.
    MarkBit map_mark = Marking::MarkBitFrom(map);
    heap->mark_compact_collector()->MarkObject(map, map_mark);
    IterateBody(map, obj);
  }

  // Visit all unmarked objects pointed to by [start, end).
  // Returns false if the operation fails (lack of stack space).
  static inline bool VisitUnmarkedObjects(Heap* heap,
                                          Object** start,
                                          Object** end) {
    // Return false is we are close to the stack limit.
    StackLimitCheck check(heap->isolate());
    if (check.HasOverflowed()) return false;

    MarkCompactCollector* collector = heap->mark_compact_collector();
    // Visit the unmarked objects.
    for (Object** p = start; p < end; p++) {
      Object* o = *p;
      if (!o->IsHeapObject()) continue;
      collector->RecordSlot(start, p, o);
      HeapObject* obj = HeapObject::cast(o);
      MarkBit mark = Marking::MarkBitFrom(obj);
      if (mark.Get()) continue;
      VisitUnmarkedObject(collector, obj);
    }
    return true;
  }

  static inline void VisitExternalReference(Address* p) { }
  static inline void VisitRuntimeEntry(RelocInfo* rinfo) { }

 private:
  class DataObjectVisitor {
   public:
    template<int size>
    static void VisitSpecialized(Map* map, HeapObject* object) {
    }

    static void Visit(Map* map, HeapObject* object) {
    }
  };

  typedef FlexibleBodyVisitor<StaticMarkingVisitor,
                              JSObject::BodyDescriptor,
                              void> JSObjectVisitor;

  typedef FlexibleBodyVisitor<StaticMarkingVisitor,
                              StructBodyDescriptor,
                              void> StructObjectVisitor;

  static void VisitCode(Map* map, HeapObject* object) {
    reinterpret_cast<Code*>(object)->CodeIterateBody<StaticMarkingVisitor>(
        map->GetHeap());
  }

  // Code flushing support.

  // How many collections newly compiled code object will survive before being
  // flushed.
  static const int kCodeAgeThreshold = 5;

  inline static bool HasSourceCode(Heap* heap, SharedFunctionInfo* info) {
    Object* undefined = heap->undefined_value();
    return (info->script() != undefined) &&
        (reinterpret_cast<Script*>(info->script())->source() != undefined);
  }


  inline static bool IsCompiled(JSFunction* function) {
    return function->unchecked_code() !=
        function->GetIsolate()->builtins()->builtin(Builtins::kLazyCompile);
  }

  inline static bool IsCompiled(SharedFunctionInfo* function) {
    return function->unchecked_code() !=
        function->GetIsolate()->builtins()->builtin(Builtins::kLazyCompile);
  }

  inline static bool IsFlushable(Heap* heap, JSFunction* function) {
    SharedFunctionInfo* shared_info = function->unchecked_shared();

    // Code is either on stack, in compilation cache or referenced
    // by optimized version of function.
    MarkBit code_mark =
        Marking::MarkBitFrom(function->unchecked_code());
    if (code_mark.Get()) {
      shared_info->set_code_age(0);
      return false;
    }

    // We do not flush code for optimized functions.
    if (function->code() != shared_info->unchecked_code()) {
      return false;
    }

    return IsFlushable(heap, shared_info);
  }

  inline static bool IsFlushable(Heap* heap, SharedFunctionInfo* shared_info) {
    // Code is either on stack, in compilation cache or referenced
    // by optimized version of function.
    MarkBit code_mark =
        Marking::MarkBitFrom(shared_info->unchecked_code());
    if (code_mark.Get()) {
      shared_info->set_code_age(0);
      return false;
    }

    // The function must be compiled and have the source code available,
    // to be able to recompile it in case we need the function again.
    if (!(shared_info->is_compiled() && HasSourceCode(heap, shared_info))) {
      return false;
    }

    // We never flush code for Api functions.
    Object* function_data = shared_info->function_data();
    if (function_data->IsFunctionTemplateInfo()) return false;

    // Only flush code for functions.
    if (shared_info->code()->kind() != Code::FUNCTION) return false;

    // Function must be lazy compilable.
    if (!shared_info->allows_lazy_compilation()) return false;

    // If this is a full script wrapped in a function we do no flush the code.
    if (shared_info->is_toplevel()) return false;

    // Age this shared function info.
    if (shared_info->code_age() < kCodeAgeThreshold) {
      shared_info->set_code_age(shared_info->code_age() + 1);
      return false;
    }

    return true;
  }


  static bool FlushCodeForFunction(Heap* heap, JSFunction* function) {
    if (!IsFlushable(heap, function)) return false;

    // This function's code looks flushable. But we have to postpone the
    // decision until we see all functions that point to the same
    // SharedFunctionInfo because some of them might be optimized.
    // That would make the nonoptimized version of the code nonflushable,
    // because it is required for bailing out from optimized code.
    heap->mark_compact_collector()->code_flusher()->AddCandidate(function);
    return true;
  }

  static inline bool IsValidNotBuiltinContext(Object* ctx) {
    return ctx->IsContext() &&
        !Context::cast(ctx)->global()->IsJSBuiltinsObject();
  }


  static void VisitSharedFunctionInfoGeneric(Map* map, HeapObject* object) {
    SharedFunctionInfo* shared = reinterpret_cast<SharedFunctionInfo*>(object);

    if (shared->IsInobjectSlackTrackingInProgress()) shared->DetachInitialMap();

    FixedBodyVisitor<StaticMarkingVisitor,
                     SharedFunctionInfo::BodyDescriptor,
                     void>::Visit(map, object);
  }


  static void VisitSharedFunctionInfoAndFlushCode(Map* map,
                                                  HeapObject* object) {
    MarkCompactCollector* collector = map->GetHeap()->mark_compact_collector();
    if (!collector->is_code_flushing_enabled()) {
      VisitSharedFunctionInfoGeneric(map, object);
      return;
    }
    VisitSharedFunctionInfoAndFlushCodeGeneric(map, object, false);
  }


  static void VisitSharedFunctionInfoAndFlushCodeGeneric(
      Map* map, HeapObject* object, bool known_flush_code_candidate) {
    Heap* heap = map->GetHeap();
    SharedFunctionInfo* shared = reinterpret_cast<SharedFunctionInfo*>(object);

    if (shared->IsInobjectSlackTrackingInProgress()) shared->DetachInitialMap();

    if (!known_flush_code_candidate) {
      known_flush_code_candidate = IsFlushable(heap, shared);
      if (known_flush_code_candidate) {
        heap->mark_compact_collector()->code_flusher()->AddCandidate(shared);
      }
    }

    VisitSharedFunctionInfoFields(heap, object, known_flush_code_candidate);
  }


  static void VisitCodeEntry(Heap* heap, Address entry_address) {
    Object* code = Code::GetObjectFromEntryAddress(entry_address);
    Object* old_code = code;
    VisitPointer(heap, &code);
    if (code != old_code) {
      Memory::Address_at(entry_address) =
          reinterpret_cast<Code*>(code)->entry();
    }
  }

  static void VisitGlobalContext(Map* map, HeapObject* object) {
    FixedBodyVisitor<StaticMarkingVisitor,
                     Context::MarkCompactBodyDescriptor,
                     void>::Visit(map, object);

    MarkCompactCollector* collector = map->GetHeap()->mark_compact_collector();
    for (int idx = Context::FIRST_WEAK_SLOT;
         idx < Context::GLOBAL_CONTEXT_SLOTS;
         ++idx) {
      Object** slot =
          HeapObject::RawField(object, FixedArray::OffsetOfElementAt(idx));
      collector->RecordSlot(slot, slot, *slot);
    }
  }

  static void VisitJSFunctionAndFlushCode(Map* map, HeapObject* object) {
    Heap* heap = map->GetHeap();
    MarkCompactCollector* collector = heap->mark_compact_collector();
    if (!collector->is_code_flushing_enabled()) {
      VisitJSFunction(map, object);
      return;
    }

    JSFunction* jsfunction = reinterpret_cast<JSFunction*>(object);
    // The function must have a valid context and not be a builtin.
    bool flush_code_candidate = false;
    if (IsValidNotBuiltinContext(jsfunction->unchecked_context())) {
      flush_code_candidate = FlushCodeForFunction(heap, jsfunction);
    }

    if (!flush_code_candidate) {
      Code* code = jsfunction->unchecked_shared()->unchecked_code();
      MarkBit code_mark = Marking::MarkBitFrom(code);
      heap->mark_compact_collector()->MarkObject(code, code_mark);

      if (jsfunction->unchecked_code()->kind() == Code::OPTIMIZED_FUNCTION) {
        // For optimized functions we should retain both non-optimized version
        // of it's code and non-optimized version of all inlined functions.
        // This is required to support bailing out from inlined code.
        DeoptimizationInputData* data =
            reinterpret_cast<DeoptimizationInputData*>(
                jsfunction->unchecked_code()->unchecked_deoptimization_data());

        FixedArray* literals = data->UncheckedLiteralArray();

        for (int i = 0, count = data->InlinedFunctionCount()->value();
             i < count;
             i++) {
          JSFunction* inlined = reinterpret_cast<JSFunction*>(literals->get(i));
          Code* inlined_code = inlined->unchecked_shared()->unchecked_code();
          MarkBit inlined_code_mark =
              Marking::MarkBitFrom(inlined_code);
          heap->mark_compact_collector()->MarkObject(
              inlined_code, inlined_code_mark);
        }
      }
    }

    VisitJSFunctionFields(map,
                          reinterpret_cast<JSFunction*>(object),
                          flush_code_candidate);
  }


  static void VisitJSFunction(Map* map, HeapObject* object) {
    VisitJSFunctionFields(map,
                          reinterpret_cast<JSFunction*>(object),
                          false);
  }


#define SLOT_ADDR(obj, offset) \
  reinterpret_cast<Object**>((obj)->address() + offset)


  static inline void VisitJSFunctionFields(Map* map,
                                           JSFunction* object,
                                           bool flush_code_candidate) {
    Heap* heap = map->GetHeap();

    VisitPointers(heap,
                  HeapObject::RawField(object, JSFunction::kPropertiesOffset),
                  HeapObject::RawField(object, JSFunction::kCodeEntryOffset));

    if (!flush_code_candidate) {
      VisitCodeEntry(heap, object->address() + JSFunction::kCodeEntryOffset);
    } else {
      // Don't visit code object.

      // Visit shared function info to avoid double checking of it's
      // flushability.
      SharedFunctionInfo* shared_info = object->unchecked_shared();
      MarkBit shared_info_mark = Marking::MarkBitFrom(shared_info);
      if (!shared_info_mark.Get()) {
        Map* shared_info_map = shared_info->map();
        MarkBit shared_info_map_mark =
            Marking::MarkBitFrom(shared_info_map);
        heap->mark_compact_collector()->SetMark(shared_info, shared_info_mark);
        heap->mark_compact_collector()->MarkObject(shared_info_map,
                                                   shared_info_map_mark);
        VisitSharedFunctionInfoAndFlushCodeGeneric(shared_info_map,
                                                   shared_info,
                                                   true);
      }
    }

    VisitPointers(
        heap,
        HeapObject::RawField(object,
                             JSFunction::kCodeEntryOffset + kPointerSize),
        HeapObject::RawField(object,
                             JSFunction::kNonWeakFieldsEndOffset));

    // Don't visit the next function list field as it is a weak reference.
    Object** next_function =
        HeapObject::RawField(object, JSFunction::kNextFunctionLinkOffset);
    heap->mark_compact_collector()->RecordSlot(
        next_function, next_function, *next_function);
  }


  static void VisitSharedFunctionInfoFields(Heap* heap,
                                            HeapObject* object,
                                            bool flush_code_candidate) {
    VisitPointer(heap, SLOT_ADDR(object, SharedFunctionInfo::kNameOffset));

    if (!flush_code_candidate) {
      VisitPointer(heap, SLOT_ADDR(object, SharedFunctionInfo::kCodeOffset));
    }

    VisitPointers(heap,
                  SLOT_ADDR(object, SharedFunctionInfo::kScopeInfoOffset),
                  SLOT_ADDR(object, SharedFunctionInfo::kSize));
  }

  #undef SLOT_ADDR

  typedef void (*Callback)(Map* map, HeapObject* object);

  static VisitorDispatchTable<Callback> table_;
};


VisitorDispatchTable<StaticMarkingVisitor::Callback>
  StaticMarkingVisitor::table_;


class MarkingVisitor : public ObjectVisitor {
 public:
  explicit MarkingVisitor(Heap* heap) : heap_(heap) { }

  void VisitPointer(Object** p) {
    StaticMarkingVisitor::VisitPointer(heap_, p);
  }

  void VisitPointers(Object** start, Object** end) {
    StaticMarkingVisitor::VisitPointers(heap_, start, end);
  }

  void VisitCodeTarget(Heap* heap, RelocInfo* rinfo) {
    StaticMarkingVisitor::VisitCodeTarget(heap, rinfo);
  }

  void VisitGlobalPropertyCell(Heap* heap, RelocInfo* rinfo) {
    StaticMarkingVisitor::VisitGlobalPropertyCell(heap, rinfo);
  }

  void VisitDebugTarget(Heap* heap, RelocInfo* rinfo) {
    StaticMarkingVisitor::VisitDebugTarget(heap, rinfo);
  }

 private:
  Heap* heap_;
};


class CodeMarkingVisitor : public ThreadVisitor {
 public:
  explicit CodeMarkingVisitor(MarkCompactCollector* collector)
      : collector_(collector) {}

  void VisitThread(Isolate* isolate, ThreadLocalTop* top) {
    for (StackFrameIterator it(isolate, top); !it.done(); it.Advance()) {
      Code* code = it.frame()->unchecked_code();
      MarkBit code_bit = Marking::MarkBitFrom(code);
      collector_->MarkObject(it.frame()->unchecked_code(), code_bit);
    }
  }

 private:
  MarkCompactCollector* collector_;
};


class SharedFunctionInfoMarkingVisitor : public ObjectVisitor {
 public:
  explicit SharedFunctionInfoMarkingVisitor(MarkCompactCollector* collector)
      : collector_(collector) {}

  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) VisitPointer(p);
  }

  void VisitPointer(Object** slot) {
    Object* obj = *slot;
    if (obj->IsSharedFunctionInfo()) {
      SharedFunctionInfo* shared = reinterpret_cast<SharedFunctionInfo*>(obj);
      MarkBit shared_mark = Marking::MarkBitFrom(shared);
      MarkBit code_mark = Marking::MarkBitFrom(shared->unchecked_code());
      collector_->MarkObject(shared->unchecked_code(), code_mark);
      collector_->MarkObject(shared, shared_mark);
    }
  }

 private:
  MarkCompactCollector* collector_;
};


void MarkCompactCollector::PrepareForCodeFlushing() {
  ASSERT(heap() == Isolate::Current()->heap());

  if (!FLAG_flush_code) {
    EnableCodeFlushing(false);
    return;
  }

#ifdef ENABLE_DEBUGGER_SUPPORT
  if (heap()->isolate()->debug()->IsLoaded() ||
      heap()->isolate()->debug()->has_break_points()) {
    EnableCodeFlushing(false);
    return;
  }
#endif
  EnableCodeFlushing(true);

  // Ensure that empty descriptor array is marked. Method MarkDescriptorArray
  // relies on it being marked before any other descriptor array.
  HeapObject* descriptor_array = heap()->empty_descriptor_array();
  MarkBit descriptor_array_mark = Marking::MarkBitFrom(descriptor_array);
  MarkObject(descriptor_array, descriptor_array_mark);

  // Make sure we are not referencing the code from the stack.
  ASSERT(this == heap()->mark_compact_collector());
  for (StackFrameIterator it; !it.done(); it.Advance()) {
    Code* code = it.frame()->unchecked_code();
    MarkBit code_mark = Marking::MarkBitFrom(code);
    MarkObject(code, code_mark);
  }

  // Iterate the archived stacks in all threads to check if
  // the code is referenced.
  CodeMarkingVisitor code_marking_visitor(this);
  heap()->isolate()->thread_manager()->IterateArchivedThreads(
      &code_marking_visitor);

  SharedFunctionInfoMarkingVisitor visitor(this);
  heap()->isolate()->compilation_cache()->IterateFunctions(&visitor);
  heap()->isolate()->handle_scope_implementer()->Iterate(&visitor);

  ProcessMarkingDeque();
}


// Visitor class for marking heap roots.
class RootMarkingVisitor : public ObjectVisitor {
 public:
  explicit RootMarkingVisitor(Heap* heap)
    : collector_(heap->mark_compact_collector()) { }

  void VisitPointer(Object** p) {
    MarkObjectByPointer(p);
  }

  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) MarkObjectByPointer(p);
  }

 private:
  void MarkObjectByPointer(Object** p) {
    if (!(*p)->IsHeapObject()) return;

    // Replace flat cons strings in place.
    HeapObject* object = ShortCircuitConsString(p);
    MarkBit mark_bit = Marking::MarkBitFrom(object);
    if (mark_bit.Get()) return;

    Map* map = object->map();
    // Mark the object.
    collector_->SetMark(object, mark_bit);

    // Mark the map pointer and body, and push them on the marking stack.
    MarkBit map_mark = Marking::MarkBitFrom(map);
    collector_->MarkObject(map, map_mark);
    StaticMarkingVisitor::IterateBody(map, object);

    // Mark all the objects reachable from the map and body.  May leave
    // overflowed objects in the heap.
    collector_->EmptyMarkingDeque();
  }

  MarkCompactCollector* collector_;
};


// Helper class for pruning the symbol table.
class SymbolTableCleaner : public ObjectVisitor {
 public:
  explicit SymbolTableCleaner(Heap* heap)
    : heap_(heap), pointers_removed_(0) { }

  virtual void VisitPointers(Object** start, Object** end) {
    // Visit all HeapObject pointers in [start, end).
    for (Object** p = start; p < end; p++) {
      Object* o = *p;
      if (o->IsHeapObject() &&
          !Marking::MarkBitFrom(HeapObject::cast(o)).Get()) {
        // Check if the symbol being pruned is an external symbol. We need to
        // delete the associated external data as this symbol is going away.

        // Since no objects have yet been moved we can safely access the map of
        // the object.
        if (o->IsExternalString()) {
          heap_->FinalizeExternalString(String::cast(*p));
        }
        // Set the entry to null_value (as deleted).
        *p = heap_->null_value();
        pointers_removed_++;
      }
    }
  }

  int PointersRemoved() {
    return pointers_removed_;
  }
 private:
  Heap* heap_;
  int pointers_removed_;
};


// Implementation of WeakObjectRetainer for mark compact GCs. All marked objects
// are retained.
class MarkCompactWeakObjectRetainer : public WeakObjectRetainer {
 public:
  virtual Object* RetainAs(Object* object) {
    if (Marking::MarkBitFrom(HeapObject::cast(object)).Get()) {
      return object;
    } else {
      return NULL;
    }
  }
};


void MarkCompactCollector::ProcessNewlyMarkedObject(HeapObject* object) {
  ASSERT(IsMarked(object));
  ASSERT(HEAP->Contains(object));
  if (object->IsMap()) {
    Map* map = Map::cast(object);
    if (FLAG_cleanup_code_caches_at_gc) {
      map->ClearCodeCache(heap());
    }

    // When map collection is enabled we have to mark through map's transitions
    // in a special way to make transition links weak.
    // Only maps with instance types between FIRST_JS_OBJECT_TYPE and
    // JS_FUNCTION_TYPE can have transitions.
    if (FLAG_collect_maps &&
        map->instance_type() >= FIRST_JS_OBJECT_TYPE &&
        map->instance_type() <= JS_FUNCTION_TYPE) {
      MarkMapContents(map);
    } else {
      marking_deque_.PushBlack(map);
    }
  } else {
    marking_deque_.PushBlack(object);
  }
}


void MarkCompactCollector::MarkMapContents(Map* map) {
  // Mark prototype transitions array but don't push it into marking stack.
  // This will make references from it weak. We will clean dead prototype
  // transitions in ClearNonLiveTransitions.
  FixedArray* prototype_transitions = map->prototype_transitions();
  MarkBit mark = Marking::MarkBitFrom(prototype_transitions);
  if (!mark.Get()) mark.Set();

  Object** raw_descriptor_array_slot =
      HeapObject::RawField(map, Map::kInstanceDescriptorsOrBitField3Offset);
  Object* raw_descriptor_array = *raw_descriptor_array_slot;
  if (!raw_descriptor_array->IsSmi()) {
    MarkDescriptorArray(
        reinterpret_cast<DescriptorArray*>(raw_descriptor_array));
  }

  // Mark the Object* fields of the Map.
  // Since the descriptor array has been marked already, it is fine
  // that one of these fields contains a pointer to it.
  Object** start_slot = HeapObject::RawField(map,
                                             Map::kPointerFieldsBeginOffset);

  Object** end_slot = HeapObject::RawField(map, Map::kPointerFieldsEndOffset);

  StaticMarkingVisitor::VisitPointers(map->GetHeap(), start_slot, end_slot);
}


void MarkCompactCollector::MarkDescriptorArray(
    DescriptorArray* descriptors) {
  MarkBit descriptors_mark = Marking::MarkBitFrom(descriptors);
  if (descriptors_mark.Get()) return;
  // Empty descriptor array is marked as a root before any maps are marked.
  ASSERT(descriptors != heap()->empty_descriptor_array());
  SetMark(descriptors, descriptors_mark);

  FixedArray* contents = reinterpret_cast<FixedArray*>(
      descriptors->get(DescriptorArray::kContentArrayIndex));
  ASSERT(contents->IsHeapObject());
  ASSERT(!IsMarked(contents));
  ASSERT(contents->IsFixedArray());
  ASSERT(contents->length() >= 2);
  MarkBit contents_mark = Marking::MarkBitFrom(contents);
  SetMark(contents, contents_mark);
  // Contents contains (value, details) pairs.  If the details say that the type
  // of descriptor is MAP_TRANSITION, CONSTANT_TRANSITION,
  // EXTERNAL_ARRAY_TRANSITION or NULL_DESCRIPTOR, we don't mark the value as
  // live.  Only for MAP_TRANSITION, EXTERNAL_ARRAY_TRANSITION and
  // CONSTANT_TRANSITION is the value an Object* (a Map*).
  for (int i = 0; i < contents->length(); i += 2) {
    // If the pair (value, details) at index i, i+1 is not
    // a transition or null descriptor, mark the value.
    PropertyDetails details(Smi::cast(contents->get(i + 1)));

    Object** slot = contents->data_start() + i;
    Object* value = *slot;
    if (!value->IsHeapObject()) continue;

    RecordSlot(slot, slot, *slot);

    if (details.type() < FIRST_PHANTOM_PROPERTY_TYPE) {
      HeapObject* object = HeapObject::cast(value);
      MarkBit mark = Marking::MarkBitFrom(HeapObject::cast(object));
      if (!mark.Get()) {
        SetMark(HeapObject::cast(object), mark);
        marking_deque_.PushBlack(object);
      }
    }
  }
  // The DescriptorArray descriptors contains a pointer to its contents array,
  // but the contents array is already marked.
  marking_deque_.PushBlack(descriptors);
}


void MarkCompactCollector::CreateBackPointers() {
  HeapObjectIterator iterator(heap()->map_space());
  for (HeapObject* next_object = iterator.Next();
       next_object != NULL; next_object = iterator.Next()) {
    if (next_object->IsMap()) {  // Could also be FreeSpace object on free list.
      Map* map = Map::cast(next_object);
      if (map->instance_type() >= FIRST_JS_OBJECT_TYPE &&
          map->instance_type() <= JS_FUNCTION_TYPE) {
        map->CreateBackPointers();
      } else {
        ASSERT(map->instance_descriptors() == heap()->empty_descriptor_array());
      }
    }
  }
}


// Fill the marking stack with overflowed objects returned by the given
// iterator.  Stop when the marking stack is filled or the end of the space
// is reached, whichever comes first.
template<class T>
static void DiscoverGreyObjectsWithIterator(Heap* heap,
                                            MarkingDeque* marking_deque,
                                            T* it) {
  // The caller should ensure that the marking stack is initially not full,
  // so that we don't waste effort pointlessly scanning for objects.
  ASSERT(!marking_deque->IsFull());

  Map* filler_map = heap->one_pointer_filler_map();
  for (HeapObject* object = it->Next();
       object != NULL;
       object = it->Next()) {
    MarkBit markbit = Marking::MarkBitFrom(object);
    if ((object->map() != filler_map) && Marking::IsGrey(markbit)) {
      Marking::GreyToBlack(markbit);
      marking_deque->PushBlack(object);
      if (marking_deque->IsFull()) return;
    }
  }
}


static inline int MarkWordToObjectStarts(uint32_t mark_bits, int* starts);


static void DiscoverGreyObjectsOnPage(MarkingDeque* marking_deque, Page* p) {
  ASSERT(strcmp(Marking::kWhiteBitPattern, "00") == 0);
  ASSERT(strcmp(Marking::kBlackBitPattern, "10") == 0);
  ASSERT(strcmp(Marking::kGreyBitPattern, "11") == 0);
  ASSERT(strcmp(Marking::kImpossibleBitPattern, "01") == 0);

  MarkBit::CellType* cells = p->markbits()->cells();

  int last_cell_index =
      Bitmap::IndexToCell(
          Bitmap::CellAlignIndex(
              p->AddressToMarkbitIndex(p->ObjectAreaEnd())));

  int cell_index = Page::kFirstUsedCell;
  Address cell_base = p->ObjectAreaStart();

  for (cell_index = Page::kFirstUsedCell;
       cell_index < last_cell_index;
       cell_index++, cell_base += 32 * kPointerSize) {
    ASSERT((unsigned)cell_index ==
        Bitmap::IndexToCell(
            Bitmap::CellAlignIndex(
                p->AddressToMarkbitIndex(cell_base))));

    const MarkBit::CellType current_cell = cells[cell_index];
    if (current_cell == 0) continue;

    const MarkBit::CellType next_cell = cells[cell_index + 1];
    MarkBit::CellType grey_objects = current_cell &
        ((current_cell >> 1) | (next_cell << (Bitmap::kBitsPerCell - 1)));

    int offset = 0;
    while (grey_objects != 0) {
      int trailing_zeros = CompilerIntrinsics::CountTrailingZeros(grey_objects);
      grey_objects >>= trailing_zeros;
      offset += trailing_zeros;
      MarkBit markbit(&cells[cell_index], 1 << offset, false);
      ASSERT(Marking::IsGrey(markbit));
      Marking::GreyToBlack(markbit);
      Address addr = cell_base + offset * kPointerSize;
      marking_deque->PushBlack(HeapObject::FromAddress(addr));
      if (marking_deque->IsFull()) return;
      offset += 2;
      grey_objects >>= 2;
    }

    grey_objects >>= (Bitmap::kBitsPerCell - 1);
  }
}


static void DiscoverGreyObjectsInSpace(Heap* heap,
                                       MarkingDeque* marking_deque,
                                       PagedSpace* space) {
  if (!space->was_swept_conservatively()) {
    HeapObjectIterator it(space);
    DiscoverGreyObjectsWithIterator(heap, marking_deque, &it);
  } else {
    PageIterator it(space);
    while (it.has_next()) {
      Page* p = it.next();
      DiscoverGreyObjectsOnPage(marking_deque, p);
      if (marking_deque->IsFull()) return;
    }
  }
}


bool MarkCompactCollector::IsUnmarkedHeapObject(Object** p) {
  Object* o = *p;
  if (!o->IsHeapObject()) return false;
  HeapObject* heap_object = HeapObject::cast(o);
  MarkBit mark = Marking::MarkBitFrom(heap_object);
  return !mark.Get();
}


void MarkCompactCollector::MarkSymbolTable() {
  SymbolTable* symbol_table = heap()->symbol_table();
  // Mark the symbol table itself.
  MarkBit symbol_table_mark = Marking::MarkBitFrom(symbol_table);
  SetMark(symbol_table, symbol_table_mark);
  // Explicitly mark the prefix.
  MarkingVisitor marker(heap());
  symbol_table->IteratePrefix(&marker);
  ProcessMarkingDeque();
}


void MarkCompactCollector::MarkRoots(RootMarkingVisitor* visitor) {
  // Mark the heap roots including global variables, stack variables,
  // etc., and all objects reachable from them.
  heap()->IterateStrongRoots(visitor, VISIT_ONLY_STRONG);

  // Handle the symbol table specially.
  MarkSymbolTable();

  // There may be overflowed objects in the heap.  Visit them now.
  while (marking_deque_.overflowed()) {
    RefillMarkingDeque();
    EmptyMarkingDeque();
  }
}


void MarkCompactCollector::MarkObjectGroups() {
  List<ObjectGroup*>* object_groups =
      heap()->isolate()->global_handles()->object_groups();

  int last = 0;
  for (int i = 0; i < object_groups->length(); i++) {
    ObjectGroup* entry = object_groups->at(i);
    ASSERT(entry != NULL);

    Object*** objects = entry->objects_;
    bool group_marked = false;
    for (size_t j = 0; j < entry->length_; j++) {
      Object* object = *objects[j];
      if (object->IsHeapObject()) {
        HeapObject* heap_object = HeapObject::cast(object);
        MarkBit mark = Marking::MarkBitFrom(heap_object);
        if (mark.Get()) {
          group_marked = true;
          break;
        }
      }
    }

    if (!group_marked) {
      (*object_groups)[last++] = entry;
      continue;
    }

    // An object in the group is marked, so mark as grey all white heap
    // objects in the group.
    for (size_t j = 0; j < entry->length_; ++j) {
      Object* object = *objects[j];
      if (object->IsHeapObject()) {
        HeapObject* heap_object = HeapObject::cast(object);
        MarkBit mark = Marking::MarkBitFrom(heap_object);
        MarkObject(heap_object, mark);
      }
    }

    // Once the entire group has been colored grey, set the object group
    // to NULL so it won't be processed again.
    entry->Dispose();
    object_groups->at(i) = NULL;
  }
  object_groups->Rewind(last);
}


void MarkCompactCollector::MarkImplicitRefGroups() {
  List<ImplicitRefGroup*>* ref_groups =
      heap()->isolate()->global_handles()->implicit_ref_groups();

  int last = 0;
  for (int i = 0; i < ref_groups->length(); i++) {
    ImplicitRefGroup* entry = ref_groups->at(i);
    ASSERT(entry != NULL);

    if (!IsMarked(*entry->parent_)) {
      (*ref_groups)[last++] = entry;
      continue;
    }

    Object*** children = entry->children_;
    // A parent object is marked, so mark all child heap objects.
    for (size_t j = 0; j < entry->length_; ++j) {
      if ((*children[j])->IsHeapObject()) {
        HeapObject* child = HeapObject::cast(*children[j]);
        MarkBit mark = Marking::MarkBitFrom(child);
        MarkObject(child, mark);
      }
    }

    // Once the entire group has been marked, dispose it because it's
    // not needed anymore.
    entry->Dispose();
  }
  ref_groups->Rewind(last);
}


// Mark all objects reachable from the objects on the marking stack.
// Before: the marking stack contains zero or more heap object pointers.
// After: the marking stack is empty, and all objects reachable from the
// marking stack have been marked, or are overflowed in the heap.
void MarkCompactCollector::EmptyMarkingDeque() {
  while (!marking_deque_.IsEmpty()) {
    HeapObject* object = marking_deque_.Pop();
    ASSERT(object->IsHeapObject());
    ASSERT(heap()->Contains(object));
    ASSERT(Marking::IsBlack(Marking::MarkBitFrom(object)));

    Map* map = object->map();
    MarkBit map_mark = Marking::MarkBitFrom(map);
    MarkObject(map, map_mark);

    StaticMarkingVisitor::IterateBody(map, object);
  }
}


// Sweep the heap for overflowed objects, clear their overflow bits, and
// push them on the marking stack.  Stop early if the marking stack fills
// before sweeping completes.  If sweeping completes, there are no remaining
// overflowed objects in the heap so the overflow flag on the markings stack
// is cleared.
void MarkCompactCollector::RefillMarkingDeque() {
  ASSERT(marking_deque_.overflowed());

  SemiSpaceIterator new_it(heap()->new_space());
  DiscoverGreyObjectsWithIterator(heap(), &marking_deque_, &new_it);
  if (marking_deque_.IsFull()) return;

  DiscoverGreyObjectsInSpace(heap(),
                             &marking_deque_,
                             heap()->old_pointer_space());
  if (marking_deque_.IsFull()) return;

  DiscoverGreyObjectsInSpace(heap(),
                             &marking_deque_,
                             heap()->old_data_space());
  if (marking_deque_.IsFull()) return;

  DiscoverGreyObjectsInSpace(heap(),
                             &marking_deque_,
                             heap()->code_space());
  if (marking_deque_.IsFull()) return;

  DiscoverGreyObjectsInSpace(heap(),
                             &marking_deque_,
                             heap()->map_space());
  if (marking_deque_.IsFull()) return;

  DiscoverGreyObjectsInSpace(heap(),
                             &marking_deque_,
                             heap()->cell_space());
  if (marking_deque_.IsFull()) return;

  LargeObjectIterator lo_it(heap()->lo_space());
  DiscoverGreyObjectsWithIterator(heap(),
                                  &marking_deque_,
                                  &lo_it);
  if (marking_deque_.IsFull()) return;

  marking_deque_.ClearOverflowed();
}


// Mark all objects reachable (transitively) from objects on the marking
// stack.  Before: the marking stack contains zero or more heap object
// pointers.  After: the marking stack is empty and there are no overflowed
// objects in the heap.
void MarkCompactCollector::ProcessMarkingDeque() {
  EmptyMarkingDeque();
  while (marking_deque_.overflowed()) {
    RefillMarkingDeque();
    EmptyMarkingDeque();
  }
}


void MarkCompactCollector::ProcessExternalMarking() {
  bool work_to_do = true;
  ASSERT(marking_deque_.IsEmpty());
  while (work_to_do) {
    MarkObjectGroups();
    MarkImplicitRefGroups();
    work_to_do = !marking_deque_.IsEmpty();
    ProcessMarkingDeque();
  }
}


void MarkCompactCollector::MarkLiveObjects() {
  GCTracer::Scope gc_scope(tracer_, GCTracer::Scope::MC_MARK);
  // The recursive GC marker detects when it is nearing stack overflow,
  // and switches to a different marking system.  JS interrupts interfere
  // with the C stack limit check.
  PostponeInterruptsScope postpone(heap()->isolate());

  bool incremental_marking_overflowed = false;
  IncrementalMarking* incremental_marking = heap_->incremental_marking();
  if (incremental_marking->IsMarking()) {
    // Finalize the incremental marking and check whether we had an overflow.
    // Both markers use grey color to mark overflowed objects so
    // non-incremental marker can deal with them as if overflow
    // occured during normal marking.
    // But incremental marker uses a separate marking deque
    // so we have to explicitly copy it's overflow state.
    incremental_marking->Finalize();
    incremental_marking_overflowed =
        incremental_marking->marking_deque()->overflowed();
    incremental_marking->marking_deque()->ClearOverflowed();
  } else {
    // Abort any pending incremental activities e.g. incremental sweeping.
    incremental_marking->Abort();
  }

#ifdef DEBUG
  ASSERT(state_ == PREPARE_GC);
  state_ = MARK_LIVE_OBJECTS;
#endif
  // The to space contains live objects, a page in from space is used as a
  // marking stack.
  Address marking_deque_start = heap()->new_space()->FromSpacePageLow();
  Address marking_deque_end = heap()->new_space()->FromSpacePageHigh();
  if (FLAG_force_marking_deque_overflows) {
    marking_deque_end = marking_deque_start + 64 * kPointerSize;
  }
  marking_deque_.Initialize(marking_deque_start,
                            marking_deque_end);
  ASSERT(!marking_deque_.overflowed());

  if (incremental_marking_overflowed) {
    // There are overflowed objects left in the heap after incremental marking.
    marking_deque_.SetOverflowed();
  }

  PrepareForCodeFlushing();

  RootMarkingVisitor root_visitor(heap());
  MarkRoots(&root_visitor);

  // The objects reachable from the roots are marked, yet unreachable
  // objects are unmarked.  Mark objects reachable due to host
  // application specific logic.
  ProcessExternalMarking();

  // The objects reachable from the roots or object groups are marked,
  // yet unreachable objects are unmarked.  Mark objects reachable
  // only from weak global handles.
  //
  // First we identify nonlive weak handles and mark them as pending
  // destruction.
  heap()->isolate()->global_handles()->IdentifyWeakHandles(
      &IsUnmarkedHeapObject);
  // Then we mark the objects and process the transitive closure.
  heap()->isolate()->global_handles()->IterateWeakRoots(&root_visitor);
  while (marking_deque_.overflowed()) {
    RefillMarkingDeque();
    EmptyMarkingDeque();
  }

  // Repeat host application specific marking to mark unmarked objects
  // reachable from the weak roots.
  ProcessExternalMarking();

  AfterMarking();
}


void MarkCompactCollector::AfterMarking() {
  // Prune the symbol table removing all symbols only pointed to by the
  // symbol table.  Cannot use symbol_table() here because the symbol
  // table is marked.
  SymbolTable* symbol_table = heap()->symbol_table();
  SymbolTableCleaner v(heap());
  symbol_table->IterateElements(&v);
  symbol_table->ElementsRemoved(v.PointersRemoved());
  heap()->external_string_table_.Iterate(&v);
  heap()->external_string_table_.CleanUp();

  // Process the weak references.
  MarkCompactWeakObjectRetainer mark_compact_object_retainer;
  heap()->ProcessWeakReferences(&mark_compact_object_retainer);

  // Remove object groups after marking phase.
  heap()->isolate()->global_handles()->RemoveObjectGroups();
  heap()->isolate()->global_handles()->RemoveImplicitRefGroups();

  // Flush code from collected candidates.
  if (is_code_flushing_enabled()) {
    code_flusher_->ProcessCandidates();
  }

  // Clean up dead objects from the runtime profiler.
  heap()->isolate()->runtime_profiler()->RemoveDeadSamples();
}


#ifdef DEBUG
void MarkCompactCollector::UpdateLiveObjectCount(HeapObject* obj) {
  live_bytes_ += obj->Size();
  if (heap()->new_space()->Contains(obj)) {
    live_young_objects_size_ += obj->Size();
  } else if (heap()->map_space()->Contains(obj)) {
    ASSERT(obj->IsMap());
    live_map_objects_size_ += obj->Size();
  } else if (heap()->cell_space()->Contains(obj)) {
    ASSERT(obj->IsJSGlobalPropertyCell());
    live_cell_objects_size_ += obj->Size();
  } else if (heap()->old_pointer_space()->Contains(obj)) {
    live_old_pointer_objects_size_ += obj->Size();
  } else if (heap()->old_data_space()->Contains(obj)) {
    live_old_data_objects_size_ += obj->Size();
  } else if (heap()->code_space()->Contains(obj)) {
    live_code_objects_size_ += obj->Size();
  } else if (heap()->lo_space()->Contains(obj)) {
    live_lo_objects_size_ += obj->Size();
  } else {
    UNREACHABLE();
  }
}
#endif  // DEBUG


void MarkCompactCollector::ClearNonLiveTransitions() {
  HeapObjectIterator map_iterator(heap()->map_space());
  // Iterate over the map space, setting map transitions that go from
  // a marked map to an unmarked map to null transitions.  At the same time,
  // set all the prototype fields of maps back to their original value,
  // dropping the back pointers temporarily stored in the prototype field.
  // Setting the prototype field requires following the linked list of
  // back pointers, reversing them all at once.  This allows us to find
  // those maps with map transitions that need to be nulled, and only
  // scan the descriptor arrays of those maps, not all maps.
  // All of these actions are carried out only on maps of JSObjects
  // and related subtypes.
  for (HeapObject* obj = map_iterator.Next();
       obj != NULL; obj = map_iterator.Next()) {
    Map* map = reinterpret_cast<Map*>(obj);
    MarkBit map_mark = Marking::MarkBitFrom(map);
    if (map->IsFreeSpace()) continue;

    ASSERT(map->IsMap());
    // Only JSObject and subtypes have map transitions and back pointers.
    if (map->instance_type() < FIRST_JS_OBJECT_TYPE) continue;
    if (map->instance_type() > JS_FUNCTION_TYPE) continue;

    if (map_mark.Get() &&
        map->attached_to_shared_function_info()) {
      // This map is used for inobject slack tracking and has been detached
      // from SharedFunctionInfo during the mark phase.
      // Since it survived the GC, reattach it now.
      map->unchecked_constructor()->unchecked_shared()->AttachInitialMap(map);
    }

    // Clear dead prototype transitions.
    FixedArray* prototype_transitions = map->prototype_transitions();
    if (prototype_transitions->length() > 0) {
      int finger = Smi::cast(prototype_transitions->get(0))->value();
      int new_finger = 1;
      for (int i = 1; i < finger; i += 2) {
        HeapObject* prototype = HeapObject::cast(prototype_transitions->get(i));
        Map* cached_map = Map::cast(prototype_transitions->get(i + 1));
        MarkBit prototype_mark = Marking::MarkBitFrom(prototype);
        MarkBit cached_map_mark = Marking::MarkBitFrom(cached_map);
        if (prototype_mark.Get() && cached_map_mark.Get()) {
          if (new_finger != i) {
            prototype_transitions->set_unchecked(heap_,
                                                 new_finger,
                                                 prototype,
                                                 UPDATE_WRITE_BARRIER);
            prototype_transitions->set_unchecked(heap_,
                                                 new_finger + 1,
                                                 cached_map,
                                                 SKIP_WRITE_BARRIER);
          }

          Object** prototype_slot =
              prototype_transitions->data_start() + new_finger;
          RecordSlot(prototype_slot, prototype_slot, prototype);
          new_finger += 2;
        }
      }

      // Fill slots that became free with undefined value.
      Object* undefined = heap()->undefined_value();
      for (int i = new_finger; i < finger; i++) {
        prototype_transitions->set_unchecked(heap_,
                                             i,
                                             undefined,
                                             SKIP_WRITE_BARRIER);

        // TODO(gc) we should not evacuate first page of data space.
        // but we are doing it now to increase coverage.
        Object** undefined_slot =
            prototype_transitions->data_start() + i;
        RecordSlot(undefined_slot, undefined_slot, undefined);
      }
      prototype_transitions->set_unchecked(0, Smi::FromInt(new_finger));
    }

    // Follow the chain of back pointers to find the prototype.
    Map* current = map;
    while (current->IsMap()) {
      current = reinterpret_cast<Map*>(current->prototype());
      ASSERT(current->IsHeapObject());
    }
    Object* real_prototype = current;

    // Follow back pointers, setting them to prototype,
    // clearing map transitions when necessary.
    current = map;
    bool on_dead_path = !map_mark.Get();
    Object* next;
    while (current->IsMap()) {
      next = current->prototype();
      // There should never be a dead map above a live map.
      MarkBit current_mark = Marking::MarkBitFrom(current);
      bool is_alive = current_mark.Get();
      ASSERT(on_dead_path || is_alive);

      // A live map above a dead map indicates a dead transition.
      // This test will always be false on the first iteration.
      if (on_dead_path && is_alive) {
        on_dead_path = false;
        current->ClearNonLiveTransitions(heap(), real_prototype);
      }
      *HeapObject::RawField(current, Map::kPrototypeOffset) =
          real_prototype;

      if (is_alive) {
        Object** slot = HeapObject::RawField(current, Map::kPrototypeOffset);
        RecordSlot(slot, slot, real_prototype);
      }
      current = reinterpret_cast<Map*>(next);
    }
  }
}


// We scavange new space simultaneously with sweeping. This is done in two
// passes.
//
// The first pass migrates all alive objects from one semispace to another or
// promotes them to old space.  Forwarding address is written directly into
// first word of object without any encoding.  If object is dead we write
// NULL as a forwarding address.
//
// The second pass updates pointers to new space in all spaces.  It is possible
// to encounter pointers to dead new space objects during traversal of pointers
// to new space.  We should clear them to avoid encountering them during next
// pointer iteration.  This is an issue if the store buffer overflows and we
// have to scan the entire old space, including dead objects, looking for
// pointers to new space.
void MarkCompactCollector::MigrateObject(Address dst,
                                         Address src,
                                         int size,
                                         AllocationSpace dest) {
  ASSERT(dest == OLD_POINTER_SPACE ||
         dest == OLD_DATA_SPACE ||
         dest == LO_SPACE ||
         dest == NEW_SPACE);

  if (dest == OLD_POINTER_SPACE || dest == LO_SPACE) {
    Address src_slot = src;
    Address dst_slot = dst;
    ASSERT(IsAligned(size, kPointerSize));

    for (int remaining = size / kPointerSize; remaining > 0; remaining--) {
      Object* value = Memory::Object_at(src_slot);

      Memory::Object_at(dst_slot) = value;

      if (heap_->InNewSpace(value)) {
        heap_->store_buffer()->Mark(dst_slot);
      } else if (value->IsHeapObject() &&
                 MarkCompactCollector::IsOnEvacuationCandidate(value)) {
        slots_buffer_.Add(reinterpret_cast<Object**>(dst_slot));
      }

      src_slot += kPointerSize;
      dst_slot += kPointerSize;
    }
  } else {
    heap_->CopyBlock(dst, src, size);
  }
  Memory::Address_at(src) = dst;
}


// Visitor for updating pointers from live objects in old spaces to new space.
// It does not expect to encounter pointers to dead objects.
class PointersUpdatingVisitor: public ObjectVisitor {
 public:
  explicit PointersUpdatingVisitor(Heap* heap) : heap_(heap) { }

  void VisitPointer(Object** p) {
    UpdatePointer(p);
  }

  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) UpdatePointer(p);
  }

  void VisitCodeTarget(RelocInfo* rinfo) {
    ASSERT(RelocInfo::IsCodeTarget(rinfo->rmode()));
    Object* target = Code::GetCodeFromTargetAddress(rinfo->target_address());
    VisitPointer(&target);
    rinfo->set_target_address(Code::cast(target)->instruction_start(), NULL);
  }

  void VisitDebugTarget(RelocInfo* rinfo) {
    ASSERT((RelocInfo::IsJSReturn(rinfo->rmode()) &&
            rinfo->IsPatchedReturnSequence()) ||
           (RelocInfo::IsDebugBreakSlot(rinfo->rmode()) &&
            rinfo->IsPatchedDebugBreakSlotSequence()));
    Object* target = Code::GetCodeFromTargetAddress(rinfo->call_address());
    VisitPointer(&target);
    rinfo->set_call_address(Code::cast(target)->instruction_start());
  }

 private:
  inline void UpdatePointer(Object** p) {
    if (!(*p)->IsHeapObject()) return;

    HeapObject* obj = HeapObject::cast(*p);

    if (heap_->InNewSpace(obj) ||
        MarkCompactCollector::IsOnEvacuationCandidate(obj)) {
      ASSERT(obj->map_word().IsForwardingAddress());
      *p = obj->map_word().ToForwardingAddress();
      ASSERT(!MarkCompactCollector::IsOnEvacuationCandidate(*p));
    }
  }

  Heap* heap_;
};


static void UpdatePointer(HeapObject** p, HeapObject* object) {
  ASSERT(*p == object);

  Address old_addr = object->address();

  Address new_addr = Memory::Address_at(old_addr);

  // The new space sweep will overwrite the map word of dead objects
  // with NULL. In this case we do not need to transfer this entry to
  // the store buffer which we are rebuilding.
  if (new_addr != NULL) {
    *p = HeapObject::FromAddress(new_addr);
  } else {
    // We have to zap this pointer, because the store buffer may overflow later,
    // and then we have to scan the entire heap and we don't want to find
    // spurious newspace pointers in the old space.
    *p = HeapObject::FromAddress(NULL);  // Fake heap object not in new space.
  }
}


static String* UpdateReferenceInExternalStringTableEntry(Heap* heap,
                                                         Object** p) {
  MapWord map_word = HeapObject::cast(*p)->map_word();

  if (map_word.IsForwardingAddress()) {
    return String::cast(map_word.ToForwardingAddress());
  }

  return String::cast(*p);
}


bool MarkCompactCollector::TryPromoteObject(HeapObject* object,
                                            int object_size) {
  Object* result;

  if (object_size > heap()->MaxObjectSizeInPagedSpace()) {
    MaybeObject* maybe_result =
        heap()->lo_space()->AllocateRawFixedArray(object_size);
    if (maybe_result->ToObject(&result)) {
      HeapObject* target = HeapObject::cast(result);
      MigrateObject(target->address(),
                    object->address(),
                    object_size,
                    LO_SPACE);
      heap()->mark_compact_collector()->tracer()->
          increment_promoted_objects_size(object_size);
      return true;
    }
  } else {
    OldSpace* target_space = heap()->TargetSpace(object);

    ASSERT(target_space == heap()->old_pointer_space() ||
           target_space == heap()->old_data_space());
    MaybeObject* maybe_result = target_space->AllocateRaw(object_size);
    if (maybe_result->ToObject(&result)) {
      HeapObject* target = HeapObject::cast(result);
      MigrateObject(target->address(),
                    object->address(),
                    object_size,
                    target_space->identity());
      heap()->mark_compact_collector()->tracer()->
          increment_promoted_objects_size(object_size);
      return true;
    }
  }

  return false;
}


void MarkCompactCollector::EvacuateNewSpace() {
  heap()->CheckNewSpaceExpansionCriteria();

  NewSpace* new_space = heap()->new_space();

  // Store allocation range before flipping semispaces.
  Address from_bottom = new_space->bottom();
  Address from_top = new_space->top();

  // Flip the semispaces.  After flipping, to space is empty, from space has
  // live objects.
  new_space->Flip();
  new_space->ResetAllocationInfo();

  int survivors_size = 0;

  // First pass: traverse all objects in inactive semispace, remove marks,
  // migrate live objects and write forwarding addresses.  This stage puts
  // new entries in the store buffer and may cause some pages to be marked
  // scan-on-scavenge.
  SemiSpaceIterator from_it(from_bottom, from_top);
  for (HeapObject* object = from_it.Next();
       object != NULL;
       object = from_it.Next()) {
    MarkBit mark_bit = Marking::MarkBitFrom(object);
    if (mark_bit.Get()) {
      mark_bit.Clear();

      int size = object->Size();
      survivors_size += size;

      // Aggressively promote young survivors to the old space.
      if (TryPromoteObject(object, size)) {
        continue;
      }

      // Promotion failed. Just migrate object to another semispace.
      MaybeObject* allocation = new_space->AllocateRaw(size);
      if (allocation->IsFailure()) {
        if (!new_space->AddFreshPage()) {
          // Shouldn't happen. We are sweeping linearly, and to-space
          // has the same number of pages as from-space, so there is
          // always room.
          UNREACHABLE();
        }
        allocation = new_space->AllocateRaw(size);
        ASSERT(!allocation->IsFailure());
      }
      Object* target = allocation->ToObjectUnchecked();

      MigrateObject(HeapObject::cast(target)->address(),
                    object->address(),
                    size,
                    NEW_SPACE);
    } else {
      // Process the dead object before we write a NULL into its header.
      LiveObjectList::ProcessNonLive(object);

      // Mark dead objects in the new space with null in their map field.
      Memory::Address_at(object->address()) = NULL;
    }
  }

  heap_->IncrementYoungSurvivorsCounter(survivors_size);
  new_space->set_age_mark(new_space->top());
}


void MarkCompactCollector::EvacuateLiveObjectsFromPage(Page* p) {
  AlwaysAllocateScope always_allocate;

  PagedSpace* space = static_cast<PagedSpace*>(p->owner());

  MarkBit::CellType* cells = p->markbits()->cells();

  int last_cell_index =
      Bitmap::IndexToCell(
          Bitmap::CellAlignIndex(
              p->AddressToMarkbitIndex(p->ObjectAreaEnd())));

  int cell_index = Page::kFirstUsedCell;
  Address cell_base = p->ObjectAreaStart();
  int offsets[16];

  for (cell_index = Page::kFirstUsedCell;
       cell_index < last_cell_index;
       cell_index++, cell_base += 32 * kPointerSize) {
    ASSERT((unsigned)cell_index ==
        Bitmap::IndexToCell(
            Bitmap::CellAlignIndex(
                p->AddressToMarkbitIndex(cell_base))));
    if (cells[cell_index] == 0) continue;

    int live_objects = MarkWordToObjectStarts(cells[cell_index], offsets);
    for (int i = 0; i < live_objects; i++) {
      Address object_addr = cell_base + offsets[i] * kPointerSize;
      HeapObject* object = HeapObject::FromAddress(object_addr);
      ASSERT(Marking::IsBlack(Marking::MarkBitFrom(object)));

      int size = object->Size();

      // This should never fail as we are in always allocate scope.
      Object* target = space->AllocateRaw(size)->ToObjectUnchecked();

      MigrateObject(HeapObject::cast(target)->address(),
                    object_addr,
                    size,
                    space->identity());
      ASSERT(object->map_word().IsForwardingAddress());
    }
  }
}


void MarkCompactCollector::EvacuatePages() {
  int npages = evacuation_candidates_.length();
  for (int i = 0; i < npages; i++) {
    Page* p = evacuation_candidates_[i];
    EvacuateLiveObjectsFromPage(p);
  }
}


void MarkCompactCollector::EvacuateNewSpaceAndCandidates() {
  EvacuateNewSpace();
  EvacuatePages();

  // Second pass: find pointers to new space and update them.
  PointersUpdatingVisitor updating_visitor(heap());

  // Update pointers in to space.
  SemiSpaceIterator to_it(heap()->new_space()->bottom(),
                          heap()->new_space()->top());
  for (HeapObject* object = to_it.Next();
       object != NULL;
       object = to_it.Next()) {
    Map* map = object->map();
    object->IterateBody(map->instance_type(),
                        object->SizeFromMap(map),
                        &updating_visitor);
  }

  // Update roots.
  heap_->IterateRoots(&updating_visitor, VISIT_ALL_IN_SWEEP_NEWSPACE);
  LiveObjectList::IterateElements(&updating_visitor);

  {
    StoreBufferRebuildScope scope(heap_,
                                  heap_->store_buffer(),
                                  &Heap::ScavengeStoreBufferCallback);
    heap_->store_buffer()->IteratePointersToNewSpace(&UpdatePointer);
  }
  slots_buffer_.Iterate(&updating_visitor);

  // Update pointers from cells.
  HeapObjectIterator cell_iterator(heap_->cell_space());
  for (HeapObject* cell = cell_iterator.Next();
       cell != NULL;
       cell = cell_iterator.Next()) {
    if (cell->IsJSGlobalPropertyCell()) {
      Address value_address =
          reinterpret_cast<Address>(cell) +
          (JSGlobalPropertyCell::kValueOffset - kHeapObjectTag);
      updating_visitor.VisitPointer(reinterpret_cast<Object**>(value_address));
    }
  }

  // Update pointer from the global contexts list.
  updating_visitor.VisitPointer(heap_->global_contexts_list_address());

  heap_->symbol_table()->Iterate(&updating_visitor);

  // Update pointers from external string table.
  heap_->UpdateReferencesInExternalStringTable(
      &UpdateReferenceInExternalStringTableEntry);

  // Update JSFunction pointers from the runtime profiler.
  heap_->isolate()->runtime_profiler()->UpdateSamplesAfterScavenge();

#ifdef DEBUG
  if (FLAG_verify_heap) {
    VerifyEvacuation(heap_);
  }
#endif

  int npages = evacuation_candidates_.length();
  for (int i = 0; i < npages; i++) {
    Page* p = evacuation_candidates_[i];
    PagedSpace* space = static_cast<PagedSpace*>(p->owner());
    space->Free(p->ObjectAreaStart(), Page::kObjectAreaSize);
    p->set_scan_on_scavenge(false);

    // We are not clearing evacuation candidate flag here
    // because it is required to notify lazy sweeper to skip
    // these pages.
  }
}


INLINE(static uint32_t SweepFree(PagedSpace* space,
                                 Page* p,
                                 uint32_t free_start,
                                 uint32_t region_end,
                                 uint32_t* cells));


static uint32_t SweepFree(PagedSpace* space,
                          Page* p,
                          uint32_t free_start,
                          uint32_t region_end,
                          uint32_t* cells) {
  uint32_t free_cell_index = Bitmap::IndexToCell(free_start);
  ASSERT(cells[free_cell_index] == 0);
  while (free_cell_index < region_end && cells[free_cell_index] == 0) {
    free_cell_index++;
  }

  if (free_cell_index >= region_end) {
    return free_cell_index;
  }

  uint32_t free_end = Bitmap::CellToIndex(free_cell_index);
  space->FreeOrUnmapPage(p,
                         p->MarkbitIndexToAddress(free_start),
                         (free_end - free_start) << kPointerSizeLog2);

  return free_cell_index;
}


INLINE(static uint32_t NextCandidate(uint32_t cell_index,
                                     uint32_t last_cell_index,
                                     uint32_t* cells));


static uint32_t NextCandidate(uint32_t cell_index,
                              uint32_t last_cell_index,
                              uint32_t* cells) {
  do {
    cell_index++;
  } while (cell_index < last_cell_index && cells[cell_index] != 0);
  return cell_index;
}


static const int kStartTableEntriesPerLine = 5;
static const int kStartTableLines = 171;
static const int kStartTableInvalidLine = 127;
static const int kStartTableUnusedEntry = 126;

#define _ kStartTableUnusedEntry
#define X kStartTableInvalidLine
// Mark-bit to object start offset table.
//
// The line is indexed by the mark bits in a byte.  The first number on
// the line describes the number of live object starts for the line and the
// other numbers on the line describe the offsets (in words) of the object
// starts.
//
// Since objects are at least 2 words large we don't have entries for two
// consecutive 1 bits.  All entries after 170 have at least 2 consecutive bits.
char kStartTable[kStartTableLines * kStartTableEntriesPerLine] = {
  0, _, _, _, _,  // 0
  1, 0, _, _, _,  // 1
  1, 1, _, _, _,  // 2
  X, _, _, _, _,  // 3
  1, 2, _, _, _,  // 4
  2, 0, 2, _, _,  // 5
  X, _, _, _, _,  // 6
  X, _, _, _, _,  // 7
  1, 3, _, _, _,  // 8
  2, 0, 3, _, _,  // 9
  2, 1, 3, _, _,  // 10
  X, _, _, _, _,  // 11
  X, _, _, _, _,  // 12
  X, _, _, _, _,  // 13
  X, _, _, _, _,  // 14
  X, _, _, _, _,  // 15
  1, 4, _, _, _,  // 16
  2, 0, 4, _, _,  // 17
  2, 1, 4, _, _,  // 18
  X, _, _, _, _,  // 19
  2, 2, 4, _, _,  // 20
  3, 0, 2, 4, _,  // 21
  X, _, _, _, _,  // 22
  X, _, _, _, _,  // 23
  X, _, _, _, _,  // 24
  X, _, _, _, _,  // 25
  X, _, _, _, _,  // 26
  X, _, _, _, _,  // 27
  X, _, _, _, _,  // 28
  X, _, _, _, _,  // 29
  X, _, _, _, _,  // 30
  X, _, _, _, _,  // 31
  1, 5, _, _, _,  // 32
  2, 0, 5, _, _,  // 33
  2, 1, 5, _, _,  // 34
  X, _, _, _, _,  // 35
  2, 2, 5, _, _,  // 36
  3, 0, 2, 5, _,  // 37
  X, _, _, _, _,  // 38
  X, _, _, _, _,  // 39
  2, 3, 5, _, _,  // 40
  3, 0, 3, 5, _,  // 41
  3, 1, 3, 5, _,  // 42
  X, _, _, _, _,  // 43
  X, _, _, _, _,  // 44
  X, _, _, _, _,  // 45
  X, _, _, _, _,  // 46
  X, _, _, _, _,  // 47
  X, _, _, _, _,  // 48
  X, _, _, _, _,  // 49
  X, _, _, _, _,  // 50
  X, _, _, _, _,  // 51
  X, _, _, _, _,  // 52
  X, _, _, _, _,  // 53
  X, _, _, _, _,  // 54
  X, _, _, _, _,  // 55
  X, _, _, _, _,  // 56
  X, _, _, _, _,  // 57
  X, _, _, _, _,  // 58
  X, _, _, _, _,  // 59
  X, _, _, _, _,  // 60
  X, _, _, _, _,  // 61
  X, _, _, _, _,  // 62
  X, _, _, _, _,  // 63
  1, 6, _, _, _,  // 64
  2, 0, 6, _, _,  // 65
  2, 1, 6, _, _,  // 66
  X, _, _, _, _,  // 67
  2, 2, 6, _, _,  // 68
  3, 0, 2, 6, _,  // 69
  X, _, _, _, _,  // 70
  X, _, _, _, _,  // 71
  2, 3, 6, _, _,  // 72
  3, 0, 3, 6, _,  // 73
  3, 1, 3, 6, _,  // 74
  X, _, _, _, _,  // 75
  X, _, _, _, _,  // 76
  X, _, _, _, _,  // 77
  X, _, _, _, _,  // 78
  X, _, _, _, _,  // 79
  2, 4, 6, _, _,  // 80
  3, 0, 4, 6, _,  // 81
  3, 1, 4, 6, _,  // 82
  X, _, _, _, _,  // 83
  3, 2, 4, 6, _,  // 84
  4, 0, 2, 4, 6,  // 85
  X, _, _, _, _,  // 86
  X, _, _, _, _,  // 87
  X, _, _, _, _,  // 88
  X, _, _, _, _,  // 89
  X, _, _, _, _,  // 90
  X, _, _, _, _,  // 91
  X, _, _, _, _,  // 92
  X, _, _, _, _,  // 93
  X, _, _, _, _,  // 94
  X, _, _, _, _,  // 95
  X, _, _, _, _,  // 96
  X, _, _, _, _,  // 97
  X, _, _, _, _,  // 98
  X, _, _, _, _,  // 99
  X, _, _, _, _,  // 100
  X, _, _, _, _,  // 101
  X, _, _, _, _,  // 102
  X, _, _, _, _,  // 103
  X, _, _, _, _,  // 104
  X, _, _, _, _,  // 105
  X, _, _, _, _,  // 106
  X, _, _, _, _,  // 107
  X, _, _, _, _,  // 108
  X, _, _, _, _,  // 109
  X, _, _, _, _,  // 110
  X, _, _, _, _,  // 111
  X, _, _, _, _,  // 112
  X, _, _, _, _,  // 113
  X, _, _, _, _,  // 114
  X, _, _, _, _,  // 115
  X, _, _, _, _,  // 116
  X, _, _, _, _,  // 117
  X, _, _, _, _,  // 118
  X, _, _, _, _,  // 119
  X, _, _, _, _,  // 120
  X, _, _, _, _,  // 121
  X, _, _, _, _,  // 122
  X, _, _, _, _,  // 123
  X, _, _, _, _,  // 124
  X, _, _, _, _,  // 125
  X, _, _, _, _,  // 126
  X, _, _, _, _,  // 127
  1, 7, _, _, _,  // 128
  2, 0, 7, _, _,  // 129
  2, 1, 7, _, _,  // 130
  X, _, _, _, _,  // 131
  2, 2, 7, _, _,  // 132
  3, 0, 2, 7, _,  // 133
  X, _, _, _, _,  // 134
  X, _, _, _, _,  // 135
  2, 3, 7, _, _,  // 136
  3, 0, 3, 7, _,  // 137
  3, 1, 3, 7, _,  // 138
  X, _, _, _, _,  // 139
  X, _, _, _, _,  // 140
  X, _, _, _, _,  // 141
  X, _, _, _, _,  // 142
  X, _, _, _, _,  // 143
  2, 4, 7, _, _,  // 144
  3, 0, 4, 7, _,  // 145
  3, 1, 4, 7, _,  // 146
  X, _, _, _, _,  // 147
  3, 2, 4, 7, _,  // 148
  4, 0, 2, 4, 7,  // 149
  X, _, _, _, _,  // 150
  X, _, _, _, _,  // 151
  X, _, _, _, _,  // 152
  X, _, _, _, _,  // 153
  X, _, _, _, _,  // 154
  X, _, _, _, _,  // 155
  X, _, _, _, _,  // 156
  X, _, _, _, _,  // 157
  X, _, _, _, _,  // 158
  X, _, _, _, _,  // 159
  2, 5, 7, _, _,  // 160
  3, 0, 5, 7, _,  // 161
  3, 1, 5, 7, _,  // 162
  X, _, _, _, _,  // 163
  3, 2, 5, 7, _,  // 164
  4, 0, 2, 5, 7,  // 165
  X, _, _, _, _,  // 166
  X, _, _, _, _,  // 167
  3, 3, 5, 7, _,  // 168
  4, 0, 3, 5, 7,  // 169
  4, 1, 3, 5, 7   // 170
};
#undef _
#undef X


// Takes a word of mark bits.  Returns the number of objects that start in the
// range.  Puts the offsets of the words in the supplied array.
static inline int MarkWordToObjectStarts(uint32_t mark_bits, int* starts) {
  int objects = 0;
  int offset = 0;

  // No consecutive 1 bits.
  ASSERT((mark_bits & 0x180) != 0x180);
  ASSERT((mark_bits & 0x18000) != 0x18000);
  ASSERT((mark_bits & 0x1800000) != 0x1800000);

  while (mark_bits != 0) {
    int byte = (mark_bits & 0xff);
    mark_bits >>= 8;
    if (byte != 0) {
      ASSERT(byte < kStartTableLines);  // No consecutive 1 bits.
      char* table = kStartTable + byte * kStartTableEntriesPerLine;
      int objects_in_these_8_words = table[0];
      ASSERT(objects_in_these_8_words != kStartTableInvalidLine);
      ASSERT(objects_in_these_8_words < kStartTableEntriesPerLine);
      for (int i = 0; i < objects_in_these_8_words; i++) {
        starts[objects++] = offset + table[1 + i];
      }
    }
    offset += 8;
  }
  return objects;
}


static inline Address DigestFreeStart(Address approximate_free_start,
                                      uint32_t free_start_cell) {
  ASSERT(free_start_cell != 0);

  int offsets[16];
  uint32_t cell = free_start_cell;
  int offset_of_last_live;
  if ((cell & 0x80000000u) != 0) {
    // This case would overflow below.
    offset_of_last_live = 31;
  } else {
    // Remove all but one bit, the most significant.  This is an optimization
    // that may or may not be worthwhile.
    cell |= cell >> 16;
    cell |= cell >> 8;
    cell |= cell >> 4;
    cell |= cell >> 2;
    cell |= cell >> 1;
    cell = (cell + 1) >> 1;
    int live_objects = MarkWordToObjectStarts(cell, offsets);
    ASSERT(live_objects == 1);
    offset_of_last_live = offsets[live_objects - 1];
  }
  Address last_live_start =
      approximate_free_start + offset_of_last_live * kPointerSize;
  HeapObject* last_live = HeapObject::FromAddress(last_live_start);
  Address free_start = last_live_start + last_live->Size();
  return free_start;
}


static inline Address StartOfLiveObject(Address block_address, uint32_t cell) {
  ASSERT(cell != 0);

  int offsets[16];
  if (cell == 0x80000000u) {  // Avoid overflow below.
    return block_address + 31 * kPointerSize;
  }
  uint32_t first_set_bit = ((cell ^ (cell - 1)) + 1) >> 1;
  ASSERT((first_set_bit & cell) == first_set_bit);
  int live_objects = MarkWordToObjectStarts(first_set_bit, offsets);
  ASSERT(live_objects == 1);
  USE(live_objects);
  return block_address + offsets[0] * kPointerSize;
}


// Sweeps a space conservatively.  After this has been done the larger free
// spaces have been put on the free list and the smaller ones have been
// ignored and left untouched.  A free space is always either ignored or put
// on the free list, never split up into two parts.  This is important
// because it means that any FreeSpace maps left actually describe a region of
// memory that can be ignored when scanning.  Dead objects other than free
// spaces will not contain the free space map.
int MarkCompactCollector::SweepConservatively(PagedSpace* space, Page* p) {
  // We might start advancing sweeper before evacuation happened.
  if (p->IsEvacuationCandidate()) return 0;

  int freed_bytes = 0;

  MarkBit::CellType* cells = p->markbits()->cells();

  p->SetFlag(MemoryChunk::WAS_SWEPT_CONSERVATIVELY);

  // This is the start of the 32 word block that we are currently looking at.
  Address block_address = p->ObjectAreaStart();

  int last_cell_index =
      Bitmap::IndexToCell(
          Bitmap::CellAlignIndex(
              p->AddressToMarkbitIndex(p->ObjectAreaEnd())));

  int cell_index = Page::kFirstUsedCell;

  // Skip over all the dead objects at the start of the page and mark them free.
  for (cell_index = Page::kFirstUsedCell;
       cell_index < last_cell_index;
       cell_index++, block_address += 32 * kPointerSize) {
    if (cells[cell_index] != 0) break;
  }
  int size = block_address - p->ObjectAreaStart();
  if (cell_index == last_cell_index) {
    freed_bytes += space->Free(p->ObjectAreaStart(), size);
    return freed_bytes;
  }
  // Grow the size of the start-of-page free space a little to get up to the
  // first live object.
  Address free_end = StartOfLiveObject(block_address, cells[cell_index]);
  // Free the first free space.
  size = free_end - p->ObjectAreaStart();
  freed_bytes += space->Free(p->ObjectAreaStart(), size);
  // The start of the current free area is represented in undigested form by
  // the address of the last 32-word section that contained a live object and
  // the marking bitmap for that cell, which describes where the live object
  // started.  Unless we find a large free space in the bitmap we will not
  // digest this pair into a real address.  We start the iteration here at the
  // first word in the marking bit map that indicates a live object.
  Address free_start = block_address;
  uint32_t free_start_cell = cells[cell_index];

  for ( ;
       cell_index < last_cell_index;
       cell_index++, block_address += 32 * kPointerSize) {
    ASSERT((unsigned)cell_index ==
        Bitmap::IndexToCell(
            Bitmap::CellAlignIndex(
                p->AddressToMarkbitIndex(block_address))));
    uint32_t cell = cells[cell_index];
    if (cell != 0) {
      // We have a live object.  Check approximately whether it is more than 32
      // words since the last live object.
      if (block_address - free_start > 32 * kPointerSize) {
        free_start = DigestFreeStart(free_start, free_start_cell);
        if (block_address - free_start > 32 * kPointerSize) {
          // Now that we know the exact start of the free space it still looks
          // like we have a large enough free space to be worth bothering with.
          // so now we need to find the start of the first live object at the
          // end of the free space.
          free_end = StartOfLiveObject(block_address, cell);
          freed_bytes += space->Free(free_start, free_end - free_start);
        }
      }
      // Update our undigested record of where the current free area started.
      free_start = block_address;
      free_start_cell = cell;
    }
  }

  // Handle the free space at the end of the page.
  if (block_address - free_start > 32 * kPointerSize) {
    free_start = DigestFreeStart(free_start, free_start_cell);
    freed_bytes += space->Free(free_start, block_address - free_start);
  }

  return freed_bytes;
}


// Sweep a space precisely.  After this has been done the space can
// be iterated precisely, hitting only the live objects.  Code space
// is always swept precisely because we want to be able to iterate
// over it.  Map space is swept precisely, because it is not compacted.
static void SweepPrecisely(PagedSpace* space,
                           Page* p) {
  MarkBit::CellType* cells = p->markbits()->cells();

  p->ClearFlag(MemoryChunk::WAS_SWEPT_CONSERVATIVELY);

  int last_cell_index =
      Bitmap::IndexToCell(
          Bitmap::CellAlignIndex(
              p->AddressToMarkbitIndex(p->ObjectAreaEnd())));

  int cell_index = Page::kFirstUsedCell;
  Address free_start = p->ObjectAreaStart();
  ASSERT(reinterpret_cast<intptr_t>(free_start) % (32 * kPointerSize) == 0);
  Address object_address = p->ObjectAreaStart();
  int offsets[16];

  for (cell_index = Page::kFirstUsedCell;
       cell_index < last_cell_index;
       cell_index++, object_address += 32 * kPointerSize) {
    ASSERT((unsigned)cell_index ==
        Bitmap::IndexToCell(
            Bitmap::CellAlignIndex(
                p->AddressToMarkbitIndex(object_address))));
    int live_objects = MarkWordToObjectStarts(cells[cell_index], offsets);
    int live_index = 0;
    for ( ; live_objects != 0; live_objects--) {
      Address free_end = object_address + offsets[live_index++] * kPointerSize;
      if (free_end != free_start) {
        space->Free(free_start, free_end - free_start);
      }
      HeapObject* live_object = HeapObject::FromAddress(free_end);
      ASSERT(Marking::IsBlack(Marking::MarkBitFrom(live_object)));
      free_start = free_end + live_object->Size();
    }
  }
  if (free_start != p->ObjectAreaEnd()) {
    space->Free(free_start, p->ObjectAreaEnd() - free_start);
  }
}


void MarkCompactCollector::SweepSpace(PagedSpace* space,
                                      SweeperType sweeper) {
  space->set_was_swept_conservatively(sweeper == CONSERVATIVE ||
                                      sweeper == LAZY_CONSERVATIVE);

  space->ClearStats();

  PageIterator it(space);

  int freed_bytes = 0;
  int newspace_size = space->heap()->new_space()->Size();

  while (it.has_next()) {
    Page* p = it.next();

    if (p->IsEvacuationCandidate()) {
      ASSERT(evacuation_candidates_.length() > 0);
      continue;
    }

    switch (sweeper) {
      case CONSERVATIVE: {
        SweepConservatively(space, p);
        break;
      }
      case LAZY_CONSERVATIVE: {
        Page* next_page = p->next_page();
        freed_bytes += SweepConservatively(space, p);
        // TODO(gc): tweak the heuristic.
        if (freed_bytes >= newspace_size && p != space->LastPage()) {
          space->SetPagesToSweep(next_page, space->LastPage());
          return;
        }
        break;
      }
      case PRECISE: {
        SweepPrecisely(space, p);
        break;
      }
      default: {
        UNREACHABLE();
      }
    }
  }

  // TODO(gc): set up allocation top and limit using the free list.
}


void MarkCompactCollector::SweepSpaces() {
  GCTracer::Scope gc_scope(tracer_, GCTracer::Scope::MC_SWEEP);
#ifdef DEBUG
  state_ = SWEEP_SPACES;
#endif
  SweeperType how_to_sweep =
      FLAG_lazy_sweeping ? LAZY_CONSERVATIVE : CONSERVATIVE;
  if (sweep_precisely_) how_to_sweep = PRECISE;
  // Noncompacting collections simply sweep the spaces to clear the mark
  // bits and free the nonlive blocks (for old and map spaces).  We sweep
  // the map space last because freeing non-live maps overwrites them and
  // the other spaces rely on possibly non-live maps to get the sizes for
  // non-live objects.
  SweepSpace(heap()->old_pointer_space(), how_to_sweep);
  SweepSpace(heap()->old_data_space(), how_to_sweep);
  SweepSpace(heap()->code_space(), PRECISE);
  // TODO(gc): implement specialized sweeper for cell space.
  SweepSpace(heap()->cell_space(), PRECISE);
  { GCTracer::Scope gc_scope(tracer_, GCTracer::Scope::MC_SWEEP_NEWSPACE);
    EvacuateNewSpaceAndCandidates();
  }
  // TODO(gc): ClearNonLiveTransitions depends on precise sweeping of
  // map space to detect whether unmarked map became dead in this
  // collection or in one of the previous ones.
  // TODO(gc): Implement specialized sweeper for map space.
  SweepSpace(heap()->map_space(), PRECISE);

  ASSERT(live_map_objects_size_ <= heap()->map_space()->Size());

  // Deallocate unmarked objects and clear marked bits for marked objects.
  heap_->lo_space()->FreeUnmarkedObjects();
}


// Iterate the live objects in a range of addresses (eg, a page or a
// semispace).  The live regions of the range have been linked into a list.
// The first live region is [first_live_start, first_live_end), and the last
// address in the range is top.  The callback function is used to get the
// size of each live object.
int MarkCompactCollector::IterateLiveObjectsInRange(
    Address start,
    Address end,
    LiveObjectCallback size_func) {
  int live_objects_size = 0;
  Address current = start;
  while (current < end) {
    uint32_t encoded_map = Memory::uint32_at(current);
    if (encoded_map == kSingleFreeEncoding) {
      current += kPointerSize;
    } else if (encoded_map == kMultiFreeEncoding) {
      current += Memory::int_at(current + kIntSize);
    } else {
      int size = (this->*size_func)(HeapObject::FromAddress(current));
      current += size;
      live_objects_size += size;
    }
  }
  return live_objects_size;
}


int MarkCompactCollector::IterateLiveObjects(
    NewSpace* space, LiveObjectCallback size_f) {
  ASSERT(MARK_LIVE_OBJECTS < state_ && state_ <= RELOCATE_OBJECTS);
  int accumulator = 0;
  Address end = space->top();
  NewSpacePageIterator it(space->bottom(), end);
  // The bottom is at the start of its page.
  ASSERT_EQ(space->bottom(),
            NewSpacePage::FromAddress(space->bottom())->body());
  while (it.has_next()) {
    NewSpacePage* page = it.next();
    Address start = page->body();
    Address limit = it.has_next() ? page->body_limit() : end;
    accumulator += IterateLiveObjectsInRange(start, limit, size_f);
  }
  return accumulator;
}


int MarkCompactCollector::IterateLiveObjects(
    PagedSpace* space, LiveObjectCallback size_f) {
  ASSERT(MARK_LIVE_OBJECTS < state_ && state_ <= RELOCATE_OBJECTS);
  // TODO(gc): Do a mark-sweep first with precise sweeping.
  int total = 0;
  PageIterator it(space);
  while (it.has_next()) {
    Page* p = it.next();
    total += IterateLiveObjectsInRange(p->ObjectAreaStart(),
                                       p->ObjectAreaEnd(),
                                       size_f);
  }
  return total;
}


// TODO(gc) ReportDeleteIfNeeded is not called currently.
// Our profiling tools do not expect intersections between
// code objects. We should either reenable it or change our tools.
void MarkCompactCollector::EnableCodeFlushing(bool enable) {
  if (enable) {
    if (code_flusher_ != NULL) return;
    code_flusher_ = new CodeFlusher(heap()->isolate());
  } else {
    if (code_flusher_ == NULL) return;
    delete code_flusher_;
    code_flusher_ = NULL;
  }
}


void MarkCompactCollector::ReportDeleteIfNeeded(HeapObject* obj,
                                                Isolate* isolate) {
#ifdef ENABLE_GDB_JIT_INTERFACE
  if (obj->IsCode()) {
    GDBJITInterface::RemoveCode(reinterpret_cast<Code*>(obj));
  }
#endif
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (obj->IsCode()) {
    PROFILE(isolate, CodeDeleteEvent(obj->address()));
  }
#endif
}


void MarkCompactCollector::Initialize() {
  StaticMarkingVisitor::Initialize();
}


SlotsBuffer::SlotsBuffer()
    : buffers_(0),
      buffer_(NULL),
      idx_(kBufferSize),
      buffer_idx_(-1) {
}


SlotsBuffer::~SlotsBuffer() {
  for (int buffer_index = 0; buffer_index < buffers_.length(); ++buffer_index) {
    delete buffers_[buffer_index];
  }
}


void SlotsBuffer::Clear() {
  idx_ = kBufferSize;
  buffer_idx_ = -1;
}


void SlotsBuffer::Add(Object** slot) {
  if (idx_ == kBufferSize) {
    idx_ = 0;
    buffer_idx_++;
    if (buffer_idx_ == buffers_.length()) {
      buffers_.Add(new ObjectSlot[kBufferSize]);
    }
    buffer_ = buffers_[buffer_idx_];
  }

  buffer_[idx_++] = slot;
}


void SlotsBuffer::Iterate(ObjectVisitor* visitor) {
  if (buffer_idx_ < 0) return;

  for (int buffer_index = 0; buffer_index < buffer_idx_; ++buffer_index) {
    ObjectSlot* buffer = buffers_[buffer_index];
    for (int slot_idx = 0; slot_idx < kBufferSize; ++slot_idx) {
      visitor->VisitPointer(buffer[slot_idx]);
    }
  }

  ObjectSlot* last_buffer = buffers_[buffer_idx_];
  for (int slot_idx = 0; slot_idx < idx_; ++slot_idx) {
    visitor->VisitPointer(last_buffer[slot_idx]);
  }
}


void SlotsBuffer::Report() {
}


} }  // namespace v8::internal
