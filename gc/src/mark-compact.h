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

namespace v8 {
namespace internal {

// Callback function, returns whether an object is alive. The heap size
// of the object is returned in size. It optionally updates the offset
// to the first live object in the page (only used for old and map objects).
typedef bool (*IsAliveFunction)(HeapObject* obj, int* size, int* offset);

// Forward declarations.
class RootMarkingVisitor;
class MarkingVisitor;


class Marking {
 public:
  static inline MarkBit MarkBitFrom(HeapObject* obj) {
    return MarkBitFrom(reinterpret_cast<Address>(obj));
  }

  static inline MarkBit MarkBitFromNewSpace(HeapObject* obj) {
    ASSERT(Heap::InNewSpace(obj));
    uint32_t index = Heap::new_space()->AddressToMarkbitIndex(
        reinterpret_cast<Address>(obj));
    return new_space_bitmap_->MarkBitFromIndex(index);
  }

  static inline MarkBit MarkBitFromOldSpace(HeapObject* obj) {
    ASSERT(!Heap::InNewSpace(obj));
    ASSERT(obj->IsHeapObject());
    Address addr = reinterpret_cast<Address>(obj);
    Page *p = Page::FromAddress(addr);
    return p->markbits()->MarkBitFromIndex(p->AddressToMarkbitIndex(addr));
  }

  static inline MarkBit MarkBitFrom(Address addr) {
    if (Heap::InNewSpace(addr)) {
      uint32_t index = Heap::new_space()->AddressToMarkbitIndex(addr);
      return new_space_bitmap_->MarkBitFromIndex(index);
    } else {
      Page *p = Page::FromAddress(addr);
      return p->markbits()->MarkBitFromIndex(p->AddressToMarkbitIndex(addr),
                                             p->ContainsOnlyData());
    }
  }

  static void ClearRange(Address addr, int size) {
    if (Heap::InNewSpace(addr)) {
      uint32_t index = Heap::new_space()->AddressToMarkbitIndex(addr);
      new_space_bitmap_->ClearRange(index, size >> kPointerSizeLog2);
    } else {
      Page *p = Page::FromAddress(addr);
      p->markbits()->ClearRange(p->FastAddressToMarkbitIndex(addr),
                                size >> kPointerSizeLog2);
    }
  }

  // Find first marked object in the given cell with index cell_index on
  // the given page.
  INLINE(static Address FirstMarkedObject(Page* page,
                                          uint32_t cell_index,
                                          uint32_t cell)) {
    ASSERT(cell != 0);
    uint32_t bit = CompilerIntrinsics::CountTrailingZeros(cell);
    return page->MarkbitIndexToAddress(
        Page::MarkbitsBitmap::CellToIndex(cell_index) + bit);
  }

  static void TransferMark(Address old_start, Address new_start);

  static bool Setup();

  static void TearDown();

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

  static NewSpaceMarkbitsBitmap* new_space_bitmap_;
};

// -------------------------------------------------------------------------
// Mark-Compact collector
//
// All methods are static.

class MarkCompactCollector: public AllStatic {
 public:
  // Type of functions to compute forwarding addresses of objects in
  // compacted spaces.  Given an object and its size, return a (non-failure)
  // Object* that will be the object after forwarding.  There is a separate
  // allocation function for each (compactable) space based on the location
  // of the object before compaction.
  typedef MaybeObject* (*AllocationFunction)(HeapObject* object,
                                             int object_size);

  // Type of functions to encode the forwarding address for an object.
  // Given the object, its size, and the new (non-failure) object it will be
  // forwarded to, encode the forwarding address.  For paged spaces, the
  // 'offset' input/output parameter contains the offset of the forwarded
  // object from the forwarding address of the previous live object in the
  // page as input, and is updated to contain the offset to be used for the
  // next live object in the same page.  For spaces using a different
  // encoding (ie, contiguous spaces), the offset parameter is ignored.
  typedef void (*EncodingFunction)(HeapObject* old_object,
                                   int object_size,
                                   Object* new_object,
                                   int* offset);

  // Type of functions to process non-live objects.
  typedef void (*ProcessNonLiveFunction)(HeapObject* object);

  // Set the global force_compaction flag, it must be called before Prepare
  // to take effect.
  static void SetFlags(int flags) {
    force_compaction_ = ((flags & Heap::kForceCompactionMask) != 0);
    sweep_precisely_ = ((flags & Heap::kMakeHeapIterableMask) != 0);
  }


  static void Initialize();

  // Prepares for GC by resetting relocation info in old and map spaces and
  // choosing spaces to compact.
  static void Prepare(GCTracer* tracer);

  // Performs a global garbage collection.
  static void CollectGarbage();

  // True if the last full GC performed heap compaction.
  static bool HasCompacted() { return compacting_collection_; }

  // True after the Prepare phase if the compaction is taking place.
  static bool IsCompacting() {
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
  static GCTracer* tracer() { return tracer_; }

#ifdef DEBUG
  // Checks whether performing mark-compact collection.
  static bool in_use() { return state_ > PREPARE_GC; }
  static bool are_map_pointers_encoded() { return state_ == UPDATE_POINTERS; }
#endif

  // Determine type of object and emit deletion log event.
  static void ReportDeleteIfNeeded(HeapObject* obj);

  // Distinguishable invalid map encodings (for single word and multiple words)
  // that indicate free regions.
  static const uint32_t kSingleFreeEncoding = 0;
  static const uint32_t kMultiFreeEncoding = 1;

#ifdef DEBUG
  static bool IsMarked(Object* obj) {
    ASSERT(obj->IsHeapObject());
    HeapObject* heap_object = HeapObject::cast(obj);
    return Marking::MarkBitFrom(heap_object).Get();
  }
#endif

 private:
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
  static CollectorState state_;
#endif

  // Global flag that forces a compaction.
  static bool force_compaction_;

  // Global flag that forces sweeping to be precise, so we can traverse the
  // heap.
  static bool sweep_precisely_;

  // Global flag indicating whether spaces were compacted on the last GC.
  static bool compacting_collection_;

  // Global flag indicating whether spaces will be compacted on the next GC.
  static bool compact_on_next_gc_;

  // A pointer to the current stack-allocated GC tracer object during a full
  // collection (NULL before and after).
  static GCTracer* tracer_;

  // Finishes GC, performs heap verification if enabled.
  static void Finish();

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

  static void PrepareForCodeFlushing();

  // Marking operations for objects reachable from roots.
  static void MarkLiveObjects();

  static void AfterMarking();


  INLINE(static void MarkObject(HeapObject* obj, MarkBit mark_bit)) {
    ASSERT(Marking::MarkBitFrom(obj) == mark_bit);
    if (!mark_bit.Get()) {
      mark_bit.Set();
      tracer_->increment_marked_count();
#ifdef DEBUG
      UpdateLiveObjectCount(obj);
#endif
      ProcessNewlyMarkedObject(obj);
    }
  }

  INLINE(static void SetMark(HeapObject* obj, MarkBit mark_bit)) {
    ASSERT(Marking::MarkBitFrom(obj) == mark_bit);
    mark_bit.Set();
    tracer_->increment_marked_count();
#ifdef DEBUG
    UpdateLiveObjectCount(obj);
#endif
  }

  static void ProcessNewlyMarkedObject(HeapObject* obj);

  // Creates back pointers for all map transitions, stores them in
  // the prototype field.  The original prototype pointers are restored
  // in ClearNonLiveTransitions().  All JSObject maps
  // connected by map transitions have the same prototype object, which
  // is why we can use this field temporarily for back pointers.
  static void CreateBackPointers();

  // Mark a Map and its DescriptorArray together, skipping transitions.
  static void MarkMapContents(Map* map);
  static void MarkDescriptorArray(DescriptorArray* descriptors);

  // Mark the heap roots and all objects reachable from them.
  static void MarkRoots(RootMarkingVisitor* visitor);

  // Mark the symbol table specially.  References to symbols from the
  // symbol table are weak.
  static void MarkSymbolTable();

  // Mark objects in object groups that have at least one object in the
  // group marked.
  static void MarkObjectGroups();

  // Mark all objects in an object group with at least one marked
  // object, then all objects reachable from marked objects in object
  // groups, and repeat.
  static void ProcessObjectGroups();

  // Mark objects reachable (transitively) from objects in the marking stack
  // or overflowed in the heap.
  static void ProcessMarkingStack();

  // Mark objects reachable (transitively) from objects in the marking
  // stack.  This function empties the marking stack, but may leave
  // overflowed objects in the heap, in which case the marking stack's
  // overflow flag will be set.
  static void EmptyMarkingStack();

  // Refill the marking stack with overflowed objects from the heap.  This
  // function either leaves the marking stack full or clears the overflow
  // flag on the marking stack.
  static void RefillMarkingStack();

  // Callback function for telling whether the object *p is an unmarked
  // heap object.
  static bool IsUnmarkedHeapObject(Object** p);

#ifdef DEBUG
  static void UpdateLiveObjectCount(HeapObject* obj);
#endif

  // Test whether a (possibly marked) object is a Map.
  static inline bool SafeIsMap(HeapObject* object);

  // Map transitions from a live map to a dead map must be killed.
  // We replace them with a null descriptor, with the same key.
  static void ClearNonLiveTransitions();

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
  static int IterateLiveObjects(NewSpace* space, HeapObjectCallback size_f);
  static int IterateLiveObjects(PagedSpace* space, HeapObjectCallback size_f);

  // Iterates the live objects between a range of addresses, returning the
  // number of live objects.
  static int IterateLiveObjectsInRange(Address start, Address end,
                                       HeapObjectCallback size_func);

  // If we are not compacting the heap, we simply sweep the spaces except
  // for the large object space, clearing mark bits and adding unmarked
  // regions to each space's free list.
  static void SweepSpaces();

  static void SweepNewSpace(NewSpace* space);

  enum SweeperType { CONSERVATIVE, PRECISE };

  static void SweepSpace(PagedSpace* space, SweeperType sweeper);

#ifdef DEBUG
  // -----------------------------------------------------------------------
  // Debugging variables, functions and classes
  // Counters used for debugging the marking phase of mark-compact or
  // mark-sweep collection.

  // Size of live objects in Heap::to_space_.
  static int live_young_objects_size_;

  // Size of live objects in Heap::old_pointer_space_.
  static int live_old_pointer_objects_size_;

  // Size of live objects in Heap::old_data_space_.
  static int live_old_data_objects_size_;

  // Size of live objects in Heap::code_space_.
  static int live_code_objects_size_;

  // Size of live objects in Heap::map_space_.
  static int live_map_objects_size_;

  // Size of live objects in Heap::cell_space_.
  static int live_cell_objects_size_;

  // Size of live objects in Heap::lo_space_.
  static int live_lo_objects_size_;

  // Number of live bytes in this collection.
  static int live_bytes_;

  friend class MarkObjectVisitor;
  static void VisitObject(HeapObject* obj);

  friend class UnmarkObjectVisitor;
  static void UnmarkObject(HeapObject* obj);
#endif
};


} }  // namespace v8::internal

#endif  // V8_MARK_COMPACT_H_
