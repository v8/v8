// Copyright 2006-2008 the V8 project authors. All rights reserved.
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
#include "heap-profiler.h"
#include "gdb-jit.h"
#include "global-handles.h"
#include "ic-inl.h"
#include "liveobjectlist-inl.h"
#include "mark-compact.h"
#include "objects-visiting.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {

// -------------------------------------------------------------------------
// MarkCompactCollector

bool MarkCompactCollector::force_compaction_ = false;
bool MarkCompactCollector::compacting_collection_ = false;
bool MarkCompactCollector::compact_on_next_gc_ = false;

GCTracer* MarkCompactCollector::tracer_ = NULL;

#ifdef DEBUG
MarkCompactCollector::CollectorState MarkCompactCollector::state_ = IDLE;

// Counters used for debugging the marking phase of mark-compact or mark-sweep
// collection.
int MarkCompactCollector::live_bytes_ = 0;
int MarkCompactCollector::live_young_objects_size_ = 0;
int MarkCompactCollector::live_old_data_objects_size_ = 0;
int MarkCompactCollector::live_old_pointer_objects_size_ = 0;
int MarkCompactCollector::live_code_objects_size_ = 0;
int MarkCompactCollector::live_map_objects_size_ = 0;
int MarkCompactCollector::live_cell_objects_size_ = 0;
int MarkCompactCollector::live_lo_objects_size_ = 0;
#endif


Marking::NewSpaceMarkbitsBitmap* Marking::new_space_bitmap_ = NULL;


bool Marking::Setup() {
  if (new_space_bitmap_ == NULL) {
    int markbits_per_newspace =
        (2*Heap::ReservedSemiSpaceSize()) >> kPointerSizeLog2;

    new_space_bitmap_ =
        BitmapStorageDescriptor::Allocate(
            NewSpaceMarkbitsBitmap::CellsForLength(markbits_per_newspace));
  }
  return new_space_bitmap_ != NULL;
}


void Marking::TearDown() {
  if (new_space_bitmap_ != NULL) {
    BitmapStorageDescriptor::Free(new_space_bitmap_);
    new_space_bitmap_ = NULL;
  }
}


#ifdef DEBUG
class VerifyMarkingVisitor: public ObjectVisitor {
 public:
  void VisitPointers(Object** start, Object** end) {
    for (Object** current = start; current < end; current++) {
      if ((*current)->IsHeapObject()) {
        HeapObject* object = HeapObject::cast(*current);
        ASSERT(Marking::IsMarked(object));
      }
    }
  }
};


static void VerifyMarking(Address bottom, Address top) {
  VerifyMarkingVisitor visitor;
  HeapObject* object;

  for (Address current = bottom;
       current < top;
       current += object->Size()) {
    object = HeapObject::FromAddress(current);
    if (Marking::IsMarked(object)) object->Iterate(&visitor);
  }
}


static void VerifyMarking(Page* p) {
  VerifyMarking(p->ObjectAreaStart(), p->AllocationTop());
}


static void VerifyMarking(NewSpace* space) {
  VerifyMarking(space->bottom(), space->top());
}


static void VerifyMarking(PagedSpace* space) {
  PageIterator it(space, PageIterator::PAGES_IN_USE);

  while (it.has_next()) {
    VerifyMarking(it.next());
  }
}


static void VerifyMarking() {
  VerifyMarking(Heap::old_pointer_space());
  VerifyMarking(Heap::old_data_space());
  VerifyMarking(Heap::code_space());
  VerifyMarking(Heap::cell_space());
  VerifyMarking(Heap::map_space());
  VerifyMarking(Heap::new_space());

  VerifyMarkingVisitor visitor;
  Heap::IterateStrongRoots(&visitor, VISIT_ONLY_STRONG);
}
#endif

void MarkCompactCollector::CollectGarbage() {
  // Make sure that Prepare() has been called. The individual steps below will
  // update the state as they proceed.
  ASSERT(state_ == PREPARE_GC);

  // Prepare has selected whether to compact the old generation or not.
  // Tell the tracer.
  if (IsCompacting()) tracer_->set_is_compacting();

  if (IncrementalMarking::state() == IncrementalMarking::STOPPED) {
    MarkLiveObjects();
  } else {
    {
      GCTracer::Scope gc_scope(tracer_, GCTracer::Scope::MC_MARK);
      IncrementalMarking::Finalize();
      ASSERT(IncrementalMarking::state() == IncrementalMarking::STOPPED);
    }
    MarkLiveObjects();
  }

  if (FLAG_collect_maps) ClearNonLiveTransitions();

#ifdef DEBUG
  VerifyMarking();
#endif

  SweepSpaces();

  PcToCodeCache::FlushPcToCodeCache();

  Finish();

  // Check that swept all marked objects and
  // null out the GC tracer.
  // TODO(gc) does not work with conservative sweeping.
  // ASSERT(tracer_->marked_count() == 0);
  tracer_ = NULL;
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


void Marking::TransferMark(Address old_start, Address new_start) {
  if (old_start == new_start) return;

  if (!IncrementalMarking::IsStopped()) {
    if (IncrementalMarking::IsBlack(HeapObject::FromAddress(old_start))) {
      IncrementalMarking::MarkBlack(HeapObject::FromAddress(new_start));
      ClearMark(old_start);
    } else if (IncrementalMarking::IsGrey(HeapObject::FromAddress(old_start))) {
      ClearMark(old_start + kPointerSize);
      IncrementalMarking::WhiteToGrey(HeapObject::FromAddress(new_start));
      IncrementalMarking::RestartIfNotMarking();
      // TODO(gc): if we shift huge array in the loop we might end up pushing
      // to much to marking stack. maybe we should check one or two elements
      // on top of it to see whether they are equal to old_start.
    }
  } else {
    if (Heap::InNewSpace(old_start) ||
        Page::FromAddress(old_start)->IsFlagSet(Page::IS_CONTINUOUS) ||
        !IsMarked(old_start)) {
      return;
    }
    SetMark(new_start);
  }
}


void MarkCompactCollector::Prepare(GCTracer* tracer) {
  FLAG_flush_code = false;
  FLAG_always_compact = false;
  FLAG_never_compact = true;

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

  compacting_collection_ =
      FLAG_always_compact || force_compaction_ || compact_on_next_gc_;
  compact_on_next_gc_ = false;

  if (FLAG_never_compact) compacting_collection_ = false;
  if (!Heap::map_space()->MapPointersEncodable()) {
    compacting_collection_ = false;
  }
  if (FLAG_collect_maps) CreateBackPointers();
#ifdef ENABLE_GDB_JIT_INTERFACE
  if (FLAG_gdbjit) {
    // If GDBJIT interface is active disable compaction.
    compacting_collection_ = false;
  }
#endif

  PagedSpaces spaces;
  for (PagedSpace* space = spaces.next();
       space != NULL; space = spaces.next()) {
    space->PrepareForMarkCompact(compacting_collection_);
  }

  if (IncrementalMarking::state() == IncrementalMarking::STOPPED) {
    Address new_space_top = Heap::new_space()->top();
    Address new_space_bottom = Heap::new_space()->bottom();

    Marking::ClearRange(new_space_bottom,
                        static_cast<int>(new_space_top - new_space_bottom));

    ClearMarkbits();
#ifdef DEBUG
    VerifyMarkbitsAreClean();
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
  StubCache::Clear();

  ExternalStringTable::CleanUp();

  // If we've just compacted old space there's no reason to check the
  // fragmentation limit. Just return.
  if (HasCompacted()) return;

  // We compact the old generation on the next GC if it has gotten too
  // fragmented (ie, we could recover an expected amount of space by
  // reclaiming the waste and free list blocks).
  static const int kFragmentationLimit = 15;        // Percent.
  static const int kFragmentationAllowed = 1 * MB;  // Absolute.
  intptr_t old_gen_recoverable = 0;
  intptr_t old_gen_used = 0;

  OldSpaces spaces;
  for (OldSpace* space = spaces.next(); space != NULL; space = spaces.next()) {
    old_gen_recoverable += space->Waste() + space->AvailableFree();
    old_gen_used += space->Size();
  }

  int old_gen_fragmentation =
      static_cast<int>((old_gen_recoverable * 100.0) / old_gen_used);
  if (old_gen_fragmentation > kFragmentationLimit &&
      old_gen_recoverable > kFragmentationAllowed) {
    compact_on_next_gc_ = true;
  }
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

static MarkingStack marking_stack;

class FlushCode : public AllStatic {
 public:
  static void AddCandidate(SharedFunctionInfo* shared_info) {
    SetNextCandidate(shared_info, shared_function_info_candidates_head_);
    shared_function_info_candidates_head_ = shared_info;
  }


  static void AddCandidate(JSFunction* function) {
    ASSERT(function->unchecked_code() ==
           function->unchecked_shared()->unchecked_code());

    SetNextCandidate(function, jsfunction_candidates_head_);
    jsfunction_candidates_head_ = function;
  }


  static void ProcessCandidates() {
    ProcessSharedFunctionInfoCandidates();
    ProcessJSFunctionCandidates();
  }

 private:
  static void ProcessJSFunctionCandidates() {
    Code* lazy_compile = Builtins::builtin(Builtins::LazyCompile);

    JSFunction* candidate = jsfunction_candidates_head_;
    JSFunction* next_candidate;
    while (candidate != NULL) {
      next_candidate = GetNextCandidate(candidate);

      SharedFunctionInfo* shared = candidate->unchecked_shared();

      Code* code = shared->unchecked_code();
      if (!Marking::IsMarked(code->address())) {
        shared->set_code(lazy_compile);
        candidate->set_code(lazy_compile);
      } else {
        candidate->set_code(shared->unchecked_code());
      }

      candidate = next_candidate;
    }

    jsfunction_candidates_head_ = NULL;
  }


  static void ProcessSharedFunctionInfoCandidates() {
    Code* lazy_compile = Builtins::builtin(Builtins::LazyCompile);

    SharedFunctionInfo* candidate = shared_function_info_candidates_head_;
    SharedFunctionInfo* next_candidate;
    while (candidate != NULL) {
      next_candidate = GetNextCandidate(candidate);
      SetNextCandidate(candidate, NULL);

      Code* code = candidate->unchecked_code();
      if (!Marking::IsMarked(code->address())) {
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


  STATIC_ASSERT(kPointerSize <= Code::kHeaderSize - Code::kHeaderPaddingStart);


  static SharedFunctionInfo** GetNextCandidateField(
      SharedFunctionInfo* candidate) {
    Code* code = candidate->unchecked_code();
    return reinterpret_cast<SharedFunctionInfo**>(
        code->address() + Code::kHeaderPaddingStart);
  }


  static SharedFunctionInfo* GetNextCandidate(SharedFunctionInfo* candidate) {
    return *GetNextCandidateField(candidate);
  }


  static void SetNextCandidate(SharedFunctionInfo* candidate,
                               SharedFunctionInfo* next_candidate) {
    *GetNextCandidateField(candidate) = next_candidate;
  }

  static JSFunction* jsfunction_candidates_head_;

  static SharedFunctionInfo* shared_function_info_candidates_head_;
};

JSFunction* FlushCode::jsfunction_candidates_head_ = NULL;

SharedFunctionInfo* FlushCode::shared_function_info_candidates_head_ = NULL;

static inline HeapObject* ShortCircuitConsString(Object** p) {
  // Optimization: If the heap object pointed to by p is a non-symbol
  // cons string whose right substring is Heap::empty_string, update
  // it in place to its left substring.  Return the updated value.
  //
  // Here we assume that if we change *p, we replace it with a heap object
  // (ie, the left substring of a cons string is always a heap object).
  //
  // The check performed is:
  //   object->IsConsString() && !object->IsSymbol() &&
  //   (ConsString::cast(object)->second() == Heap::empty_string())
  // except the maps for the object and its possible substrings might be
  // marked.
  HeapObject* object = HeapObject::cast(*p);
  InstanceType type = object->map()->instance_type();
  if ((type & kShortcutTypeMask) != kShortcutTypeTag) return object;

  Object* second = reinterpret_cast<ConsString*>(object)->unchecked_second();
  if (second != Heap::raw_unchecked_empty_string()) {
    return object;
  }

  // Since we don't have the object's start, it is impossible to update the
  // page dirty marks. Therefore, we only replace the string with its left
  // substring when page dirty marks do not change.
  // TODO(gc): Seems like we could relax this restriction with store buffers.
  Object* first = reinterpret_cast<ConsString*>(object)->unchecked_first();
  if (!Heap::InNewSpace(object) && Heap::InNewSpace(first)) return object;

  *p = first;
  return HeapObject::cast(first);
}


class StaticMarkingVisitor : public StaticVisitorBase {
 public:
  static inline void IterateBody(Map* map, HeapObject* obj) {
    table_.GetVisitor(map)(map, obj);
  }

  static void EnableCodeFlushing(bool enabled) {
    if (enabled) {
      table_.Register(kVisitJSFunction, &VisitJSFunctionAndFlushCode);
      table_.Register(kVisitSharedFunctionInfo,
                      &VisitSharedFunctionInfoAndFlushCode);

    } else {
      table_.Register(kVisitJSFunction, &VisitJSFunction);
      table_.Register(kVisitSharedFunctionInfo,
                      &VisitSharedFunctionInfoGeneric);
    }
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

    table_.Register(kVisitGlobalContext,
                    &FixedBodyVisitor<StaticMarkingVisitor,
                                      Context::MarkCompactBodyDescriptor,
                                      void>::Visit);

    table_.Register(kVisitByteArray, &DataObjectVisitor::Visit);
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

  INLINE(static void VisitPointer(Object** p)) {
    MarkObjectByPointer(p);
  }

  INLINE(static void VisitPointers(Object** start, Object** end)) {
    // Mark all objects pointed to in [start, end).
    const int kMinRangeForMarkingRecursion = 64;
    if (end - start >= kMinRangeForMarkingRecursion) {
      if (VisitUnmarkedObjects(start, end)) return;
      // We are close to a stack overflow, so just mark the objects.
    }
    for (Object** p = start; p < end; p++) MarkObjectByPointer(p);
  }

  static inline void VisitCodeTarget(RelocInfo* rinfo) {
    ASSERT(RelocInfo::IsCodeTarget(rinfo->rmode()));
    Code* code = Code::GetCodeFromTargetAddress(rinfo->target_address());
    if (FLAG_cleanup_ics_at_gc && code->is_inline_cache_stub()) {
      IC::Clear(rinfo->pc());
      // Please note targets for cleared inline cached do not have to be
      // marked since they are contained in Heap::non_monomorphic_cache().
    } else {
      MarkCompactCollector::MarkObject(code);
    }
  }

  static void VisitGlobalPropertyCell(RelocInfo* rinfo) {
    ASSERT(rinfo->rmode() == RelocInfo::GLOBAL_PROPERTY_CELL);
    Object* cell = rinfo->target_cell();
    Object* old_cell = cell;
    VisitPointer(&cell);
    if (cell != old_cell) {
      rinfo->set_target_cell(reinterpret_cast<JSGlobalPropertyCell*>(cell),
                             NULL);
    }
  }

  static inline void VisitDebugTarget(RelocInfo* rinfo) {
    ASSERT((RelocInfo::IsJSReturn(rinfo->rmode()) &&
            rinfo->IsPatchedReturnSequence()) ||
           (RelocInfo::IsDebugBreakSlot(rinfo->rmode()) &&
            rinfo->IsPatchedDebugBreakSlotSequence()));
    HeapObject* code = Code::GetCodeFromTargetAddress(rinfo->call_address());
    MarkCompactCollector::MarkObject(code);
  }

  // Mark object pointed to by p.
  INLINE(static void MarkObjectByPointer(Object** p)) {
    if (!(*p)->IsHeapObject()) return;
    HeapObject* object = ShortCircuitConsString(p);
    MarkCompactCollector::MarkObject(object);
  }

  // Visit an unmarked object.
  static inline void VisitUnmarkedObject(HeapObject* obj) {
#ifdef DEBUG
    ASSERT(Heap::Contains(obj));
    ASSERT(!Marking::IsMarked(obj->address()));
#endif
    Map* map = obj->map();
    MarkCompactCollector::SetMark(obj);
    // Mark the map pointer and the body.
    MarkCompactCollector::MarkObject(map);
    IterateBody(map, obj);
  }

  // Visit all unmarked objects pointed to by [start, end).
  // Returns false if the operation fails (lack of stack space).
  static inline bool VisitUnmarkedObjects(Object** start, Object** end) {
    // Return false is we are close to the stack limit.
    StackLimitCheck check;
    if (check.HasOverflowed()) return false;

    // Visit the unmarked objects.
    for (Object** p = start; p < end; p++) {
      if (!(*p)->IsHeapObject()) continue;
      HeapObject* obj = HeapObject::cast(*p);
      if (Marking::IsMarked(obj)) continue;
      VisitUnmarkedObject(obj);
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
    reinterpret_cast<Code*>(object)->CodeIterateBody<StaticMarkingVisitor>();
  }

  // Code flushing support.

  // How many collections newly compiled code object will survive before being
  // flushed.
  static const int kCodeAgeThreshold = 5;

  inline static bool HasSourceCode(SharedFunctionInfo* info) {
    Object* undefined = Heap::raw_unchecked_undefined_value();
    return (info->script() != undefined) &&
        (reinterpret_cast<Script*>(info->script())->source() != undefined);
  }


  inline static bool IsCompiled(JSFunction* function) {
    return
        function->unchecked_code() != Builtins::builtin(Builtins::LazyCompile);
  }


  inline static bool IsCompiled(SharedFunctionInfo* function) {
    return
        function->unchecked_code() != Builtins::builtin(Builtins::LazyCompile);
  }

  inline static bool IsFlushable(JSFunction* function) {
    SharedFunctionInfo* shared_info = function->unchecked_shared();

    // Code is either on stack, in compilation cache or referenced
    // by optimized version of function.
    if (Marking::IsMarked(function->unchecked_code())) {
      shared_info->set_code_age(0);
      return false;
    }

    // We do not flush code for optimized functions.
    if (function->code() != shared_info->unchecked_code()) {
      return false;
    }

    return IsFlushable(shared_info);
  }

  inline static bool IsFlushable(SharedFunctionInfo* shared_info) {
    // Code is either on stack, in compilation cache or referenced
    // by optimized version of function.
    if (Marking::IsMarked(shared_info->unchecked_code())) {
      shared_info->set_code_age(0);
      return false;
    }

    // The function must be compiled and have the source code available,
    // to be able to recompile it in case we need the function again.
    if (!(shared_info->is_compiled() && HasSourceCode(shared_info))) {
      return false;
    }

    // We never flush code for Api functions.
    Object* function_data = shared_info->function_data();
    if (function_data->IsHeapObject() &&
        (SafeMap(function_data)->instance_type() ==
         FUNCTION_TEMPLATE_INFO_TYPE)) {
      return false;
    }

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


  static bool FlushCodeForFunction(JSFunction* function) {
    if (!IsFlushable(function)) return false;

    // This function's code looks flushable. But we have to postpone the
    // decision until we see all functions that point to the same
    // SharedFunctionInfo because some of them might be optimized.
    // That would make the nonoptimized version of the code nonflushable,
    // because it is required for bailing out from optimized code.
    FlushCode::AddCandidate(function);
    return true;
  }


  static inline Map* SafeMap(Object* obj) {
    return HeapObject::cast(obj)->map();
  }


  static inline bool IsJSBuiltinsObject(Object* obj) {
    return obj->IsHeapObject() &&
        (SafeMap(obj)->instance_type() == JS_BUILTINS_OBJECT_TYPE);
  }


  static inline bool IsValidNotBuiltinContext(Object* ctx) {
    if (!ctx->IsHeapObject()) return false;

    Map* map = SafeMap(ctx);
    if (!(map == Heap::raw_unchecked_context_map() ||
          map == Heap::raw_unchecked_catch_context_map() ||
          map == Heap::raw_unchecked_global_context_map())) {
      return false;
    }

    Context* context = reinterpret_cast<Context*>(ctx);

    if (IsJSBuiltinsObject(context->global())) {
      return false;
    }

    return true;
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
    VisitSharedFunctionInfoAndFlushCodeGeneric(map, object, false);
  }


  static void VisitSharedFunctionInfoAndFlushCodeGeneric(
      Map* map, HeapObject* object, bool known_flush_code_candidate) {
    SharedFunctionInfo* shared = reinterpret_cast<SharedFunctionInfo*>(object);

    if (shared->IsInobjectSlackTrackingInProgress()) shared->DetachInitialMap();

    if (!known_flush_code_candidate) {
      known_flush_code_candidate = IsFlushable(shared);
      if (known_flush_code_candidate) FlushCode::AddCandidate(shared);
    }

    VisitSharedFunctionInfoFields(object, known_flush_code_candidate);
  }


  static void VisitCodeEntry(Address entry_address) {
    Object* code = Code::GetObjectFromEntryAddress(entry_address);
    Object* old_code = code;
    VisitPointer(&code);
    if (code != old_code) {
      Memory::Address_at(entry_address) =
          reinterpret_cast<Code*>(code)->entry();
    }
  }


  static void VisitJSFunctionAndFlushCode(Map* map, HeapObject* object) {
    JSFunction* jsfunction = reinterpret_cast<JSFunction*>(object);
    // The function must have a valid context and not be a builtin.
    bool flush_code_candidate = false;
    if (IsValidNotBuiltinContext(jsfunction->unchecked_context())) {
      flush_code_candidate = FlushCodeForFunction(jsfunction);
    }

    if (!flush_code_candidate) {
      MarkCompactCollector::MarkObject(
          jsfunction->unchecked_shared()->unchecked_code());

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
          MarkCompactCollector::MarkObject(
              inlined->unchecked_shared()->unchecked_code());
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
    VisitPointers(SLOT_ADDR(object, JSFunction::kPropertiesOffset),
                  SLOT_ADDR(object, JSFunction::kCodeEntryOffset));

    if (!flush_code_candidate) {
      VisitCodeEntry(object->address() + JSFunction::kCodeEntryOffset);
    } else {
      // Don't visit code object.

      // Visit shared function info to avoid double checking of it's
      // flushability.
      SharedFunctionInfo* shared_info = object->unchecked_shared();
      if (!Marking::IsMarked(shared_info)) {
        Map* shared_info_map = shared_info->map();
        MarkCompactCollector::SetMark(shared_info);
        MarkCompactCollector::MarkObject(shared_info_map);
        VisitSharedFunctionInfoAndFlushCodeGeneric(shared_info_map,
                                                   shared_info,
                                                   true);
      }
    }

    VisitPointers(SLOT_ADDR(object,
                            JSFunction::kCodeEntryOffset + kPointerSize),
                  SLOT_ADDR(object, JSFunction::kNonWeakFieldsEndOffset));

    // Don't visit the next function list field as it is a weak reference.
  }


  static void VisitSharedFunctionInfoFields(HeapObject* object,
                                            bool flush_code_candidate) {
    VisitPointer(SLOT_ADDR(object, SharedFunctionInfo::kNameOffset));

    if (!flush_code_candidate) {
      VisitPointer(SLOT_ADDR(object, SharedFunctionInfo::kCodeOffset));
    }

    VisitPointers(SLOT_ADDR(object, SharedFunctionInfo::kScopeInfoOffset),
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
  void VisitPointer(Object** p) {
    StaticMarkingVisitor::VisitPointer(p);
  }

  void VisitPointers(Object** start, Object** end) {
    StaticMarkingVisitor::VisitPointers(start, end);
  }

  void VisitCodeTarget(RelocInfo* rinfo) {
    StaticMarkingVisitor::VisitCodeTarget(rinfo);
  }

  void VisitGlobalPropertyCell(RelocInfo* rinfo) {
    StaticMarkingVisitor::VisitGlobalPropertyCell(rinfo);
  }

  void VisitDebugTarget(RelocInfo* rinfo) {
    StaticMarkingVisitor::VisitDebugTarget(rinfo);
  }
};


class CodeMarkingVisitor : public ThreadVisitor {
 public:
  void VisitThread(ThreadLocalTop* top) {
    for (StackFrameIterator it(top); !it.done(); it.Advance()) {
      MarkCompactCollector::MarkObject(it.frame()->unchecked_code());
    }
  }
};


class SharedFunctionInfoMarkingVisitor : public ObjectVisitor {
 public:
  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) VisitPointer(p);
  }

  void VisitPointer(Object** slot) {
    Object* obj = *slot;
    if (obj->IsSharedFunctionInfo()) {
      SharedFunctionInfo* shared = reinterpret_cast<SharedFunctionInfo*>(obj);
      MarkCompactCollector::MarkObject(shared->unchecked_code());
      MarkCompactCollector::MarkObject(shared);
    }
  }
};


void MarkCompactCollector::PrepareForCodeFlushing() {
  if (!FLAG_flush_code) {
    StaticMarkingVisitor::EnableCodeFlushing(false);
    return;
  }

#ifdef ENABLE_DEBUGGER_SUPPORT
  if (Debug::IsLoaded() || Debug::has_break_points()) {
    StaticMarkingVisitor::EnableCodeFlushing(false);
    return;
  }
#endif
  StaticMarkingVisitor::EnableCodeFlushing(true);

  // Ensure that empty descriptor array is marked. Method MarkDescriptorArray
  // relies on it being marked before any other descriptor array.
  MarkObject(Heap::raw_unchecked_empty_descriptor_array());

  // Make sure we are not referencing the code from the stack.
  for (StackFrameIterator it; !it.done(); it.Advance()) {
    MarkObject(it.frame()->unchecked_code());
  }

  // Iterate the archived stacks in all threads to check if
  // the code is referenced.
  CodeMarkingVisitor code_marking_visitor;
  ThreadManager::IterateArchivedThreads(&code_marking_visitor);

  SharedFunctionInfoMarkingVisitor visitor;
  CompilationCache::IterateFunctions(&visitor);
  HandleScopeImplementer::Iterate(&visitor);

  ProcessMarkingStack();
}


// Visitor class for marking heap roots.
class RootMarkingVisitor : public ObjectVisitor {
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

    // Replace flat cons strings in place.
    HeapObject* object = ShortCircuitConsString(p);
    if (Marking::IsMarked(object)) return;

    Map* map = object->map();
    // Mark the object.
    MarkCompactCollector::SetMark(object);

    // Mark the map pointer and body, and push them on the marking stack.
    MarkCompactCollector::MarkObject(map);
    StaticMarkingVisitor::IterateBody(map, object);

    // Mark all the objects reachable from the map and body.  May leave
    // overflowed objects in the heap.
    MarkCompactCollector::EmptyMarkingStack();
  }
};


// Helper class for pruning the symbol table.
class SymbolTableCleaner : public ObjectVisitor {
 public:
  SymbolTableCleaner() : pointers_removed_(0) { }

  virtual void VisitPointers(Object** start, Object** end) {
    // Visit all HeapObject pointers in [start, end).
    for (Object** p = start; p < end; p++) {
      if ((*p)->IsHeapObject() &&
          !Marking::IsMarked(HeapObject::cast(*p))) {
        // Check if the symbol being pruned is an external symbol. We need to
        // delete the associated external data as this symbol is going away.

        // Since no objects have yet been moved we can safely access the map of
        // the object.
        if ((*p)->IsExternalString()) {
          Heap::FinalizeExternalString(String::cast(*p));
        }
        // Set the entry to null_value (as deleted).
        *p = Heap::raw_unchecked_null_value();
        pointers_removed_++;
      }
    }
  }

  int PointersRemoved() {
    return pointers_removed_;
  }
 private:
  int pointers_removed_;
};


// Implementation of WeakObjectRetainer for mark compact GCs. All marked objects
// are retained.
class MarkCompactWeakObjectRetainer : public WeakObjectRetainer {
 public:
  virtual Object* RetainAs(Object* object) {
    if (Marking::IsMarked(HeapObject::cast(object))) {
      return object;
    } else {
      return NULL;
    }
  }
};


void MarkCompactCollector::ProcessNewlyMarkedObject(HeapObject* object) {
  ASSERT(Marking::IsMarked(object));
  ASSERT(Heap::Contains(object));
  if (object->IsMap()) {
    Map* map = Map::cast(object);
    if (FLAG_cleanup_caches_in_maps_at_gc) {
      map->ClearCodeCache();
    }
    if (FLAG_collect_maps &&
        map->instance_type() >= FIRST_JS_OBJECT_TYPE &&
        map->instance_type() <= JS_FUNCTION_TYPE) {
      MarkMapContents(map);
    } else {
      marking_stack.Push(map);
    }
  } else {
    marking_stack.Push(object);
  }
}


void MarkCompactCollector::MarkMapContents(Map* map) {
  MarkDescriptorArray(reinterpret_cast<DescriptorArray*>(
      *HeapObject::RawField(map, Map::kInstanceDescriptorsOffset)));

  // Mark the Object* fields of the Map.
  // Since the descriptor array has been marked already, it is fine
  // that one of these fields contains a pointer to it.
  Object** start_slot = HeapObject::RawField(map,
                                             Map::kPointerFieldsBeginOffset);

  Object** end_slot = HeapObject::RawField(map, Map::kPointerFieldsEndOffset);

  StaticMarkingVisitor::VisitPointers(start_slot, end_slot);
}


void MarkCompactCollector::MarkDescriptorArray(
    DescriptorArray* descriptors) {
  if (Marking::IsMarked(descriptors)) return;
  // Empty descriptor array is marked as a root before any maps are marked.
  ASSERT(descriptors != Heap::raw_unchecked_empty_descriptor_array());
  SetMark(descriptors);

  FixedArray* contents = reinterpret_cast<FixedArray*>(
      descriptors->get(DescriptorArray::kContentArrayIndex));
  ASSERT(contents->IsHeapObject());
  ASSERT(!Marking::IsMarked(contents));
  ASSERT(contents->IsFixedArray());
  ASSERT(contents->length() >= 2);
  SetMark(contents);
  // Contents contains (value, details) pairs.  If the details say that
  // the type of descriptor is MAP_TRANSITION, CONSTANT_TRANSITION, or
  // NULL_DESCRIPTOR, we don't mark the value as live.  Only for
  // MAP_TRANSITION and CONSTANT_TRANSITION is the value an Object* (a
  // Map*).
  for (int i = 0; i < contents->length(); i += 2) {
    // If the pair (value, details) at index i, i+1 is not
    // a transition or null descriptor, mark the value.
    PropertyDetails details(Smi::cast(contents->get(i + 1)));
    if (details.type() < FIRST_PHANTOM_PROPERTY_TYPE) {
      HeapObject* object = reinterpret_cast<HeapObject*>(contents->get(i));
      if (object->IsHeapObject() && !Marking::IsMarked(object)) {
        SetMark(object);
        marking_stack.Push(object);
      }
    }
  }
  // The DescriptorArray descriptors contains a pointer to its contents array,
  // but the contents array is already marked.
  marking_stack.Push(descriptors);
}


void MarkCompactCollector::CreateBackPointers() {
  HeapObjectIterator iterator(Heap::map_space());
  for (HeapObject* next_object = iterator.next();
       next_object != NULL; next_object = iterator.next()) {
    if (next_object->IsMap()) {  // Could also be ByteArray on free list.
      Map* map = Map::cast(next_object);
      if (map->instance_type() >= FIRST_JS_OBJECT_TYPE &&
          map->instance_type() <= JS_FUNCTION_TYPE) {
        map->CreateBackPointers();
      } else {
        ASSERT(map->instance_descriptors() == Heap::empty_descriptor_array());
      }
    }
  }
}


static int OverflowObjectSize(HeapObject* obj) {
  // Recover the normal map pointer, it might be marked as live and
  // overflowed.
  return obj->Size();
}


// Fill the marking stack with overflowed objects returned by the given
// iterator.  Stop when the marking stack is filled or the end of the space
// is reached, whichever comes first.
template<class T>
static void ScanOverflowedObjects(T* it) {
#if 0
  // The caller should ensure that the marking stack is initially not full,
  // so that we don't waste effort pointlessly scanning for objects.
  ASSERT(!marking_stack.is_full());

  for (HeapObject* object = it->next(); object != NULL; object = it->next()) {
    if (object->IsOverflowed()) {
      object->ClearOverflow();
      ASSERT(Marking::IsMarked(object));
      ASSERT(Heap::Contains(object));
      marking_stack.Push(object);
      if (marking_stack.is_full()) return;
    }
  }
#endif
  UNREACHABLE();
}


bool MarkCompactCollector::IsUnmarkedHeapObject(Object** p) {
  return (*p)->IsHeapObject() &&
      !Marking::IsMarked(HeapObject::cast(*p));
}


void MarkCompactCollector::MarkSymbolTable() {
  SymbolTable* symbol_table = Heap::raw_unchecked_symbol_table();
  // Mark the symbol table itself.
  SetMark(symbol_table);
  // Explicitly mark the prefix.
  MarkingVisitor marker;
  symbol_table->IteratePrefix(&marker);
  ProcessMarkingStack();
}


void MarkCompactCollector::MarkRoots(RootMarkingVisitor* visitor) {
  // Mark the heap roots including global variables, stack variables,
  // etc., and all objects reachable from them.
  Heap::IterateStrongRoots(visitor, VISIT_ONLY_STRONG);

  // Handle the symbol table specially.
  MarkSymbolTable();

  // There may be overflowed objects in the heap.  Visit them now.
  while (marking_stack.overflowed()) {
    RefillMarkingStack();
    EmptyMarkingStack();
  }
}


void MarkCompactCollector::MarkObjectGroups() {
  List<ObjectGroup*>* object_groups = GlobalHandles::ObjectGroups();

  for (int i = 0; i < object_groups->length(); i++) {
    ObjectGroup* entry = object_groups->at(i);
    if (entry == NULL) continue;

    List<Object**>& objects = entry->objects_;
    bool group_marked = false;
    for (int j = 0; j < objects.length(); j++) {
      Object* object = *objects[j];
      if (object->IsHeapObject()
          && Marking::IsMarked(HeapObject::cast(object))) {
        group_marked = true;
        break;
      }
    }

    if (!group_marked) continue;

    // An object in the group is marked, so mark as gray all white heap
    // objects in the group.
    for (int j = 0; j < objects.length(); ++j) {
      if ((*objects[j])->IsHeapObject()) {
        MarkObject(HeapObject::cast(*objects[j]));
      }
    }
    // Once the entire group has been colored gray, set the object group
    // to NULL so it won't be processed again.
    delete object_groups->at(i);
    object_groups->at(i) = NULL;
  }
}


// Mark all objects reachable from the objects on the marking stack.
// Before: the marking stack contains zero or more heap object pointers.
// After: the marking stack is empty, and all objects reachable from the
// marking stack have been marked, or are overflowed in the heap.
void MarkCompactCollector::EmptyMarkingStack() {
  while (!marking_stack.is_empty()) {
    HeapObject* object = marking_stack.Pop();
    ASSERT(object->IsHeapObject());
    ASSERT(Heap::Contains(object));
    ASSERT(Marking::IsMarked(object));
    ASSERT(!object->IsOverflowed());

    // Because the object is marked, we have to recover the original map
    // pointer and use it to mark the object's body.
    Map* map = object->map();
    MarkObject(map);

    StaticMarkingVisitor::IterateBody(map, object);
  }
}


// Sweep the heap for overflowed objects, clear their overflow bits, and
// push them on the marking stack.  Stop early if the marking stack fills
// before sweeping completes.  If sweeping completes, there are no remaining
// overflowed objects in the heap so the overflow flag on the markings stack
// is cleared.
void MarkCompactCollector::RefillMarkingStack() {
  ASSERT(marking_stack.overflowed());

  SemiSpaceIterator new_it(Heap::new_space(), &OverflowObjectSize);
  ScanOverflowedObjects(&new_it);
  if (marking_stack.is_full()) return;

  HeapObjectIterator old_pointer_it(Heap::old_pointer_space(),
                                    &OverflowObjectSize);
  ScanOverflowedObjects(&old_pointer_it);
  if (marking_stack.is_full()) return;

  HeapObjectIterator old_data_it(Heap::old_data_space(), &OverflowObjectSize);
  ScanOverflowedObjects(&old_data_it);
  if (marking_stack.is_full()) return;

  HeapObjectIterator code_it(Heap::code_space(), &OverflowObjectSize);
  ScanOverflowedObjects(&code_it);
  if (marking_stack.is_full()) return;

  HeapObjectIterator map_it(Heap::map_space(), &OverflowObjectSize);
  ScanOverflowedObjects(&map_it);
  if (marking_stack.is_full()) return;

  HeapObjectIterator cell_it(Heap::cell_space(), &OverflowObjectSize);
  ScanOverflowedObjects(&cell_it);
  if (marking_stack.is_full()) return;

  LargeObjectIterator lo_it(Heap::lo_space(), &OverflowObjectSize);
  ScanOverflowedObjects(&lo_it);
  if (marking_stack.is_full()) return;

  marking_stack.clear_overflowed();
}


// Mark all objects reachable (transitively) from objects on the marking
// stack.  Before: the marking stack contains zero or more heap object
// pointers.  After: the marking stack is empty and there are no overflowed
// objects in the heap.
void MarkCompactCollector::ProcessMarkingStack() {
  EmptyMarkingStack();
  while (marking_stack.overflowed()) {
    RefillMarkingStack();
    EmptyMarkingStack();
  }
}


void MarkCompactCollector::ProcessObjectGroups() {
  bool work_to_do = true;
  ASSERT(marking_stack.is_empty());
  while (work_to_do) {
    MarkObjectGroups();
    work_to_do = !marking_stack.is_empty();
    ProcessMarkingStack();
  }
}


void MarkCompactCollector::MarkLiveObjects() {
  GCTracer::Scope gc_scope(tracer_, GCTracer::Scope::MC_MARK);
  // The recursive GC marker detects when it is nearing stack overflow,
  // and switches to a different marking system.  JS interrupts interfere
  // with the C stack limit check.
  PostponeInterruptsScope postpone;

#ifdef DEBUG
  ASSERT(state_ == PREPARE_GC);
  state_ = MARK_LIVE_OBJECTS;
#endif
  // The to space contains live objects, the from space is used as a marking
  // stack.
  marking_stack.Initialize(Heap::new_space()->FromSpaceLow(),
                           Heap::new_space()->FromSpaceHigh());

  ASSERT(!marking_stack.overflowed());

  PrepareForCodeFlushing();

  RootMarkingVisitor root_visitor;
  MarkRoots(&root_visitor);

  // The objects reachable from the roots are marked, yet unreachable
  // objects are unmarked.  Mark objects reachable from object groups
  // containing at least one marked object, and continue until no new
  // objects are reachable from the object groups.
  ProcessObjectGroups();

  // The objects reachable from the roots or object groups are marked,
  // yet unreachable objects are unmarked.  Mark objects reachable
  // only from weak global handles.
  //
  // First we identify nonlive weak handles and mark them as pending
  // destruction.
  GlobalHandles::IdentifyWeakHandles(&IsUnmarkedHeapObject);
  // Then we mark the objects and process the transitive closure.
  GlobalHandles::IterateWeakRoots(&root_visitor);
  while (marking_stack.overflowed()) {
    RefillMarkingStack();
    EmptyMarkingStack();
  }

  // Repeat the object groups to mark unmarked groups reachable from the
  // weak roots.
  ProcessObjectGroups();

  AfterMarking();
}


void MarkCompactCollector::AfterMarking() {
  // Prune the symbol table removing all symbols only pointed to by the
  // symbol table.  Cannot use symbol_table() here because the symbol
  // table is marked.
  SymbolTable* symbol_table = Heap::raw_unchecked_symbol_table();
  SymbolTableCleaner v;
  symbol_table->IterateElements(&v);
  symbol_table->ElementsRemoved(v.PointersRemoved());
  ExternalStringTable::Iterate(&v);
  ExternalStringTable::CleanUp();

  // Process the weak references.
  MarkCompactWeakObjectRetainer mark_compact_object_retainer;
  Heap::ProcessWeakReferences(&mark_compact_object_retainer);

  // Remove object groups after marking phase.
  GlobalHandles::RemoveObjectGroups();

  // Flush code from collected candidates.
  if (FLAG_flush_code) {
    FlushCode::ProcessCandidates();
  }

  // Clean up dead objects from the runtime profiler.
  RuntimeProfiler::RemoveDeadSamples();
}


#ifdef DEBUG
void MarkCompactCollector::UpdateLiveObjectCount(HeapObject* obj) {
  live_bytes_ += obj->Size();
  if (Heap::new_space()->Contains(obj)) {
    live_young_objects_size_ += obj->Size();
  } else if (Heap::map_space()->Contains(obj)) {
    ASSERT(obj->IsMap());
    live_map_objects_size_ += obj->Size();
  } else if (Heap::cell_space()->Contains(obj)) {
    ASSERT(obj->IsJSGlobalPropertyCell());
    live_cell_objects_size_ += obj->Size();
  } else if (Heap::old_pointer_space()->Contains(obj)) {
    live_old_pointer_objects_size_ += obj->Size();
  } else if (Heap::old_data_space()->Contains(obj)) {
    live_old_data_objects_size_ += obj->Size();
  } else if (Heap::code_space()->Contains(obj)) {
    live_code_objects_size_ += obj->Size();
  } else if (Heap::lo_space()->Contains(obj)) {
    live_lo_objects_size_ += obj->Size();
  } else {
    UNREACHABLE();
  }
}
#endif  // DEBUG


// Safe to use during marking phase only.
bool MarkCompactCollector::SafeIsMap(HeapObject* object) {
  return object->map()->instance_type() == MAP_TYPE;
}


void MarkCompactCollector::ClearNonLiveTransitions() {
  HeapObjectIterator map_iterator(Heap::map_space());
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
  for (HeapObject* obj = map_iterator.next();
       obj != NULL; obj = map_iterator.next()) {
    Map* map = reinterpret_cast<Map*>(obj);
    if (!Marking::IsMarked(map) && map->IsByteArray()) continue;

    ASSERT(SafeIsMap(map));
    // Only JSObject and subtypes have map transitions and back pointers.
    if (map->instance_type() < FIRST_JS_OBJECT_TYPE) continue;
    if (map->instance_type() > JS_FUNCTION_TYPE) continue;

    if (Marking::IsMarked(map) &&
        map->attached_to_shared_function_info()) {
      // This map is used for inobject slack tracking and has been detached
      // from SharedFunctionInfo during the mark phase.
      // Since it survived the GC, reattach it now.
      map->unchecked_constructor()->unchecked_shared()->AttachInitialMap(map);
    }

    // Follow the chain of back pointers to find the prototype.
    Map* current = map;
    while (SafeIsMap(current)) {
      current = reinterpret_cast<Map*>(current->prototype());
      ASSERT(current->IsHeapObject());
    }
    Object* real_prototype = current;

    // Follow back pointers, setting them to prototype,
    // clearing map transitions when necessary.
    current = map;
    bool on_dead_path = !Marking::IsMarked(current);
    Object* next;
    while (SafeIsMap(current)) {
      next = current->prototype();
      // There should never be a dead map above a live map.
      ASSERT(on_dead_path || Marking::IsMarked(current));

      // A live map above a dead map indicates a dead transition.
      // This test will always be false on the first iteration.
      if (on_dead_path && Marking::IsMarked(current)) {
        on_dead_path = false;
        current->ClearNonLiveTransitions(real_prototype);
      }
      *HeapObject::RawField(current, Map::kPrototypeOffset) =
          real_prototype;
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
static void MigrateObject(Address dst,
                          Address src,
                          int size,
                          bool to_old_space) {
  if (to_old_space) {
    Heap::CopyBlockToOldSpaceAndUpdateWriteBarrier(dst, src, size);
  } else {
    Heap::CopyBlock(dst, src, size);
  }
  Memory::Address_at(src) = dst;
}


class StaticPointersToNewGenUpdatingVisitor : public
  StaticNewSpaceVisitor<StaticPointersToNewGenUpdatingVisitor> {
 public:
  static inline void VisitPointer(Object** p) {
    if (!(*p)->IsHeapObject()) return;

    HeapObject* obj = HeapObject::cast(*p);
    Address old_addr = obj->address();

    if (Heap::new_space()->Contains(obj)) {
      ASSERT(Heap::InFromSpace(*p));
      *p = HeapObject::FromAddress(Memory::Address_at(old_addr));
    }
  }
};


// Visitor for updating pointers from live objects in old spaces to new space.
// It does not expect to encounter pointers to dead objects.
class PointersToNewGenUpdatingVisitor: public ObjectVisitor {
 public:
  void VisitPointer(Object** p) {
    StaticPointersToNewGenUpdatingVisitor::VisitPointer(p);
  }

  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) {
      StaticPointersToNewGenUpdatingVisitor::VisitPointer(p);
    }
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
};


static void UpdatePointerToNewGen(HeapObject** p, HeapObject* object) {
  ASSERT(Heap::InFromSpace(object));
  ASSERT(*p == object);

  Address old_addr = object->address();

  Address new_addr = Memory::Address_at(old_addr);

  // The new space sweep will overwrite the map word of dead objects
  // with NULL. In this case we do not need to transfer this entry to
  // the store buffer which we are rebuilding.
  if (new_addr != NULL) {
    *p = HeapObject::FromAddress(new_addr);
    if (Heap::InNewSpace(new_addr)) {
      StoreBuffer::EnterDirectlyIntoStoreBuffer(reinterpret_cast<Address>(p));
    }
  } else {
    // We have to zap this pointer, because the store buffer may overflow later,
    // and then we have to scan the entire heap and we don't want to find
    // spurious newspace pointers in the old space.
    *p = HeapObject::FromAddress(NULL);  // Fake heap object not in new space.
  }
}


static String* UpdateNewSpaceReferenceInExternalStringTableEntry(Object **p) {
  Address old_addr = HeapObject::cast(*p)->address();
  Address new_addr = Memory::Address_at(old_addr);
  return String::cast(HeapObject::FromAddress(new_addr));
}


static bool TryPromoteObject(HeapObject* object, int object_size) {
  Object* result;

  if (object_size > Heap::MaxObjectSizeInPagedSpace()) {
    MaybeObject* maybe_result =
        Heap::lo_space()->AllocateRawFixedArray(object_size);
    if (maybe_result->ToObject(&result)) {
      HeapObject* target = HeapObject::cast(result);
      MigrateObject(target->address(), object->address(), object_size, true);
      MarkCompactCollector::tracer()->
          increment_promoted_objects_size(object_size);
      return true;
    }
  } else {
    OldSpace* target_space = Heap::TargetSpace(object);

    ASSERT(target_space == Heap::old_pointer_space() ||
           target_space == Heap::old_data_space());
    MaybeObject* maybe_result = target_space->AllocateRaw(object_size);
    if (maybe_result->ToObject(&result)) {
      HeapObject* target = HeapObject::cast(result);
      MigrateObject(target->address(),
                    object->address(),
                    object_size,
                    target_space == Heap::old_pointer_space());
      MarkCompactCollector::tracer()->
          increment_promoted_objects_size(object_size);
      return true;
    }
  }

  return false;
}


void MarkCompactCollector::SweepNewSpace(NewSpace* space) {
  Heap::CheckNewSpaceExpansionCriteria();

  Address from_bottom = space->bottom();
  Address from_top = space->top();

  // Flip the semispaces.  After flipping, to space is empty, from space has
  // live objects.
  space->Flip();
  space->ResetAllocationInfo();

  int size = 0;
  int survivors_size = 0;

  // First pass: traverse all objects in inactive semispace, remove marks,
  // migrate live objects and write forwarding addresses.
  for (Address current = from_bottom; current < from_top; current += size) {
    HeapObject* object = HeapObject::FromAddress(current);

    if (Marking::IsMarked(object)) {
      Marking::ClearMark(object);
      MarkCompactCollector::tracer()->decrement_marked_count();

      size = object->Size();
      survivors_size += size;

      // Aggressively promote young survivors to the old space.
      if (TryPromoteObject(object, size)) {
        continue;
      }

      // Promotion failed. Just migrate object to another semispace.
      // Allocation cannot fail at this point: semispaces are of equal size.
      Object* target = space->AllocateRaw(size)->ToObjectUnchecked();

      MigrateObject(HeapObject::cast(target)->address(),
                    current,
                    size,
                    false);
    } else {
      // Process the dead object before we write a NULL into its header.
      LiveObjectList::ProcessNonLive(object);

      size = object->Size();
      // Mark dead objects in the new space with null in their map field.
      Memory::Address_at(current) = NULL;
    }
  }

  // Second pass: find pointers to new space and update them.
  PointersToNewGenUpdatingVisitor updating_visitor;

  // Update pointers in to space.
  Address current = space->bottom();
  while (current < space->top()) {
    HeapObject* object = HeapObject::FromAddress(current);
    current +=
        StaticPointersToNewGenUpdatingVisitor::IterateBody(object->map(),
                                                           object);
  }

  // Update roots.
  Heap::IterateRoots(&updating_visitor, VISIT_ALL_IN_SCAVENGE);
  LiveObjectList::IterateElements(&updating_visitor);

  {
    StoreBufferRebuildScope scope;
    StoreBuffer::IteratePointersToNewSpace(&UpdatePointerToNewGen);
  }

  // Update pointers from cells.
  HeapObjectIterator cell_iterator(Heap::cell_space());
  for (HeapObject* cell = cell_iterator.next();
       cell != NULL;
       cell = cell_iterator.next()) {
    if (cell->IsJSGlobalPropertyCell()) {
      Address value_address =
          reinterpret_cast<Address>(cell) +
          (JSGlobalPropertyCell::kValueOffset - kHeapObjectTag);
      updating_visitor.VisitPointer(reinterpret_cast<Object**>(value_address));
    }
  }

  // Update pointer from the global contexts list.
  updating_visitor.VisitPointer(Heap::global_contexts_list_address());

  // Update pointers from external string table.
  Heap::UpdateNewSpaceReferencesInExternalStringTable(
      &UpdateNewSpaceReferenceInExternalStringTableEntry);

  // All pointers were updated. Update auxiliary allocation info.
  Heap::IncrementYoungSurvivorsCounter(survivors_size);
  space->set_age_mark(space->top());

  // Update JSFunction pointers from the runtime profiler.
  RuntimeProfiler::UpdateSamplesAfterScavenge();
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
  uint32_t free_cell_index = Page::MarkbitsBitmap::IndexToCell(free_start);
  ASSERT(cells[free_cell_index] == 0);
  while (free_cell_index < region_end && cells[free_cell_index] == 0) {
    free_cell_index++;
  }

  if (free_cell_index >= region_end) {
    return free_cell_index;
  }

  uint32_t free_end = Page::MarkbitsBitmap::CellToIndex(free_cell_index);
  space->DeallocateBlock(p->MarkbitIndexToAddress(free_start),
                         (free_end - free_start) << kPointerSizeLog2,
                         true);

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


INLINE(static int SizeOfPreviousObject(Page* p,
                                       uint32_t cell_index,
                                       uint32_t* cells));


static int SizeOfPreviousObject(Page* p,
                                uint32_t cell_index,
                                uint32_t* cells) {
  ASSERT(cells[cell_index] == 0);
  if (cells[cell_index - 1] == 0) return 0;

  int leading_zeroes =
      CompilerIntrinsics::CountLeadingZeros(cells[cell_index - 1]) + 1;
  Address addr =
      p->MarkbitIndexToAddress(
          Page::MarkbitsBitmap::CellToIndex(cell_index) - leading_zeroes);
  HeapObject* obj = HeapObject::FromAddress(addr);
  ASSERT(obj->map()->IsMap());
  return (obj->Size() >> kPointerSizeLog2) - leading_zeroes;
}


static void SweepConservatively(PagedSpace* space,
                                Page* p,
                                Address* last_free_start) {
  typedef Page::MarkbitsBitmap::CellType CellType;
  Page::MarkbitsBitmap* markbits = p->markbits();
  CellType* cells = markbits->cells();

  uint32_t last_cell_index =
      Page::MarkbitsBitmap::IndexToCell(
          Page::MarkbitsBitmap::CellAlignIndex(
              p->AddressToMarkbitIndex(p->AllocationTop())));

  uint32_t cell_index = Page::kFirstUsedCell;
  uint32_t polluted_cell_index = Page::kFirstUsedCell;
  if (cells[cell_index] == 0) {
    polluted_cell_index =
        SweepFree(space,
                  p,
                  p->AddressToMarkbitIndex(p->ObjectAreaStart()),
                  last_cell_index,
                  cells);

    if (polluted_cell_index >= last_cell_index) {
      // All cells are free.
      *last_free_start = p->ObjectAreaStart();
      return;
    }
  }

  p->ClearFlag(Page::IS_CONTINUOUS);

  ASSERT(cells[polluted_cell_index] != 0);
  for (cell_index = NextCandidate(polluted_cell_index, last_cell_index, cells);
       cell_index < last_cell_index;
       cell_index = NextCandidate(polluted_cell_index,
                                  last_cell_index,
                                  cells)) {
    ASSERT(cells[cell_index] == 0);

    int size = SizeOfPreviousObject(p, cell_index, cells);
    if (size <= 0) {
      polluted_cell_index =
          SweepFree(space,
                    p,
                    Page::MarkbitsBitmap::CellToIndex(cell_index),
                    last_cell_index,
                    cells);
      if (polluted_cell_index >= last_cell_index) {
        // This free region is the last on the page.
        *last_free_start = p->MarkbitIndexToAddress(
            Page::MarkbitsBitmap::CellToIndex(cell_index));
        return;
      }
    } else {
      // Skip cells covered by this object.
      polluted_cell_index = cell_index +
          Page::MarkbitsBitmap::IndexToCell(size - 1);
    }
  }
}


static void SweepPrecisely(PagedSpace* space,
                           Page* p,
                           Address* last_free_start) {
  bool is_previous_alive = true;
  Address free_start = NULL;
  HeapObject* object;

  for (Address current = p->ObjectAreaStart();
       current < p->AllocationTop();
       current += object->Size()) {
    object = HeapObject::FromAddress(current);
    if (Marking::IsMarked(object)) {
      Marking::ClearMark(object);
      MarkCompactCollector::tracer()->decrement_marked_count();

      if (!is_previous_alive) {  // Transition from free to live.
        space->DeallocateBlock(free_start,
                               static_cast<int>(current - free_start),
                               true);
        is_previous_alive = true;
      }
    } else {
      ASSERT((current + kPointerSize) >= p->AllocationTop() ||
             object->Size() == kPointerSize ||
             IncrementalMarking::IsWhite(object));
      MarkCompactCollector::ReportDeleteIfNeeded(object);
      if (is_previous_alive) {  // Transition from live to free.
        free_start = current;
        is_previous_alive = false;
      }
    }
  }

  if (!is_previous_alive) *last_free_start = free_start;
}


void MarkCompactCollector::SweepSpace(PagedSpace* space,
                                      SweeperType sweeper) {
  PageIterator it(space, PageIterator::PAGES_IN_USE);

  // During sweeping of paged space we are trying to find longest sequences
  // of pages without live objects and free them (instead of putting them on
  // the free list).

  // Page preceding current.
  Page* prev = Page::FromAddress(NULL);

  // First empty page in a sequence.
  Page* first_empty_page = Page::FromAddress(NULL);

  // Page preceding first empty page.
  Page* prec_first_empty_page = Page::FromAddress(NULL);

  // If last used page of space ends with a sequence of dead objects
  // we can adjust allocation top instead of puting this free area into
  // the free list. Thus during sweeping we keep track of such areas
  // and defer their deallocation until the sweeping of the next page
  // is done: if one of the next pages contains live objects we have
  // to put such area into the free list.
  Address last_free_start = NULL;
  int last_free_size = 0;

  while (it.has_next()) {
    Page* p = it.next();

    Address free_start = p->AllocationTop();

    if (sweeper == CONSERVATIVE) {
      SweepConservatively(space, p, &free_start);
      p->set_linearity_boundary(free_start);
    } else {
      ASSERT(sweeper == PRECISE);
      SweepPrecisely(space, p, &free_start);
    }

    bool page_is_empty = (p->ObjectAreaStart() == free_start);
    bool is_previous_alive = (free_start == p->AllocationTop());

    ASSERT(free_start <= p->AllocationTop());

    if (page_is_empty) {
      // This page is empty. Check whether we are in the middle of
      // sequence of empty pages and start one if not.
      if (!first_empty_page->is_valid()) {
        first_empty_page = p;
        prec_first_empty_page = prev;
      }

      if (!is_previous_alive) {
        // There are dead objects on this page. Update space accounting stats
        // without putting anything into free list.
        int size_in_bytes = static_cast<int>(p->AllocationTop() - free_start);
        if (size_in_bytes > 0) {
          space->DeallocateBlock(free_start, size_in_bytes, false);
        }
      }
    } else {
      // This page is not empty. Sequence of empty pages ended on the previous
      // one.
      if (first_empty_page->is_valid()) {
        space->FreePages(prec_first_empty_page, prev);
        prec_first_empty_page = first_empty_page = Page::FromAddress(NULL);
      }

      // If there is a free ending area on one of the previous pages we have
      // deallocate that area and put it on the free list.
      if (last_free_size > 0) {
        Page::FromAddress(last_free_start)->
            SetAllocationWatermark(last_free_start);
        space->DeallocateBlock(last_free_start, last_free_size, true);
        last_free_start = NULL;
        last_free_size  = 0;
      }

      // If the last region of this page was not live we remember it.
      if (!is_previous_alive) {
        ASSERT(last_free_size == 0);
        last_free_size = static_cast<int>(p->AllocationTop() - free_start);
        last_free_start = free_start;
      }
    }

    prev = p;
  }

  // We reached end of space. See if we need to adjust allocation top.
  Address new_allocation_top = NULL;

  if (first_empty_page->is_valid()) {
    // Last used pages in space are empty. We can move allocation top backwards
    // to the beginning of first empty page.
    ASSERT(prev == space->AllocationTopPage());
    space->FreePages(prec_first_empty_page, prev);
    new_allocation_top = first_empty_page->ObjectAreaStart();
  }

  if (last_free_size > 0) {
    // There was a free ending area on the previous page.
    // Deallocate it without putting it into freelist and move allocation
    // top to the beginning of this free area.
    space->DeallocateBlock(last_free_start, last_free_size, false);
    new_allocation_top = last_free_start;
  }

  if (new_allocation_top != NULL) {
#ifdef DEBUG
    Page* new_allocation_top_page = Page::FromAllocationTop(new_allocation_top);
    if (!first_empty_page->is_valid()) {
      ASSERT(new_allocation_top_page == space->AllocationTopPage());
    } else if (last_free_size > 0) {
      ASSERT(new_allocation_top_page == prec_first_empty_page);
    } else {
      ASSERT(new_allocation_top_page == first_empty_page);
    }
#endif

    space->SetTop(new_allocation_top);
  }
}


void MarkCompactCollector::SweepSpaces() {
  GCTracer::Scope gc_scope(tracer_, GCTracer::Scope::MC_SWEEP);
#ifdef DEBUG
  state_ = SWEEP_SPACES;
#endif

#ifndef DEBUG
  SweeperType fast_sweeper = CONSERVATIVE;
#else
  SweeperType fast_sweeper = PRECISE;
#endif

  ASSERT(!IsCompacting());
  // Noncompacting collections simply sweep the spaces to clear the mark
  // bits and free the nonlive blocks (for old and map spaces).  We sweep
  // the map space last because freeing non-live maps overwrites them and
  // the other spaces rely on possibly non-live maps to get the sizes for
  // non-live objects.
  SweepSpace(Heap::old_pointer_space(), fast_sweeper);
  SweepSpace(Heap::old_data_space(), fast_sweeper);
  SweepSpace(Heap::code_space(), PRECISE);
  // TODO(gc): implement specialized sweeper for cell space.
  SweepSpace(Heap::cell_space(), fast_sweeper);
  { GCTracer::Scope gc_scope(tracer_, GCTracer::Scope::MC_SWEEP_NEWSPACE);
    SweepNewSpace(Heap::new_space());
  }
  // TODO(gc): ClearNonLiveTransitions depends on precise sweeping of
  // map space to detect whether unmarked map became dead in this
  // collection or in one of the previous ones.
  // TODO(gc): Implement specialized sweeper for map space.
  SweepSpace(Heap::map_space(), PRECISE);

  ASSERT(live_map_objects_size_ <= Heap::map_space()->Size());

  // Deallocate unmarked objects and clear marked bits for marked objects.
  Heap::lo_space()->FreeUnmarkedObjects();
}


// Iterate the live objects in a range of addresses (eg, a page or a
// semispace).  The live regions of the range have been linked into a list.
// The first live region is [first_live_start, first_live_end), and the last
// address in the range is top.  The callback function is used to get the
// size of each live object.
int MarkCompactCollector::IterateLiveObjectsInRange(
    Address start,
    Address end,
    HeapObjectCallback size_func) {
  int live_objects_size = 0;
  Address current = start;
  while (current < end) {
    uint32_t encoded_map = Memory::uint32_at(current);
    if (encoded_map == kSingleFreeEncoding) {
      current += kPointerSize;
    } else if (encoded_map == kMultiFreeEncoding) {
      current += Memory::int_at(current + kIntSize);
    } else {
      int size = size_func(HeapObject::FromAddress(current));
      current += size;
      live_objects_size += size;
    }
  }
  return live_objects_size;
}


int MarkCompactCollector::IterateLiveObjects(NewSpace* space,
                                             HeapObjectCallback size_f) {
  ASSERT(MARK_LIVE_OBJECTS < state_ && state_ <= RELOCATE_OBJECTS);
  return IterateLiveObjectsInRange(space->bottom(), space->top(), size_f);
}


int MarkCompactCollector::IterateLiveObjects(PagedSpace* space,
                                             HeapObjectCallback size_f) {
  ASSERT(MARK_LIVE_OBJECTS < state_ && state_ <= RELOCATE_OBJECTS);
  int total = 0;
  PageIterator it(space, PageIterator::PAGES_IN_USE);
  while (it.has_next()) {
    Page* p = it.next();
    total += IterateLiveObjectsInRange(p->ObjectAreaStart(),
                                       p->AllocationTop(),
                                       size_f);
  }
  return total;
}


void MarkCompactCollector::ReportDeleteIfNeeded(HeapObject* obj) {
#ifdef ENABLE_GDB_JIT_INTERFACE
  if (obj->IsCode()) {
    GDBJITInterface::RemoveCode(reinterpret_cast<Code*>(obj));
  }
#endif
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (obj->IsCode()) {
    PROFILE(CodeDeleteEvent(obj->address()));
  }
#endif
}


void MarkCompactCollector::Initialize() {
  StaticPointersToNewGenUpdatingVisitor::Initialize();
  StaticMarkingVisitor::Initialize();
}


} }  // namespace v8::internal
