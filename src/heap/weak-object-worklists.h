// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_WEAK_OBJECT_WORKLISTS_H_
#define V8_HEAP_WEAK_OBJECT_WORKLISTS_H_

#include "src/common/globals.h"
#include "src/heap/base/worklist.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-weak-refs.h"

namespace v8 {
namespace internal {

struct Ephemeron {
  HeapObject key;
  HeapObject value;
};

using HeapObjectAndSlot = std::pair<HeapObject, HeapObjectSlot>;
using HeapObjectAndCode = std::pair<HeapObject, Code>;
class EphemeronHashTable;
class JSFunction;
class SharedFunctionInfo;
class TransitionArray;

// Weak objects and weak references discovered during incremental/concurrent
// marking. They are processed in ClearNonLiveReferences after marking.
// Each entry in this list specifies:
// 1) Type of the worklist entry.
// 2) Lower-case name of the worklsit.
// 3) Capitalized name of the worklist.
//
// If you add a new entry, then you also need to implement the corresponding
// Update*() function in the cc file for updating pointers after Scavenge.
#define WEAK_OBJECT_WORKLISTS(F)                                             \
  F(TransitionArray, transition_arrays, TransitionArrays)                    \
  /* Keep track of all EphemeronHashTables in the heap to process            \
     them in the atomic pause. */                                            \
  F(EphemeronHashTable, ephemeron_hash_tables, EphemeronHashTables)          \
  /* Keep track of all ephemerons for concurrent marking tasks. Only store   \
     ephemerons in these worklists if both (key, value) are unreachable at   \
     the moment.                                                             \
     MarkCompactCollector::ProcessEphemeronsUntilFixpoint drains/fills       \
     these worklists. current_ephemerons is used as draining worklist in     \
     the current fixpoint iteration. */                                      \
  F(Ephemeron, current_ephemerons, CurrentEphemerons)                        \
  /* Stores ephemerons to visit in the next fixpoint iteration. */           \
  F(Ephemeron, next_ephemerons, NextEphemerons)                              \
  /* When draining the marking worklist new discovered ephemerons are pushed \
      into this worklist. */                                                 \
  F(Ephemeron, discovered_ephemerons, DiscoveredEphemerons)                  \
  /* TODO(marja): For old space, we only need the slot, not the host object. \
     Optimize this by adding a different storage for old space. */           \
  F(HeapObjectAndSlot, weak_references, WeakReferences)                      \
  F(HeapObjectAndCode, weak_objects_in_code, WeakObjectsInCode)              \
  F(JSWeakRef, js_weak_refs, JSWeakRefs)                                     \
  F(WeakCell, weak_cells, WeakCells)                                         \
  F(SharedFunctionInfo, bytecode_flushing_candidates,                        \
    BytecodeFlushingCandidates)                                              \
  F(JSFunction, flushed_js_functions, FlushedJSFunctions)

class WeakObjects {
 public:
  template <typename Type>
  using WeakObjectWorklist = ::heap::base::Worklist<Type, 64>;

  class Local {
   public:
    explicit Local(WeakObjects* weak_objects);
    bool IsLocalAndGlobalEmpty();
    void Publish();

#define DECLARE_WORKLIST(Type, name, _) WeakObjectWorklist<Type>::Local name;
    WEAK_OBJECT_WORKLISTS(DECLARE_WORKLIST)
#undef DECLARE_WORKLIST

   private:
    // Dummy field used for terminating the initializer list
    // in the constructor.
    bool end_of_initializer_list_;
  };

  void Clear();
  void UpdateAfterScavenge();

#define DECLARE_WORKLIST(Type, name, _) WeakObjectWorklist<Type> name;
  WEAK_OBJECT_WORKLISTS(DECLARE_WORKLIST)
#undef DECLARE_WORKLIST

 private:
#define DECLARE_UPDATE_METHODS(Type, _, Name) \
  void Update##Name(WeakObjectWorklist<Type>&);
  WEAK_OBJECT_WORKLISTS(DECLARE_UPDATE_METHODS)
#undef DECLARE_UPDATE_METHODS

#ifdef DEBUG
  template <typename Type>
  bool ContainsYoungObjects(WeakObjectWorklist<Type>& worklist);
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_WEAK_OBJECT_WORKLISTS_H_
