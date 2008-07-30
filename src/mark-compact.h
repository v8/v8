// Copyright 2006-2008 Google Inc. All Rights Reserved.
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

namespace v8 { namespace internal {

// Callback function, returns whether an object is alive. The heap size
// of the object is returned in size. It optionally updates the offset
// to the first live object in the page (only used for old and map objects).
typedef bool (*IsAliveFunction)(HeapObject* obj, int* size, int* offset);

// Callback function for non-live blocks in the old generation.
typedef void (*DeallocateFunction)(Address start, int size_in_bytes);


// Forward declaration of visitor.
class MarkingVisitor;

// ----------------------------------------------------------------------------
// Mark-Compact collector
//
// All methods are static.

class MarkCompactCollector : public AllStatic {
 public:
  // Type of functions to compute forwarding addresses of objects in
  // compacted spaces.  Given an object and its size, return a (non-failure)
  // Object* that will be the object after forwarding.  There is a separate
  // allocation function for each (compactable) space based on the location
  // of the object before compaction.
  typedef Object* (*AllocationFunction)(HeapObject* object, int object_size);

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

  // Performs a global garbage collection.
  static void CollectGarbage(GCTracer* tracer);

  // True if the last full GC performed heap compaction.
  static bool HasCompacted() { return compacting_collection_; }

  // True after the Prepare phase if the compaction is taking place.
  static bool IsCompacting() { return compacting_collection_; }

  // The count of the number of objects left marked at the end of the last
  // completed full GC (expected to be zero).
  static int previous_marked_count() { return previous_marked_count_; }

  // During a full GC, there is a stack-allocated GCTracer that is used for
  // bookkeeping information.  Return a pointer to that tracer.
  static GCTracer* tracer() { return tracer_; }

#ifdef DEBUG
  // Checks whether performing mark-compact collection.
  static bool in_use() { return state_ > PREPARE_GC; }
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
    RELOCATE_OBJECTS,
    REBUILD_RSETS
  };

  // The current stage of the collector.
  static CollectorState state_;
#endif
  // Global flag indicating whether spaces were compacted on the last GC.
  static bool compacting_collection_;

  // The number of objects left marked at the end of the last completed full
  // GC (expected to be zero).
  static int previous_marked_count_;

  // A pointer to the current stack-allocated GC tracer object during a full
  // collection (NULL before and after).
  static GCTracer* tracer_;

  // Prepares for GC by resetting relocation info in old and map spaces and
  // choosing spaces to compact.
  static void Prepare();

  // Finishes GC, performs heap verification.
  static void Finish();

  // --------------------------------------------------------------------------
  // Phase 1: functions related to marking phase.
  //   before: Heap is in normal state, collector is 'IDLE'.
  //
  //           The first word of a page in old spaces has the end of
  //           allocation address of the page.
  //
  //           The word at Chunk::high_ address has the address of the
  //           first page in the next chunk. (The address is tagged to
  //           distinguish it from end-of-allocation address).
  //
  //    after: live objects are marked.

  friend class MarkingVisitor;

  // Marking operations for objects reachable from roots.
  static void MarkLiveObjects();
  static void UnmarkLiveObjects();

  // Visit overflowed object, push overflowed object on the marking stack and
  // clear the overflow bit. If the marking stack is overflowed during this
  // process, return false;
  static bool VisitOverflowedObject(HeapObject* obj);

  static void MarkUnmarkedObject(HeapObject* obj);

  static inline void MarkObject(HeapObject* obj) {
     if (!obj->IsMarked()) MarkUnmarkedObject(obj);
  }

  // Mark the heap roots.
  static void MarkStrongRoots(MarkingVisitor* marking_visitor);

  // Mark objects in object groups that have at least one object in the
  // group marked.
  static void MarkObjectGroups();

  // Mark all objects in an object group with at least one marked
  // object, then all objects reachable from marked objects in object
  // groups, and repeat.
  static void ProcessObjectGroups(MarkingVisitor* marking_visitor);

  // Mark all objects reachable (transitively) from objects in the
  // marking stack or marked as overflowed in the heap.
  static void ProcessMarkingStack(MarkingVisitor* marking_visitor);

  // Callback function for telling whether the object *p must be marked.
  static bool MustBeMarked(Object** p);

#ifdef DEBUG
  static void UpdateLiveObjectCount(HeapObject* obj);
  static void VerifyHeapAfterMarkingPhase();
#endif

  // We sweep the large object space in the same way whether we are
  // compacting or not, because the large object space is never compacted.
  static void SweepLargeObjectSpace();

  // --------------------------------------------------------------------------
  // Phase 2: functions related to computing and encoding forwarding pointers
  //   before: live objects' map pointers are marked as '00'
  //    after: Map pointers of live old and map objects have encoded
  //           forwarding pointers and map pointers
  //
  //           The 3rd word of a page has the page top offset after compaction.
  //
  //           The 4th word of a page in the map space has the map index
  //           of this page in the map table. This word is not used in
  //           the old space.
  //
  //           The 5th and 6th words of a page have the start and end
  //           addresses of the first free region in the page.
  //
  //           The 7th word of a page in old spaces has the forwarding address
  //           of the first live object in the page.
  //
  //           Live young objects have their forwarding pointers in
  //           the from space at the same offset to the beginning of the space.

  // Encodes forwarding addresses of objects in compactable parts of the
  // heap.
  static void EncodeForwardingAddresses();

  // Encodes the forwarding addresses of objects in new space.
  static void EncodeForwardingAddressesInNewSpace();

  // Function template to encode the forwarding addresses of objects in
  // paged spaces, parameterized by allocation and non-live processing
  // functions.
  template<AllocationFunction Alloc, ProcessNonLiveFunction ProcessNonLive>
  static void EncodeForwardingAddressesInPagedSpace(PagedSpace* space);

  // Iterates live objects in a space, passes live objects
  // to a callback function which returns the heap size of the object.
  // Returns the number of live objects iterated.
  static int IterateLiveObjects(NewSpace* space, HeapObjectCallback size_f);
  static int IterateLiveObjects(PagedSpace* space, HeapObjectCallback size_f);

  // Iterates the live objects between a range of addresses, returning the
  // number of live objects.
  static int IterateLiveObjectsInRange(Address start, Address end,
                                       HeapObjectCallback size_func);

  // Callback functions for deallocating non-live blocks in the old
  // generation.
  static void DeallocateOldBlock(Address start, int size_in_bytes);
  static void DeallocateCodeBlock(Address start, int size_in_bytes);
  static void DeallocateMapBlock(Address start, int size_in_bytes);

  // Phase 2: If we are not compacting the heap, we simply sweep the spaces
  // except for the large object space, clearing mark bits and adding
  // unmarked regions to each space's free list.
  static void SweepSpaces();

#ifdef DEBUG
  static void VerifyHeapAfterEncodingForwardingAddresses();
#endif

  // --------------------------------------------------------------------------
  // Phase 3: function related to updating pointers and decode map pointers
  //   before: see after phase 2
  //    after: all pointers are updated to forwarding addresses.

  friend class UpdatingVisitor;  // helper for updating visited objects

  // Updates pointers in all spaces.
  static void UpdatePointers();

  // Updates pointers in an object in new space.
  // Returns the heap size of the object.
  static int UpdatePointersInNewObject(HeapObject* obj);

  // Updates pointers in an object in old spaces.
  // Returns the heap size of the object.
  static int UpdatePointersInOldObject(HeapObject* obj);

  // Updates the pointer in a slot.
  static void UpdatePointer(Object** p);

  // Calculates the forwarding address of an object in an old space.
  static Address GetForwardingAddressInOldSpace(HeapObject* obj);

#ifdef DEBUG
  static void VerifyHeapAfterUpdatingPointers();
#endif

  // --------------------------------------------------------------------------
  // Phase 4: functions related to relocating objects
  //     before: see after phase 3
  //      after: heap is in a normal state, except remembered set is not built

  // Relocates objects in all spaces.
  static void RelocateObjects();

  // Converts a code object's inline target to addresses, convention from
  // address to target happens in the marking phase.
  static int ConvertCodeICTargetToAddress(HeapObject* obj);

  // Relocate a map object.
  static int RelocateMapObject(HeapObject* obj);

  // Relocates an old object.
  static int RelocateOldObject(HeapObject* obj);

  // Relocates an immutable object in the code space.
  static int RelocateCodeObject(HeapObject* obj);

  // Copy a new object.
  static int RelocateNewObject(HeapObject* obj);

#ifdef DEBUG
  static void VerifyHeapAfterRelocatingObjects();
#endif

  // ---------------------------------------------------------------------------
  // Phase 5: functions related to rebuilding remembered sets

  // Rebuild remembered set in old and map spaces.
  static void RebuildRSets();

#ifdef DEBUG
  // ---------------------------------------------------------------------------
  // Debugging variables, functions and classes
  // Counters used for debugging the marking phase of mark-compact or
  // mark-sweep collection.

  // Number of live objects in Heap::to_space_.
  static int live_young_objects_;

  // Number of live objects in Heap::old_space_.
  static int live_old_objects_;

  // Number of live objects in Heap::code_space_.
  static int live_immutable_objects_;

  // Number of live objects in Heap::map_space_.
  static int live_map_objects_;

  // Number of live objects in Heap::lo_space_.
  static int live_lo_objects_;

  // Number of live bytes in this collection.
  static int live_bytes_;

  static void VerifyPageHeaders(PagedSpace* space);

  // Verification functions when relocating objects.
  friend class VerifyCopyingVisitor;
  static void VerifyCopyingObjects(Object** p);

  friend class MarkObjectVisitor;
  static void VisitObject(HeapObject* obj);

  friend class UnmarkObjectVisitor;
  static void UnmarkObject(HeapObject* obj);
#endif
};


} }  // namespace v8::internal

#endif  // V8_MARK_COMPACT_H_
