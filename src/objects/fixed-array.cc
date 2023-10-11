// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/fixed-array.h"

#include "src/objects/map-inl.h"

namespace v8 {
namespace internal {

int FixedArrayBase::GetMaxLengthForNewSpaceAllocation(ElementsKind kind) {
  return ((kMaxRegularHeapObjectSize - FixedArrayBase::kHeaderSize) >>
          ElementsKindToShiftSize(kind));
}

bool FixedArrayBase::IsCowArray() const {
  return map() == GetReadOnlyRoots().fixed_cow_array_map();
}

Handle<FixedArray> FixedArray::SetAndGrow(Isolate* isolate,
                                          Handle<FixedArray> array, int index,
                                          Handle<Object> value) {
  int len = array->length();
  if (index >= len) {
    int new_capacity = FixedArray::NewCapacityForIndex(index, len);
    array = Handle<FixedArray>::cast(
        FixedArray::Resize(isolate, array, new_capacity));
    // TODO(jgruber): This is somewhat subtle - other FixedArray methods
    // use `undefined` as a filler. Make this more explicit.
    array->FillWithHoles(len, new_capacity);
  }

  array->set(index, *value);
  return array;
}

Handle<FixedArray> FixedArray::ShrinkOrEmpty(Isolate* isolate,
                                             Handle<FixedArray> array,
                                             int new_length) {
  if (new_length == 0) {
    return ReadOnlyRoots{isolate}.empty_fixed_array_handle();
  }
  array->Shrink(isolate, new_length);
  return array;
}

void FixedArray::Shrink(Isolate* isolate, int new_length) {
  CHECK_GT(new_length, 0);
  DCHECK_LE(new_length, length());
  if (new_length < length()) {
    isolate->heap()->RightTrimFixedArray(FixedArrayBase::cast(*this),
                                         length() - new_length);
  }
}

// static
Handle<ArrayList> ArrayList::Add(Isolate* isolate, Handle<ArrayList> array,
                                 Handle<Object> obj,
                                 AllocationType allocation) {
  int length = array->Length();
  array = EnsureSpace(isolate, array, length + 1, allocation);
  // Check that GC didn't remove elements from the array.
  DCHECK_EQ(array->Length(), length);
  {
    DisallowGarbageCollection no_gc;
    Tagged<ArrayList> raw_array = *array;
    raw_array->Set(length, *obj);
    raw_array->SetLength(length + 1);
  }
  return array;
}

Handle<ArrayList> ArrayList::Add(Isolate* isolate, Handle<ArrayList> array,
                                 Tagged<Smi> obj1) {
  int length = array->Length();
  array = EnsureSpace(isolate, array, length + 1);
  // Check that GC didn't remove elements from the array.
  DCHECK_EQ(array->Length(), length);
  {
    DisallowGarbageCollection no_gc;
    Tagged<ArrayList> raw_array = *array;
    raw_array->Set(length, obj1);
    raw_array->SetLength(length + 1);
  }
  return array;
}

// static
Handle<ArrayList> ArrayList::Add(Isolate* isolate, Handle<ArrayList> array,
                                 Handle<Object> obj1, Handle<Object> obj2) {
  int length = array->Length();
  array = EnsureSpace(isolate, array, length + 2);
  // Check that GC didn't remove elements from the array.
  DCHECK_EQ(array->Length(), length);
  {
    DisallowGarbageCollection no_gc;
    Tagged<ArrayList> raw_array = *array;
    raw_array->Set(length, *obj1);
    raw_array->Set(length + 1, *obj2);
    raw_array->SetLength(length + 2);
  }
  return array;
}

Handle<ArrayList> ArrayList::Add(Isolate* isolate, Handle<ArrayList> array,
                                 Handle<Object> obj1, Tagged<Smi> obj2,
                                 Tagged<Smi> obj3, Tagged<Smi> obj4) {
  int length = array->Length();
  array = EnsureSpace(isolate, array, length + 4);
  // Check that GC didn't remove elements from the array.
  DCHECK_EQ(array->Length(), length);
  {
    DisallowGarbageCollection no_gc;
    Tagged<ArrayList> raw_array = *array;
    raw_array->Set(length, *obj1);
    raw_array->Set(length + 1, obj2);
    raw_array->Set(length + 2, obj3);
    raw_array->Set(length + 3, obj4);
    raw_array->SetLength(length + 4);
  }
  return array;
}

// static
Handle<ArrayList> ArrayList::New(Isolate* isolate, int size,
                                 AllocationType allocation) {
  return isolate->factory()->NewArrayList(size, allocation);
}

// static
Handle<FixedArray> ArrayList::Elements(Isolate* isolate,
                                       Handle<ArrayList> array) {
  int length = array->Length();
  Handle<FixedArray> result = isolate->factory()->NewFixedArray(length);

  if (length != 0) {
    DisallowGarbageCollection no_gc;
    Tagged<FixedArray> dst = *result;
    WriteBarrierMode mode = dst->GetWriteBarrierMode(no_gc);
    // Do not copy the first entry, i.e., the length.
    FixedArray::CopyElements(isolate, dst, 0, *array, kFirstIndex, length,
                             mode);
  }

  return result;
}

namespace {

Handle<FixedArray> EnsureSpaceInFixedArray(Isolate* isolate,
                                           Handle<FixedArray> array, int length,
                                           AllocationType allocation) {
  // Ensure calculation matches CodeStubAssembler::ArrayListEnsureSpace.
  int capacity = array->length();
  if (capacity < length) {
    int new_capacity = length;
    new_capacity = new_capacity + std::max(new_capacity / 2, 2);
    int grow_by = new_capacity - capacity;
    array =
        isolate->factory()->CopyFixedArrayAndGrow(array, grow_by, allocation);
  }
  return array;
}

}  // namespace

// static
Handle<ArrayList> ArrayList::EnsureSpace(Isolate* isolate,
                                         Handle<ArrayList> array, int length,
                                         AllocationType allocation) {
  DCHECK_LT(0, length);
  Handle<ArrayList> new_array = Handle<ArrayList>::cast(EnsureSpaceInFixedArray(
      isolate, array, kFirstIndex + length, allocation));
  DCHECK_EQ(array->Length(), new_array->Length());
  return new_array;
}

// static
Handle<WeakArrayList> WeakArrayList::AddToEnd(Isolate* isolate,
                                              Handle<WeakArrayList> array,
                                              MaybeObjectHandle value) {
  int length = array->length();
  array = EnsureSpace(isolate, array, length + 1);
  {
    DisallowGarbageCollection no_gc;
    Tagged<WeakArrayList> raw = *array;
    // Reload length; GC might have removed elements from the array.
    length = raw->length();
    raw->Set(length, *value);
    raw->set_length(length + 1);
  }
  return array;
}

Handle<WeakArrayList> WeakArrayList::AddToEnd(Isolate* isolate,
                                              Handle<WeakArrayList> array,
                                              MaybeObjectHandle value1,
                                              Tagged<Smi> value2) {
  int length = array->length();
  array = EnsureSpace(isolate, array, length + 2);
  {
    DisallowGarbageCollection no_gc;
    Tagged<WeakArrayList> raw = *array;
    // Reload length; GC might have removed elements from the array.
    length = array->length();
    raw->Set(length, *value1);
    raw->Set(length + 1, value2);
    raw->set_length(length + 2);
  }
  return array;
}

// static
Handle<WeakArrayList> WeakArrayList::Append(Isolate* isolate,
                                            Handle<WeakArrayList> array,
                                            MaybeObjectHandle value,
                                            AllocationType allocation) {
  int length = 0;
  int new_length = 0;
  {
    DisallowGarbageCollection no_gc;
    Tagged<WeakArrayList> raw = *array;
    length = raw->length();

    if (length < raw->capacity()) {
      raw->Set(length, *value);
      raw->set_length(length + 1);
      return array;
    }

    // Not enough space in the array left, either grow, shrink or
    // compact the array.
    new_length = raw->CountLiveElements() + 1;
  }

  bool shrink = new_length < length / 4;
  bool grow = 3 * (length / 4) < new_length;

  if (shrink || grow) {
    // Grow or shrink array and compact out-of-place.
    int new_capacity = CapacityForLength(new_length);
    array = isolate->factory()->CompactWeakArrayList(array, new_capacity,
                                                     allocation);

  } else {
    // Perform compaction in the current array.
    array->Compact(isolate);
  }

  // Now append value to the array, there should always be enough space now.
  DCHECK_LT(array->length(), array->capacity());

  {
    DisallowGarbageCollection no_gc;
    Tagged<WeakArrayList> raw = *array;
    // Reload length, allocation might have killed some weak refs.
    int index = raw->length();
    raw->Set(index, *value);
    raw->set_length(index + 1);
  }
  return array;
}

void WeakArrayList::Compact(Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  int length = this->length();
  int new_length = 0;

  for (int i = 0; i < length; i++) {
    MaybeObject value = Get(isolate, i);

    if (!value->IsCleared()) {
      if (new_length != i) {
        Set(new_length, value);
      }
      ++new_length;
    }
  }

  set_length(new_length);
}

bool WeakArrayList::IsFull() const { return length() == capacity(); }

// static
Handle<WeakArrayList> WeakArrayList::EnsureSpace(Isolate* isolate,
                                                 Handle<WeakArrayList> array,
                                                 int length,
                                                 AllocationType allocation) {
  int capacity = array->capacity();
  if (capacity < length) {
    int grow_by = CapacityForLength(length) - capacity;
    array = isolate->factory()->CopyWeakArrayListAndGrow(array, grow_by,
                                                         allocation);
  }
  return array;
}

int WeakArrayList::CountLiveWeakReferences() const {
  int live_weak_references = 0;
  for (int i = 0; i < length(); i++) {
    if (Get(i)->IsWeak()) {
      ++live_weak_references;
    }
  }
  return live_weak_references;
}

int WeakArrayList::CountLiveElements() const {
  int non_cleared_objects = 0;
  for (int i = 0; i < length(); i++) {
    if (!Get(i)->IsCleared()) {
      ++non_cleared_objects;
    }
  }
  return non_cleared_objects;
}

bool WeakArrayList::RemoveOne(MaybeObjectHandle value) {
  int last_index = length() - 1;
  // Optimize for the most recently added element to be removed again.
  for (int i = last_index; i >= 0; --i) {
    if (Get(i) != *value) continue;
    // Move the last element into this slot (or no-op, if this is the last
    // slot).
    Set(i, Get(last_index));
    Set(last_index, HeapObjectReference::ClearedValue(GetIsolate()));
    set_length(last_index);
    return true;
  }
  return false;
}

bool WeakArrayList::Contains(MaybeObject value) {
  for (int i = 0; i < length(); ++i) {
    if (Get(i) == value) return true;
  }
  return false;
}

}  // namespace internal
}  // namespace v8
