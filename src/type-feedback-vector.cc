// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/ic/ic-state.h"
#include "src/objects.h"
#include "src/type-feedback-vector-inl.h"

namespace v8 {
namespace internal {

// static
TypeFeedbackVector::VectorICKind TypeFeedbackVector::FromCodeKind(
    Code::Kind kind) {
  switch (kind) {
    case Code::CALL_IC:
      return KindCallIC;
    case Code::LOAD_IC:
      return KindLoadIC;
    case Code::KEYED_LOAD_IC:
      return KindKeyedLoadIC;
    default:
      // Shouldn't get here.
      UNREACHABLE();
  }

  return KindUnused;
}


// static
Code::Kind TypeFeedbackVector::FromVectorICKind(VectorICKind kind) {
  switch (kind) {
    case KindCallIC:
      return Code::CALL_IC;
    case KindLoadIC:
      return Code::LOAD_IC;
    case KindKeyedLoadIC:
      return Code::KEYED_LOAD_IC;
    case KindUnused:
      break;
  }
  // Sentinel for no information.
  return Code::NUMBER_OF_KINDS;
}


Code::Kind TypeFeedbackVector::GetKind(FeedbackVectorICSlot slot) const {
  if (!FLAG_vector_ics) {
    // We only have CALL_ICs
    return Code::CALL_IC;
  }

  int index = VectorICComputer::index(kReservedIndexCount, slot.ToInt());
  int data = Smi::cast(get(index))->value();
  VectorICKind b = VectorICComputer::decode(data, slot.ToInt());
  return FromVectorICKind(b);
}


void TypeFeedbackVector::SetKind(FeedbackVectorICSlot slot, Code::Kind kind) {
  if (!FLAG_vector_ics) {
    // Nothing to do if we only have CALL_ICs
    return;
  }

  VectorICKind b = FromCodeKind(kind);
  int index = VectorICComputer::index(kReservedIndexCount, slot.ToInt());
  int data = Smi::cast(get(index))->value();
  int new_data = VectorICComputer::encode(data, slot.ToInt(), b);
  set(index, Smi::FromInt(new_data));
}


// static
Handle<TypeFeedbackVector> TypeFeedbackVector::Allocate(Isolate* isolate,
                                                        int slot_count,
                                                        int ic_slot_count) {
  int index_count =
      FLAG_vector_ics ? VectorICComputer::word_count(ic_slot_count) : 0;
  int length = slot_count + ic_slot_count + index_count + kReservedIndexCount;
  if (length == kReservedIndexCount) {
    return Handle<TypeFeedbackVector>::cast(
        isolate->factory()->empty_fixed_array());
  }

  Handle<FixedArray> array = isolate->factory()->NewFixedArray(length, TENURED);
  if (ic_slot_count > 0) {
    array->set(kFirstICSlotIndex,
               Smi::FromInt(slot_count + index_count + kReservedIndexCount));
  } else {
    array->set(kFirstICSlotIndex, Smi::FromInt(length));
  }
  array->set(kWithTypesIndex, Smi::FromInt(0));
  array->set(kGenericCountIndex, Smi::FromInt(0));
  // Fill the indexes with zeros.
  for (int i = 0; i < index_count; i++) {
    array->set(kReservedIndexCount + i, Smi::FromInt(0));
  }

  // Ensure we can skip the write barrier
  Handle<Object> uninitialized_sentinel = UninitializedSentinel(isolate);
  DCHECK_EQ(isolate->heap()->uninitialized_symbol(), *uninitialized_sentinel);
  for (int i = kReservedIndexCount + index_count; i < length; i++) {
    array->set(i, *uninitialized_sentinel, SKIP_WRITE_BARRIER);
  }
  return Handle<TypeFeedbackVector>::cast(array);
}


// static
Handle<TypeFeedbackVector> TypeFeedbackVector::Copy(
    Isolate* isolate, Handle<TypeFeedbackVector> vector) {
  Handle<TypeFeedbackVector> result;
  result = Handle<TypeFeedbackVector>::cast(
      isolate->factory()->CopyFixedArray(Handle<FixedArray>::cast(vector)));
  return result;
}


void TypeFeedbackVector::ClearSlots(SharedFunctionInfo* shared) {
  int slots = Slots();
  Isolate* isolate = GetIsolate();
  Object* uninitialized_sentinel =
      TypeFeedbackVector::RawUninitializedSentinel(isolate->heap());

  for (int i = 0; i < slots; i++) {
    FeedbackVectorSlot slot(i);
    Object* obj = Get(slot);
    if (obj->IsHeapObject()) {
      InstanceType instance_type =
          HeapObject::cast(obj)->map()->instance_type();
      // AllocationSites are exempt from clearing. They don't store Maps
      // or Code pointers which can cause memory leaks if not cleared
      // regularly.
      if (instance_type != ALLOCATION_SITE_TYPE) {
        Set(slot, uninitialized_sentinel, SKIP_WRITE_BARRIER);
      }
    }
  }

  slots = ICSlots();
  if (slots == 0) return;

  // Now clear vector-based ICs. They are all CallICs.
  // Try and pass the containing code (the "host").
  Code* host = shared->code();
  for (int i = 0; i < slots; i++) {
    FeedbackVectorICSlot slot(i);
    Object* obj = Get(slot);
    if (obj != uninitialized_sentinel) {
      ICUtility::Clear(isolate, Code::CALL_IC, host, this, slot);
    }
  }
}
}
}  // namespace v8::internal
