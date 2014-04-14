// Copyright 2012 the V8 project authors. All rights reserved.
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

#include "objects.h"
#include "transitions-inl.h"
#include "utils.h"

namespace v8 {
namespace internal {


Handle<TransitionArray> TransitionArray::Allocate(Isolate* isolate,
                                                  int number_of_transitions) {
  Handle<FixedArray> array =
      isolate->factory()->NewFixedArray(ToKeyIndex(number_of_transitions));
  array->set(kPrototypeTransitionsIndex, Smi::FromInt(0));
  return Handle<TransitionArray>::cast(array);
}


Handle<TransitionArray> TransitionArray::AllocateSimple(Isolate* isolate,
                                                        Handle<Map> target) {
  Handle<FixedArray> array =
      isolate->factory()->NewFixedArray(kSimpleTransitionSize);
  array->set(kSimpleTransitionTarget, *target);
  return Handle<TransitionArray>::cast(array);
}


void TransitionArray::NoIncrementalWriteBarrierCopyFrom(TransitionArray* origin,
                                                        int origin_transition,
                                                        int target_transition) {
  NoIncrementalWriteBarrierSet(target_transition,
                               origin->GetKey(origin_transition),
                               origin->GetTarget(origin_transition));
}


static bool InsertionPointFound(Name* key1, Name* key2) {
  return key1->Hash() > key2->Hash();
}


Handle<TransitionArray> TransitionArray::NewWith(Handle<Map> map,
                                                 Handle<Name> name,
                                                 Handle<Map> target,
                                                 SimpleTransitionFlag flag) {
  Handle<TransitionArray> result;
  Isolate* isolate = name->GetIsolate();

  if (flag == SIMPLE_TRANSITION) {
    result = AllocateSimple(isolate, target);
  } else {
    result = Allocate(isolate, 1);
    result->NoIncrementalWriteBarrierSet(0, *name, *target);
  }
  result->set_back_pointer_storage(map->GetBackPointer());
  return result;
}


Handle<TransitionArray> TransitionArray::ExtendToFullTransitionArray(
    Handle<Map> containing_map) {
  ASSERT(!containing_map->transitions()->IsFullTransitionArray());
  int nof = containing_map->transitions()->number_of_transitions();

  // A transition array may shrink during GC.
  Handle<TransitionArray> result = Allocate(containing_map->GetIsolate(), nof);
  DisallowHeapAllocation no_gc;
  int new_nof = containing_map->transitions()->number_of_transitions();
  if (new_nof != nof) {
    ASSERT(new_nof == 0);
    result->Shrink(ToKeyIndex(0));
  } else if (nof == 1) {
    result->NoIncrementalWriteBarrierCopyFrom(
        containing_map->transitions(), kSimpleTransitionIndex, 0);
  }

  result->set_back_pointer_storage(
      containing_map->transitions()->back_pointer_storage());
  return result;
}


Handle<TransitionArray> TransitionArray::CopyInsert(Handle<Map> map,
                                                    Handle<Name> name,
                                                    Handle<Map> target,
                                                    SimpleTransitionFlag flag) {
  if (!map->HasTransitionArray()) {
    return TransitionArray::NewWith(map, name, target, flag);
  }

  int number_of_transitions = map->transitions()->number_of_transitions();
  int new_size = number_of_transitions;

  int insertion_index = map->transitions()->Search(*name);
  if (insertion_index == kNotFound) ++new_size;

  Handle<TransitionArray> result = Allocate(map->GetIsolate(), new_size);

  // The map's transition array may have disappeared or grown smaller during
  // the allocation above as it was weakly traversed. Trim the result copy if
  // needed, and recompute variables.
  DisallowHeapAllocation no_gc;
  if (!map->HasTransitionArray()) {
    if (flag == SIMPLE_TRANSITION) {
      ASSERT(result->length() >= kSimpleTransitionSize);
      result->Shrink(kSimpleTransitionSize);
      result->set(kSimpleTransitionTarget, *target);
    } else {
      ASSERT(result->length() >= ToKeyIndex(1));
      result->Shrink(ToKeyIndex(1));
      result->set(kPrototypeTransitionsIndex, Smi::FromInt(0));
      result->NoIncrementalWriteBarrierSet(0, *name, *target);
    }
    result->set_back_pointer_storage(map->GetBackPointer());

    return result;
  }

  TransitionArray* array = map->transitions();
  if (array->number_of_transitions() != number_of_transitions) {
    ASSERT(array->number_of_transitions() < number_of_transitions);

    number_of_transitions = array->number_of_transitions();
    new_size = number_of_transitions;

    insertion_index = array->Search(*name);
    if (insertion_index == kNotFound) ++new_size;

    result->Shrink(ToKeyIndex(new_size));
  }

  if (array->HasPrototypeTransitions()) {
    result->SetPrototypeTransitions(array->GetPrototypeTransitions());
  }

  if (insertion_index != kNotFound) {
    for (int i = 0; i < number_of_transitions; ++i) {
      if (i != insertion_index) {
        result->NoIncrementalWriteBarrierCopyFrom(array, i, i);
      }
    }
    result->NoIncrementalWriteBarrierSet(insertion_index, *name, *target);
    result->set_back_pointer_storage(array->back_pointer_storage());
    return result;
  }

  insertion_index = 0;
  for (; insertion_index < number_of_transitions; ++insertion_index) {
    if (InsertionPointFound(array->GetKey(insertion_index), *name)) break;
    result->NoIncrementalWriteBarrierCopyFrom(
        array, insertion_index, insertion_index);
  }

  result->NoIncrementalWriteBarrierSet(insertion_index, *name, *target);

  for (; insertion_index < number_of_transitions; ++insertion_index) {
    result->NoIncrementalWriteBarrierCopyFrom(
        array, insertion_index, insertion_index + 1);
  }

  result->set_back_pointer_storage(array->back_pointer_storage());
  return result;
}


} }  // namespace v8::internal
