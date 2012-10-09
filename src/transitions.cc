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


MaybeObject* TransitionArray::Allocate(int number_of_transitions,
                                       JSGlobalPropertyCell* descriptors_cell) {
  Heap* heap = Isolate::Current()->heap();

  if (descriptors_cell == NULL) {
    MaybeObject* maybe_cell =
        heap->AllocateJSGlobalPropertyCell(heap->empty_descriptor_array());
    if (!maybe_cell->To(&descriptors_cell)) return maybe_cell;
  }

  // Use FixedArray to not use DescriptorArray::cast on incomplete object.
  FixedArray* array;
  MaybeObject* maybe_array =
      heap->AllocateFixedArray(ToKeyIndex(number_of_transitions));
  if (!maybe_array->To(&array)) return maybe_array;

  array->set(kDescriptorsPointerIndex, descriptors_cell);
  array->set(kElementsTransitionIndex, Smi::FromInt(0));
  array->set(kPrototypeTransitionsIndex, Smi::FromInt(0));
  return array;
}


void TransitionArray::NoIncrementalWriteBarrierCopyFrom(TransitionArray* origin,
                                                        int origin_transition,
                                                        int target_transition) {
  NoIncrementalWriteBarrierSet(target_transition,
                               origin->GetKey(origin_transition),
                               origin->GetTarget(origin_transition));
}


static bool InsertionPointFound(String* key1, String* key2) {
  return key1->Hash() > key2->Hash();
}


MaybeObject* TransitionArray::NewWith(
    String* name,
    Map* target,
    JSGlobalPropertyCell* descriptors_pointer,
    Object* back_pointer) {
  TransitionArray* result;

  MaybeObject* maybe_array = TransitionArray::Allocate(1, descriptors_pointer);
  if (!maybe_array->To(&result)) return maybe_array;

  result->NoIncrementalWriteBarrierSet(0, name, target);
  result->set_back_pointer_storage(back_pointer);
  return result;
}


MaybeObject* TransitionArray::CopyInsert(String* name, Map* target) {
  TransitionArray* result;

  int number_of_transitions = this->number_of_transitions();
  int new_size = number_of_transitions;

  int insertion_index = this->Search(name);
  if (insertion_index == kNotFound) ++new_size;

  MaybeObject* maybe_array;
  maybe_array = TransitionArray::Allocate(new_size, descriptors_pointer());
  if (!maybe_array->To(&result)) return maybe_array;

  if (HasElementsTransition()) {
    result->set_elements_transition(elements_transition());
  }

  if (HasPrototypeTransitions()) {
    result->SetPrototypeTransitions(GetPrototypeTransitions());
  }

  if (insertion_index != kNotFound) {
    for (int i = 0; i < number_of_transitions; ++i) {
      if (i != insertion_index) {
        result->NoIncrementalWriteBarrierCopyFrom(this, i, i);
      }
    }
    result->NoIncrementalWriteBarrierSet(insertion_index, name, target);
    return result;
  }

  insertion_index = 0;
  for (; insertion_index < number_of_transitions; ++insertion_index) {
    if (InsertionPointFound(GetKey(insertion_index), name)) break;
    result->NoIncrementalWriteBarrierCopyFrom(
        this, insertion_index, insertion_index);
  }

  result->NoIncrementalWriteBarrierSet(insertion_index, name, target);

  for (; insertion_index < number_of_transitions; ++insertion_index) {
    result->NoIncrementalWriteBarrierCopyFrom(
        this, insertion_index, insertion_index + 1);
  }

  result->set_back_pointer_storage(back_pointer_storage());
  return result;
}


} }  // namespace v8::internal
