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
      : heap_(heap),
        new_space_bitmap_(NULL) {
  }

  inline MarkBit MarkBitFromNewSpace(HeapObject* obj);

  static inline MarkBit MarkBitFromOldSpace(HeapObject* obj);

  inline MarkBit MarkBitFrom(Address addr);

  inline MarkBit MarkBitFrom(HeapObject* obj) {
    return MarkBitFrom(reinterpret_cast<Address>(obj));
  }

  inline void ClearRange(Address addr, int size);

  void TransferMark(Address old_start, Address new_start);

  bool Setup();

  void TearDown();

 private:
  class BitmapStorageDescriptor {
   public:
    INLINE(static int CellsCount(Address addr)) {
      return HeaderOf(addr)->cells_count_;
    }

    static Bitmap<BitmapStorageDescriptor>* Allocate(int cells_count) {
      VirtualMemory* memory = new VirtualMemory(SizeFor(cells_count));

      if (!memory->Commit(memory->address(), memory->size(), false)) {
        delete memory;
        return NULL;
      }

      Address bitmap_address =
          reinterpret_cast<Address>(memory->address()) + sizeof(Header);
      HeaderOf(bitmap_address)->cells_count_ = cells_count;
      HeaderOf(bitmap_address)->storage_ = memory;
      return Bitmap<BitmapStorageDescriptor>::FromAddress(bitmap_address);
    }

    static void Free(Bitmap<BitmapStorageDescriptor>* bitmap) {
      delete HeaderOf(bitmap->address())->storage_;
    }

   private:
    struct Header {
      VirtualMemory* storage_;
      int cells_count_;
    };

    static int SizeFor(int cell_count) {
      return sizeof(Header) +
          Bitmap<BitmapStorageDescriptor>::SizeFor(cell_count);
    }

    static Header* HeaderOf(Address addr) {
      return reinterpret_cast<Header*>(addr - sizeof(Header));
    }
  };

  typedef Bitmap<BitmapStorageDescriptor> NewSpaceMarkbitsBitmap;

  Heap* heap_;
  NewSpaceMarkbitsBitmap* new_space_bitmap_;
};

// ----------------------------------------------------------------------------
// Marking stack for tracing live objects.

class MarkingStack {
 public:
  MarkingStack() : low_(NULL), top_(NULL), high_(NULL), overflowed_(false) { }

  void Initialize(Address low, Address high) {
    top_ = low_ = reinterpret_cast<HeapObject**>(low);
    high_ = reinterpret_cast<HeapObject**>(high);
    overflowed_ = false;
  }

  bool is_full() const { return top_ >= high_; }

  bool is_empty() const { return top_ <= low_; }

  bool overflowed() const { return overflowed_; }

  void clear_overflowed() { overflowed_ = false; }

  // Push the (marked) object on the marking stack if there is room,
  // otherwise mark the object as overflowed and wait for a rescan of the
  // heap.
  void Push(HeapObject* object) {
    CHECK(object->IsHeapObject());
    if (is_full()) {
      object->SetOverflow();
      overflowed_ = true;
    } else {
      *(top_++) = object;
    }
  }

  HeapObject* Pop() {
    ASSERT(!is_empty());
    HeapObject* object = *(--top_);
    CHECK(object->IsHeapObject());
    return object;
  }

  HeapObject** low() { return low_; }
  HeapObject** top() { return top_; }
  void set_top(HeapObject** top) { top_ = top; }

 private:
  HeapObject** low_;
  HeapObject** top_;
  HeapObject** high_;
  bool overflowed_;

  DISALLOW_COPY_AND_ASSIGN(MarkingStack);
};


// -------------------------------------------------------------------------
// Mark-Compact collector
//
// All methods are static.

class OverflowedObjectsScanner;

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
  typedef void (*ProcessNonLiveFunction)(HeapObject* object);

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
  static void ReportDeleteIfNeeded(HeapObject* obj);

  // Distinguishable invalid map encodings (for single word and multiple words)
  // that indicate free regions.
  static const uint32_t kSingleFreeEncoding = 0;
  static const uint32_t kMultiFreeEncoding = 1;

  inline bool IsMarked(Object* obj);

  inline Heap* heap() const { return heap_; }

  CodeFlusher* code_flusher() { return code_flusher_; }
  inline bool is_code_flushing_enabled() const { return code_flusher_ != NULL; }
  void EnableCodeFlushing(bool enable);

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
  void ProcessMarkingStack();

  // Mark objects reachable (transitively) from objects in the marking
  // stack.  This function empties the marking stack, but may leave
  // overflowed objects in the heap, in which case the marking stack's
  // overflow flag will be set.
  void EmptyMarkingStack();

  // Refill the marking stack with overflowed objects from the heap.  This
  // function either leaves the marking stack full or clears the overflow
  // flag on the marking stack.
  void RefillMarkingStack();

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

  enum SweeperType { CONSERVATIVE, PRECISE };

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
  MarkingStack marking_stack_;
  CodeFlusher* code_flusher_;

  friend class Heap;
  friend class OverflowedObjectsScanner;
};


} }  // namespace v8::internal

#endif  // V8_MARK_COMPACT_H_
