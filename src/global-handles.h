// Copyright 2007-2008 the V8 project authors. All rights reserved.
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

#ifndef V8_GLOBAL_HANDLES_H_
#define V8_GLOBAL_HANDLES_H_

#include "../include/v8-profiler.h"

#include "list-inl.h"

namespace v8 {
namespace internal {

// Structure for tracking global handles.
// A single list keeps all the allocated global handles.
// Destroyed handles stay in the list but is added to the free list.
// At GC the destroyed global handles are removed from the free list
// and deallocated.

// An object group is treated like a single JS object: if one of object in
// the group is alive, all objects in the same group are considered alive.
// An object group is used to simulate object relationship in a DOM tree.
class ObjectGroup {
 public:
  static ObjectGroup* New(Object*** handles,
                          size_t length,
                          v8::RetainedObjectInfo* info) {
    ASSERT(length > 0);
    ObjectGroup* group = reinterpret_cast<ObjectGroup*>(
        malloc(OFFSET_OF(ObjectGroup, objects_[length])));
    group->length_ = length;
    group->info_ = info;
    CopyWords(group->objects_, handles, static_cast<int>(length));
    return group;
  }

  void Dispose() {
    if (info_ != NULL) info_->Dispose();
    free(this);
  }

  size_t length_;
  v8::RetainedObjectInfo* info_;
  Object** objects_[1];  // Variable sized array.

 private:
  void* operator new(size_t size);
  void operator delete(void* p);
  ~ObjectGroup();
  DISALLOW_IMPLICIT_CONSTRUCTORS(ObjectGroup);
};


// An implicit references group consists of two parts: a parent object and
// a list of children objects.  If the parent is alive, all the children
// are alive too.
class ImplicitRefGroup {
 public:
  static ImplicitRefGroup* New(HeapObject** parent,
                               Object*** children,
                               size_t length) {
    ASSERT(length > 0);
    ImplicitRefGroup* group = reinterpret_cast<ImplicitRefGroup*>(
        malloc(OFFSET_OF(ImplicitRefGroup, children_[length])));
    group->parent_ = parent;
    group->length_ = length;
    CopyWords(group->children_, children, static_cast<int>(length));
    return group;
  }

  void Dispose() {
    free(this);
  }

  HeapObject** parent_;
  size_t length_;
  Object** children_[1];  // Variable sized array.

 private:
  void* operator new(size_t size);
  void operator delete(void* p);
  ~ImplicitRefGroup();
  DISALLOW_IMPLICIT_CONSTRUCTORS(ImplicitRefGroup);
};


typedef void (*WeakReferenceGuest)(Object* object, void* parameter);

class GlobalHandles {
 public:
  ~GlobalHandles();

  // Creates a new global handle that is alive until Destroy is called.
  Handle<Object> Create(Object* value);

  // Destroy a global handle.
  void Destroy(Object** location);

  // Make the global handle weak and set the callback parameter for the
  // handle.  When the garbage collector recognizes that only weak global
  // handles point to an object the handles are cleared and the callback
  // function is invoked (for each handle) with the handle and corresponding
  // parameter as arguments.  Note: cleared means set to Smi::FromInt(0). The
  // reason is that Smi::FromInt(0) does not change during garage collection.
  void MakeWeak(Object** location,
                void* parameter,
                WeakReferenceCallback callback);

  static void SetWrapperClassId(Object** location, uint16_t class_id);

  // Returns the current number of weak handles.
  int NumberOfWeakHandles() { return number_of_weak_handles_; }

  void RecordStats(HeapStats* stats);

  // Returns the current number of weak handles to global objects.
  // These handles are also included in NumberOfWeakHandles().
  int NumberOfGlobalObjectWeakHandles() {
    return number_of_global_object_weak_handles_;
  }

  // Clear the weakness of a global handle.
  void ClearWeakness(Object** location);

  // Tells whether global handle is near death.
  static bool IsNearDeath(Object** location);

  // Tells whether global handle is weak.
  static bool IsWeak(Object** location);

  // Process pending weak handles.
  // Returns true if next major GC is likely to collect more garbage.
  bool PostGarbageCollectionProcessing();

  // Iterates over all strong handles.
  void IterateStrongRoots(ObjectVisitor* v);

  // Iterates over all handles.
  void IterateAllRoots(ObjectVisitor* v);

  // Iterates over all handles that have embedder-assigned class ID.
  void IterateAllRootsWithClassIds(ObjectVisitor* v);

  // Iterates over all weak roots in heap.
  void IterateWeakRoots(ObjectVisitor* v);

  // Iterates over weak roots that are bound to a given callback.
  void IterateWeakRoots(WeakReferenceGuest f,
                        WeakReferenceCallback callback);

  // Find all weak handles satisfying the callback predicate, mark
  // them as pending.
  void IdentifyWeakHandles(WeakSlotCallback f);

  // Add an object group.
  // Should be only used in GC callback function before a collection.
  // All groups are destroyed after a mark-compact collection.
  void AddObjectGroup(Object*** handles,
                      size_t length,
                      v8::RetainedObjectInfo* info);

  // Add an implicit references' group.
  // Should be only used in GC callback function before a collection.
  // All groups are destroyed after a mark-compact collection.
  void AddImplicitReferences(HeapObject** parent,
                             Object*** children,
                             size_t length);

  // Returns the object groups.
  List<ObjectGroup*>* object_groups() { return &object_groups_; }

  // Returns the implicit references' groups.
  List<ImplicitRefGroup*>* implicit_ref_groups() {
    return &implicit_ref_groups_;
  }

  // Remove bags, this should only happen after GC.
  void RemoveObjectGroups();
  void RemoveImplicitRefGroups();

  // Tear down the global handle structure.
  void TearDown();

  Isolate* isolate() { return isolate_; }

#ifdef DEBUG
  void PrintStats();
  void Print();
#endif
  class Pool;
 private:
  explicit GlobalHandles(Isolate* isolate);

  // Internal node structure, one for each global handle.
  class Node;

  Isolate* isolate_;

  // Field always containing the number of weak and near-death handles.
  int number_of_weak_handles_;

  // Field always containing the number of weak and near-death handles
  // to global objects.  These objects are also included in
  // number_of_weak_handles_.
  int number_of_global_object_weak_handles_;

  // Global handles are kept in a single linked list pointed to by head_.
  Node* head_;
  Node* head() { return head_; }
  void set_head(Node* value) { head_ = value; }

  // Free list for DESTROYED global handles not yet deallocated.
  Node* first_free_;
  Node* first_free() { return first_free_; }
  void set_first_free(Node* value) { first_free_ = value; }

  // List of deallocated nodes.
  // Deallocated nodes form a prefix of all the nodes and
  // |first_deallocated| points to last deallocated node before
  // |head|.  Those deallocated nodes are additionally linked
  // by |next_free|:
  //                                    1st deallocated  head
  //                                           |          |
  //                                           V          V
  //    node          node        ...         node       node
  //      .next      -> .next ->                .next ->
  //   <- .next_free <- .next_free           <- .next_free
  Node* first_deallocated_;
  Node* first_deallocated() { return first_deallocated_; }
  void set_first_deallocated(Node* value) {
    first_deallocated_ = value;
  }

  Pool* pool_;
  int post_gc_processing_count_;
  List<ObjectGroup*> object_groups_;
  List<ImplicitRefGroup*> implicit_ref_groups_;

  friend class Isolate;

  DISALLOW_COPY_AND_ASSIGN(GlobalHandles);
};


} }  // namespace v8::internal

#endif  // V8_GLOBAL_HANDLES_H_
