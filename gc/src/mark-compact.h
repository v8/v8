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

#ifndef V8_MARK_COMPACT_H_
#define V8_MARK_COMPACT_H_

#include "compiler-intrinsics.h"
#include "spaces.h"

namespace v8 {
namespace internal {

// Callback function, returns whether an object is alive. The heap size
// of the object is returned in size. It optionally updates the offset
// to the first live object in the page (only used for old and map objects).
typedef bool (*IsAliveFunction)(HeapObject* obj, int* size, int* offset);

// Forward declarations.
class CodeFlusher;
class GCTracer;
class MarkingVisitor;
class RootMarkingVisitor;


class Marking {
 public:
  explicit Marking(Heap* heap)
      : heap_(heap) {
  }

  static inline MarkBit MarkBitFrom(Address addr);

  static inline MarkBit MarkBitFrom(HeapObject* obj) {
    return MarkBitFrom(reinterpret_cast<Address>(obj));
  }

  // Impossible markbits: 01
  static const char* kImpossibleBitPattern;
  static inline bool IsImpossible(MarkBit mark_bit) {
    ASSERT(strcmp(kImpossibleBitPattern, "01") == 0);
    return !mark_bit.Get() && mark_bit.Next().Get();
  }

  // Black markbits: 10 - this is required by the sweeper.
  static const char* kBlackBitPattern;
  static inline bool IsBlack(MarkBit mark_bit) {
    ASSERT(strcmp(kBlackBitPattern, "10") == 0);
    ASSERT(!IsImpossible(mark_bit));
    return mark_bit.Get() && !mark_bit.Next().Get();
  }

  // White markbits: 00 - this is required by the mark bit clearer.
  static const char* kWhiteBitPattern;
  static inline bool IsWhite(MarkBit mark_bit) {
    ASSERT(strcmp(kWhiteBitPattern, "00") == 0);
    ASSERT(!IsImpossible(mark_bit));
    return !mark_bit.Get();
  }

  // Grey markbits: 11
  static const char* kGreyBitPattern;
  static inline bool IsGrey(MarkBit mark_bit) {
    ASSERT(strcmp(kGreyBitPattern, "11") == 0);
    ASSERT(!IsImpossible(mark_bit));
    return mark_bit.Get() && mark_bit.Next().Get();
  }

  static inline void MarkBlack(MarkBit mark_bit) {
    mark_bit.Set();
    mark_bit.Next().Clear();
    ASSERT(Marking::IsBlack(mark_bit));
  }

  static inline void BlackToGrey(MarkBit markbit) {
    ASSERT(IsBlack(markbit));
    markbit.Next().Set();
    ASSERT(IsGrey(markbit));
  }

  static inline void WhiteToGrey(MarkBit markbit) {
    ASSERT(IsWhite(markbit));
    markbit.Set();
    markbit.Next().Set();
    ASSERT(IsGrey(markbit));
  }

  static inline void GreyToBlack(MarkBit markbit) {
    ASSERT(IsGrey(markbit));
    markbit.Next().Clear();
    ASSERT(IsBlack(markbit));
  }

  static inline void BlackToGrey(HeapObject* obj) {
    ASSERT(obj->Size() >= 2 * kPointerSize);
    BlackToGrey(MarkBitFrom(obj));
  }

  void TransferMark(Address old_start, Address new_start);

#ifdef DEBUG
  enum ObjectColor {
    BLACK_OBJECT,
    WHITE_OBJECT,
    GREY_OBJECT,
    IMPOSSIBLE_COLOR
  };

  static const char* ColorName(ObjectColor color) {
    switch (color) {
      case BLACK_OBJECT: return "black";
      case WHITE_OBJECT: return "white";
      case GREY_OBJECT: return "grey";
      case IMPOSSIBLE_COLOR: return "impossible";
    }
    return "error";
  }

  static ObjectColor Color(HeapObject* obj) {
    return Color(Marking::MarkBitFrom(obj));
  }

  static ObjectColor Color(MarkBit mark_bit) {
    if (IsBlack(mark_bit)) return BLACK_OBJECT;
    if (IsWhite(mark_bit)) return WHITE_OBJECT;
    if (IsGrey(mark_bit)) return GREY_OBJECT;
    UNREACHABLE();
    return IMPOSSIBLE_COLOR;
  }
#endif

  INLINE(static void TransferColor(HeapObject* from,
                                   HeapObject* to)) {
    MarkBit from_mark_bit = MarkBitFrom(from);
    MarkBit to_mark_bit = MarkBitFrom(to);
    if (from_mark_bit.Get()) to_mark_bit.Set();
    if (from_mark_bit.Next().Get()) to_mark_bit.Next().Set();
    ASSERT(Color(from) == Color(to));
  }

 private:
  Heap* heap_;
};

// ----------------------------------------------------------------------------
// Marking deque for tracing live objects.

class MarkingDeque {
 public:
  MarkingDeque()
      : array_(NULL), top_(0), bottom_(0), mask_(0), overflowed_(false) { }

  void Initialize(Address low, Address high) {
    HeapObject** obj_low = reinterpret_cast<HeapObject**>(low);
    HeapObject** obj_high = reinterpret_cast<HeapObject**>(high);
    array_ = obj_low;
    mask_ = RoundDownToPowerOf2(obj_high - obj_low) - 1;
    top_ = bottom_ = 0;
    overflowed_ = false;
  }

  inline bool IsFull() { return ((top_ + 1) & mask_) == bottom_; }

  inline bool IsEmpty() { return top_ == bottom_; }

  bool overflowed() const { return overflowed_; }

  void ClearOverflowed() { overflowed_ = false; }

  void SetOverflowed() { overflowed_ = true; }

  // Push the (marked) object on the marking stack if there is room,
  // otherwise mark the object as overflowed and wait for a rescan of the
  // heap.
  inline void PushBlack(HeapObject* object) {
    ASSERT(object->IsHeapObject());
    if (IsFull()) {
      Marking::BlackToGrey(object);
      SetOverflowed();
    } else {
      array_[top_] = object;
      top_ = ((top_ + 1) & mask_);
    }
  }

  inline void PushGrey(HeapObject* object) {
    ASSERT(object->IsHeapObject());
    if (IsFull()) {
      ASSERT(Marking::IsGrey(Marking::MarkBitFrom(object)));
      SetOverflowed();
    } else {
      array_[top_] = object;
      top_ = ((top_ + 1) & mask_);
    }
  }

  inline HeapObject* Pop() {
    ASSERT(!IsEmpty());
    top_ = ((top_ - 1) & mask_);
    HeapObject* object = array_[top_];
    ASSERT(object->IsHeapObject());
    return object;
  }

  inline void UnshiftGrey(HeapObject* object) {
    ASSERT(object->IsHeapObject());
    if (IsFull()) {
      ASSERT(Marking::IsGrey(Marking::MarkBitFrom(object)));
      SetOverflowed();
    } else {
      bottom_ = ((bottom_ - 1) & mask_);
      array_[bottom_] = object;
    }
  }

  HeapObject** array() { return array_; }
  intptr_t bottom() { return bottom_; }
  intptr_t top() { return top_; }
  intptr_t mask() { return mask_; }
  void set_top(intptr_t top) { top_ = top; }

 private:
  HeapObject** array_;
  // array_[(top - 1) & mask_] is the top element in the deque.  The Deque is
  // empty when top_ == bottom_.  It is full when top_ + 1 == bottom
  // (mod mask + 1).
  int top_;
  int bottom_;
  int mask_;
  bool overflowed_;

  DISALLOW_COPY_AND_ASSIGN(MarkingDeque);
};


// -------------------------------------------------------------------------
// Mark-Compact collector
class MarkCompactCollector {
 public:
  // Type of functions to compute forwarding addresses of objects in
  // compacted spaces.  Given an object and its size, return a (non-failure)
  // Object* that will be the object after forwarding.  There is a separate
  // allocation function for each (compactable) space based on the location
  // of the object before compaction.
  typedef MaybeObject* (*AllocationFunction)(Heap* heap,
                                             HeapObject* object,
                                             int object_size);

  // Type of functions to encode the forwarding address for an object.
  // Given the object, its size, and the new (non-failure) object it will be
  // forwarded to, encode the forwarding address.  For paged spaces, the
  // 'offset' input/output parameter contains the offset of the forwarded
  // object from the forwarding address of the previous live object in the
  // page as input, and is updated to contain the offset to be used for the
  // next live object in the same page.  For spaces using a different
  // encoding (ie, contiguous spaces), the offset parameter is ignored.
  typedef void (*EncodingFunction)(Heap* heap,
                                   HeapObject* old_object,
                                   int object_size,
                                   Object* new_object,
                                   int* offset);

  // Type of functions to process non-live objects.
  typedef void (*ProcessNonLiveFunction)(HeapObject* object, Isolate* isolate);

  // Pointer to member function, used in IterateLiveObjects.
  typedef int (MarkCompactCollector::*LiveObjectCallback)(HeapObject* obj);

  // Set the global force_compaction flag, it must be called before Prepare
  // to take effect.
  inline void SetFlags(int flags);

  inline bool PreciseSweepingRequired() {
    return sweep_precisely_;
  }

  static void Initialize();

  // Prepares for GC by resetting relocation info in old and map spaces and
  // choosing spaces to compact.
  void Prepare(GCTracer* tracer);

  // Performs a global garbage collection.
  void CollectGarbage();

  // True if the last full GC performed heap compaction.
  bool HasCompacted() { return compacting_collection_; }

  // True after the Prepare phase if the compaction is taking place.
  bool IsCompacting() {
#ifdef DEBUG
    // For the purposes of asserts we don't want this to keep returning true
    // after the collection is completed.
    return state_ != IDLE && compacting_collection_;
#else
    return compacting_collection_;
#endif
  }

  // During a full GC, there is a stack-allocated GCTracer that is used for
  // bookkeeping information.  Return a pointer to that tracer.
  GCTracer* tracer() { return tracer_; }

#ifdef DEBUG
  // Checks whether performing mark-compact collection.
  bool in_use() { return state_ > PREPARE_GC; }
  bool are_map_pointers_encoded() { return state_ == UPDATE_POINTERS; }
#endif

  // Determine type of object and emit deletion log event.
  static void ReportDeleteIfNeeded(HeapObject* obj, Isolate* isolate);

  // Distinguishable invalid map encodings (for single word and multiple words)
  // that indicate free regions.
  static const uint32_t kSingleFreeEncoding = 0;
  static const uint32_t kMultiFreeEncoding = 1;

  inline bool IsMarked(Object* obj);

  inline Heap* heap() const { return heap_; }

  CodeFlusher* code_flusher() { return code_flusher_; }
  inline bool is_code_flushing_enabled() const { return code_flusher_ != NULL; }
  void EnableCodeFlushing(bool enable);

  enum SweeperType {
    CONSERVATIVE,
    LAZY_CONSERVATIVE,
    PRECISE
  };

  // Sweep a single page from the given space conservatively.
  // Return a number of reclaimed bytes.
  static int SweepConservatively(PagedSpace* space, Page* p);

 private:
  MarkCompactCollector();
  ~MarkCompactCollector();

#ifdef DEBUG
  enum CollectorState {
    IDLE,
    PREPARE_GC,
    MARK_LIVE_OBJECTS,
    SWEEP_SPACES,
    ENCODE_FORWARDING_ADDRESSES,
    UPDATE_POINTERS,
    RELOCATE_OBJECTS
  };

  // The current stage of the collector.
  CollectorState state_;
#endif

  // Global flag that forces a compaction.
  bool force_compaction_;

  // Global flag that forces sweeping to be precise, so we can traverse the
  // heap.
  bool sweep_precisely_;

  // Global flag indicating whether spaces were compacted on the last GC.
  bool compacting_collection_;

  // Global flag indicating whether spaces will be compacted on the next GC.
  bool compact_on_next_gc_;

  // The number of objects left marked at the end of the last completed full
  // GC (expected to be zero).
  int previous_marked_count_;

  // A pointer to the current stack-allocated GC tracer object during a full
  // collection (NULL before and after).
  GCTracer* tracer_;

  // Finishes GC, performs heap verification if enabled.
  void Finish();

  // -----------------------------------------------------------------------
  // Phase 1: Marking live objects.
  //
  //  Before: The heap has been prepared for garbage collection by
  //          MarkCompactCollector::Prepare() and is otherwise in its
  //          normal state.
  //
  //   After: Live objects are marked and non-live objects are unmarked.


  friend class RootMarkingVisitor;
  friend class MarkingVisitor;
  friend class StaticMarkingVisitor;
  friend class CodeMarkingVisitor;
  friend class SharedFunctionInfoMarkingVisitor;

  void PrepareForCodeFlushing();

  // Marking operations for objects reachable from roots.
  void MarkLiveObjects();

  void AfterMarking();

  INLINE(void MarkObject(HeapObject* obj, MarkBit mark_bit));

  INLINE(void SetMark(HeapObject* obj, MarkBit mark_bit));

  void ProcessNewlyMarkedObject(HeapObject* obj);

  // Creates back pointers for all map transitions, stores them in
  // the prototype field.  The original prototype pointers are restored
  // in ClearNonLiveTransitions().  All JSObject maps
  // connected by map transitions have the same prototype object, which
  // is why we can use this field temporarily for back pointers.
  void CreateBackPointers();

  // Mark a Map and its DescriptorArray together, skipping transitions.
  void MarkMapContents(Map* map);
  void MarkDescriptorArray(DescriptorArray* descriptors);

  // Mark the heap roots and all objects reachable from them.
  void MarkRoots(RootMarkingVisitor* visitor);

  // Mark the symbol table specially.  References to symbols from the
  // symbol table are weak.
  void MarkSymbolTable();

  // Mark objects in object groups that have at least one object in the
  // group marked.
  void MarkObjectGroups();

  // Mark objects in implicit references groups if their parent object
  // is marked.
  void MarkImplicitRefGroups();

  // Mark all objects which are reachable due to host application
  // logic like object groups or implicit references' groups.
  void ProcessExternalMarking();

  // Mark objects reachable (transitively) from objects in the marking stack
  // or overflowed in the heap.
  void ProcessMarkingDeque();

  // Mark objects reachable (transitively) from objects in the marking
  // stack.  This function empties the marking stack, but may leave
  // overflowed objects in the heap, in which case the marking stack's
  // overflow flag will be set.
  void EmptyMarkingDeque();

  // Refill the marking stack with overflowed objects from the heap.  This
  // function either leaves the marking stack full or clears the overflow
  // flag on the marking stack.
  void RefillMarkingDeque();

  // Callback function for telling whether the object *p is an unmarked
  // heap object.
  static bool IsUnmarkedHeapObject(Object** p);

#ifdef DEBUG
  void UpdateLiveObjectCount(HeapObject* obj);
#endif

  // Test whether a (possibly marked) object is a Map.
  static inline bool SafeIsMap(HeapObject* object);

  // Map transitions from a live map to a dead map must be killed.
  // We replace them with a null descriptor, with the same key.
  void ClearNonLiveTransitions();

  // -----------------------------------------------------------------------
  // Phase 2: Sweeping to clear mark bits and free non-live objects for
  // a non-compacting collection.
  //
  //  Before: Live objects are marked and non-live objects are unmarked.
  //
  //   After: Live objects are unmarked, non-live regions have been added to
  //          their space's free list. Active eden semispace is compacted by
  //          evacuation.
  //


  // Iterates live objects in a space, passes live objects
  // to a callback function which returns the heap size of the object.
  // Returns the number of live objects iterated.
  int IterateLiveObjects(NewSpace* space, LiveObjectCallback size_f);
  int IterateLiveObjects(PagedSpace* space, LiveObjectCallback size_f);

  // Iterates the live objects between a range of addresses, returning the
  // number of live objects.
  int IterateLiveObjectsInRange(Address start, Address end,
                                LiveObjectCallback size_func);

  // If we are not compacting the heap, we simply sweep the spaces except
  // for the large object space, clearing mark bits and adding unmarked
  // regions to each space's free list.
  void SweepSpaces();

  void SweepNewSpace(NewSpace* space);


  void SweepSpace(PagedSpace* space, SweeperType sweeper);


#ifdef DEBUG
  // -----------------------------------------------------------------------
  // Debugging variables, functions and classes
  // Counters used for debugging the marking phase of mark-compact or
  // mark-sweep collection.

  // Size of live objects in Heap::to_space_.
  int live_young_objects_size_;

  // Size of live objects in Heap::old_pointer_space_.
  int live_old_pointer_objects_size_;

  // Size of live objects in Heap::old_data_space_.
  int live_old_data_objects_size_;

  // Size of live objects in Heap::code_space_.
  int live_code_objects_size_;

  // Size of live objects in Heap::map_space_.
  int live_map_objects_size_;

  // Size of live objects in Heap::cell_space_.
  int live_cell_objects_size_;

  // Size of live objects in Heap::lo_space_.
  int live_lo_objects_size_;

  // Number of live bytes in this collection.
  int live_bytes_;

  friend class MarkObjectVisitor;
  static void VisitObject(HeapObject* obj);

  friend class UnmarkObjectVisitor;
  static void UnmarkObject(HeapObject* obj);
#endif

  Heap* heap_;
  MarkingDeque marking_deque_;
  CodeFlusher* code_flusher_;

  friend class Heap;
};


} }  // namespace v8::internal

#endif  // V8_MARK_COMPACT_H_
